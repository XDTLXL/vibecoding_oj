// VibeCoding OJ — 服务入口（M0：环境与骨架）
//
// 本阶段只做三件事：加载配置、初始化日志、启动最小 SSLServer 并跑通 /api/health。
// 数据库、认证、判题等装配在 M1/M2/M3 接入（SPEC.md §15）。

#include <signal.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

// SPEC.md §4.2：必须定义 CPPHTTPLIB_OPENSSL_SUPPORT，否则 httplib 里根本不存在
// SSLServer，而报错会出现在离原因很远的地方（"SSLServer was not declared"）。
// 在包含头文件之前显式挡住，直接说清原因。
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#error "必须定义 CPPHTTPLIB_OPENSSL_SUPPORT（Makefile 已通过 -D 传入，见 SPEC.md §4.2）"
#endif
#include "httplib.h"

#include "config.h"
#include "json.h"
#include "util/log.h"

namespace {

constexpr char kVersion[] = "0.1.0";
constexpr char kDefaultConfigPath[] = "config/config.ini";

// 退出码：0 正常；2 配置/参数错误；3 证书缺失；4 证书非法；5 端口占用；6 启动失败
constexpr int kExitUsage = 2;
constexpr int kExitCertMissing = 3;
constexpr int kExitCertInvalid = 4;
constexpr int kExitBindFailed = 5;
constexpr int kExitListenFailed = 6;

// 统一响应体（SPEC.md §7.1）：{ "code": 0, "message": "ok", "data": {...} }
// 用 ordered_json 让键顺序与 SPEC 的示例一致（默认的 json 会按字典序输出）。
std::string Envelope(int code, const std::string& message, nlohmann::ordered_json data) {
  nlohmann::ordered_json body;
  body["code"] = code;
  body["message"] = message;
  body["data"] = std::move(data);
  return body.dump();
}

void PrintUsage(const char* argv0) {
  std::printf(
      "VibeCoding OJ v%s\n"
      "\n"
      "用法: %s [选项]\n"
      "\n"
      "选项:\n"
      "  -c, --config <路径>   配置文件路径，默认 %s\n"
      "  -h, --help            显示本帮助\n"
      "\n"
      "首次运行前的准备:\n"
      "  tools/gen_cert.sh                                  生成自签证书\n"
      "  cp config/config.ini.example config/config.ini     准备配置并填写 jwt_secret\n"
      "  sudo mkdir -p /tmp/oj_build && sudo chmod 1777 /tmp/oj_build\n"
      "\n"
      "健康检查: curl -k https://localhost:8443/api/health\n",
      kVersion, argv0, kDefaultConfigPath);
}

bool IsRegularFile(const std::string& path) {
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec);
}

// 请求行未必解析得出来：客户端在 TLS 握手阶段拒绝自签证书并断开时，
// method 与 path 都是空的，直接打 "  -> 400" 会是一行读不懂的日志
std::string DescribeRequest(const httplib::Request& req) {
  if (req.path.empty()) return "(无请求行：TLS 握手失败，或客户端中途断开)";
  return req.method + " " + req.path;
}

// 端口占用探测。
//
// 必须有这一步：httplib 在 bind 时会设置 SO_REUSEPORT（见 httplib.h 的
// default_socket_options），于是同一端口上可以同时存在两个监听 socket，
// 内核把新连接按轮转分给它们。结果是 bind_to_port 根本不会因"端口被占用"而失败——
// 第二个实例照样启动成功，但服务实际上变成"两个进程各处理一半请求"，
// 排查起来非常痛苦（配置改了不生效、判题状态时有时无）。
//
// 所以这里自己探一次：只设 SO_REUSEADDR、刻意不设 SO_REUSEPORT。
//   - 对端是"已有进程在监听"  → bind 报 EADDRINUSE，如实拦下
//   - 对端是"上次退出留下的 TIME_WAIT" → SO_REUSEADDR 让 bind 成功，不误报
bool IsPortAvailable(const std::string& bind_host, int port, std::string* reason) {
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<std::uint16_t>(port));
  if (::inet_pton(AF_INET, bind_host.c_str(), &address.sin_addr) != 1) {
    // 不是 IPv4 字面量（例如配置里写了 localhost）：交给 httplib 自己判定
    return true;
  }

  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return true;  // 探测不了就不挡路，别让辅助检查变成新的故障点

  int reuse = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  const bool available =
      ::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
  if (!available) *reason = std::strerror(errno);
  ::close(fd);
  return available;
}

}  // namespace

int main(int argc, char** argv) {
  std::string config_path = kDefaultConfigPath;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      PrintUsage(argv[0]);
      return 0;
    }
    if (arg == "-c" || arg == "--config") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "[FATAL] %s 需要一个参数\n", arg.c_str());
        return kExitUsage;
      }
      config_path = argv[++i];
      continue;
    }
    std::fprintf(stderr, "[FATAL] 未知参数：%s（用 --help 查看用法）\n", arg.c_str());
    return kExitUsage;
  }

  // —— 配置 ——
  // 此时日志级别还没读出来，失败只能用 stderr 裸输出
  oj::Config config;
  std::string config_error;
  if (!oj::Config::Load(config_path, &config, &config_error)) {
    std::fprintf(stderr, "[FATAL] 配置加载失败（%s）：\n%s\n", config_path.c_str(),
                 config_error.c_str());
    return kExitUsage;
  }

  if (oj::LogLevel level; oj::ParseLogLevel(config.log().level, &level)) {
    oj::SetLogLevel(level);
  } else {
    std::fprintf(stderr, "[WARN] 无法识别的日志级别 \"%s\"，使用默认 INFO\n",
                 config.log().level.c_str());
  }

  OJ_LOG_INFO << "VibeCoding OJ v" << kVersion << " 启动中";
  OJ_LOG_INFO << "配置已加载（" << config_path << "）：" << config.Summary();

  // —— 证书预检 ——
  // 先自己检查文件是否存在：否则 OpenSSL 只会抛一句底层报错，
  // 用户看到的是 SSL 上下文失败，而不是"该去跑哪个脚本"。
  for (const std::string& path :
       {config.server().cert_file, config.server().key_file}) {
    if (!IsRegularFile(path)) {
      OJ_LOG_ERROR << "证书文件不存在：" << path << "；执行 tools/gen_cert.sh 生成自签证书";
      return kExitCertMissing;
    }
  }

  httplib::SSLServer server(config.server().cert_file.c_str(),
                            config.server().key_file.c_str());
  if (!server.is_valid()) {
    OJ_LOG_ERROR << "SSL 上下文创建失败，证书或私钥无法加载：" << config.server().cert_file
                 << " / " << config.server().key_file
                 << "；可先用 openssl x509 -in <证书> -noout -text 确认文件有效";
    return kExitCertInvalid;
  }

  // —— 服务配置 ——
  // httplib 默认的请求体上限是 SIZE_MAX（等于不限制），必须显式收窄
  server.set_payload_max_length(config.server().max_payload_bytes);
  // 超时按"最慢的合法请求"定：M3 的测试数据上传是 multipart 大文件，留 30s
  server.set_read_timeout(30, 0);
  server.set_write_timeout(30, 0);

  // 静态文件根目录。M0 还没有前端页面，缺失时只告警不报错（页面在 M4 落地）
  std::error_code fs_error;
  if (std::filesystem::is_directory(config.server().static_root, fs_error)) {
    if (server.set_mount_point("/", config.server().static_root)) {
      OJ_LOG_INFO << "静态目录已挂载：" << config.server().static_root << " -> /";
    } else {
      OJ_LOG_WARN << "静态目录挂载失败：" << config.server().static_root;
    }
  } else {
    OJ_LOG_WARN << "静态目录不存在，跳过挂载（前端页面在 M4 落地）："
                << config.server().static_root;
  }

  // —— 路由 ——
  const auto started_at = std::chrono::steady_clock::now();
  server.Get("/api/health", [started_at](const httplib::Request&, httplib::Response& res) {
    const auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::steady_clock::now() - started_at)
                                    .count();
    nlohmann::ordered_json data;
    data["status"] = "ok";
    data["version"] = kVersion;
    data["uptime_s"] = uptime_seconds;
    // M3 在此追加 queue_len / judge_busy（SPEC.md §15 M3）
    res.set_content(Envelope(0, "ok", std::move(data)), "application/json; charset=utf-8");
  });

  // 未捕获异常：对外只给统一错误码，细节进日志，不外泄实现信息（SPEC.md §7.1）
  server.set_exception_handler([](const httplib::Request& req, httplib::Response& res,
                                  std::exception_ptr ep) {
    std::string detail = "非 std::exception 类型";
    try {
      std::rethrow_exception(ep);
    } catch (const std::exception& e) {
      detail = e.what();
    } catch (...) {
    }
    OJ_LOG_ERROR << DescribeRequest(req) << " 未捕获异常：" << detail;
    res.status = 500;
    res.set_content(Envelope(5000, "服务器内部错误", nlohmann::ordered_json::object()),
                    "application/json; charset=utf-8");
  });

  // 错误响应（status >= 400）。业务层可能已经写好响应体，此时不能覆盖。
  server.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
    if (!res.body.empty()) return;

    if (res.status == 404) {
      // SPEC.md §7.1 未规定 404 的响应体；这里给统一响应体便于前端统一处理，
      // 沿用 SPEC 错误码表的 1004（资源不存在）
      OJ_LOG_INFO << DescribeRequest(req) << " -> 404";
      res.set_content(Envelope(1004, "接口不存在", nlohmann::ordered_json::object()),
                      "application/json; charset=utf-8");
      return;
    }
    OJ_LOG_WARN << DescribeRequest(req) << " -> " << res.status;
    res.set_content("HTTP " + std::to_string(res.status), "text/plain; charset=utf-8");
  });

  // —— 绑定端口 ——
  // 先自己探一次端口占用：SO_REUSEPORT 会让 bind 成功，靠 bind 的返回值判断不出来
  std::string port_reason;
  if (!IsPortAvailable(config.server().bind_host, config.server().port, &port_reason)) {
    OJ_LOG_ERROR << "端口已被占用：" << config.server().bind_host << ':' << config.server().port
                 << "（" << port_reason
                 << "）；可能已有一个 oj_server 在运行（pgrep -a oj_server），"
                 << "或把 config/config.ini 的 [server] port 改成别的端口";
    return kExitBindFailed;
  }

  // 再 bind：这一步的失败原因是"端口需要更高权限"等自己探不出来的情况
  if (!server.bind_to_port(config.server().bind_host, config.server().port)) {
    OJ_LOG_ERROR << "端口绑定失败：" << config.server().bind_host << ':' << config.server().port
                 << "（该端口可能需要更高权限）";
    return kExitBindFailed;
  }

  // —— 信号 ——
  // 先屏蔽 SIGINT/SIGTERM，再由主线程 sigwait。
  // 服务线程由主线程创建，会继承这个屏蔽集，因此信号只会送到主线程。
  // 这样就不用在信号处理函数里调用 svr.stop()（那不是异步信号安全的）。
  sigset_t signal_set;
  sigemptyset(&signal_set);
  sigaddset(&signal_set, SIGINT);
  sigaddset(&signal_set, SIGTERM);
  if (::pthread_sigmask(SIG_BLOCK, &signal_set, nullptr) != 0) {
    OJ_LOG_ERROR << "屏蔽信号失败：" << std::strerror(errno);
    return kExitListenFailed;
  }

  std::atomic<bool> listen_failed{false};
  std::thread listen_thread([&server, &listen_failed] {
    if (!server.listen_after_bind()) listen_failed.store(true);
  });

  for (int i = 0; i < 500 && !server.is_running() && !listen_failed.load(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!server.is_running()) {
    server.stop();
    listen_thread.join();
    OJ_LOG_ERROR << "服务未能进入监听状态";
    return kExitListenFailed;
  }

  OJ_LOG_INFO << "HTTPS 服务已启动：https://" << config.server().bind_host << ':'
              << config.server().port;
  OJ_LOG_INFO << "健康检查：curl -k https://localhost:" << config.server().port << "/api/health";
  OJ_LOG_INFO << "自签证书会让浏览器告警，这是预期行为（SPEC.md §13 R5）";
  OJ_LOG_INFO << "按 Ctrl-C 停止服务";

  int received_signal = 0;
  if (::sigwait(&signal_set, &received_signal) == 0) {
    OJ_LOG_INFO << "收到信号 " << received_signal << "，停止服务...";
  }

  server.stop();
  listen_thread.join();
  OJ_LOG_INFO << "服务已停止";
  return 0;
}
