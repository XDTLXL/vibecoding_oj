#pragma once

// 配置读取（SPEC.md §11.3）
//
// 配置文件为 ini 格式，默认路径 config/config.ini；内容示例与逐项说明见
// config/config.ini.example。结构体里的默认值与示例文件保持一致：
// 配置项缺失时按默认值走，配置项存在但非法时拒绝启动。

#include <cstddef>
#include <string>

namespace oj {

struct ServerConfig {
  // 0.0.0.0 = 监听所有网卡（WSL2 下从 Windows 浏览器访问需要它）
  std::string bind_host = "0.0.0.0";
  int port = 8443;
  std::string static_root = "web";
  std::string cert_file = "certs/server.crt";
  std::string key_file = "certs/server.key";
  // 单请求体上限。httplib 内置默认值是 SIZE_MAX（等于不限制），必须显式设置，
  // 否则一个超大 POST 就能把进程内存吃光。需覆盖 M3 的 multipart 测试数据上传。
  std::size_t max_payload_bytes = 32u * 1024 * 1024;
};

struct MysqlConfig {
  std::string host = "127.0.0.1";
  int port = 3306;
  std::string user = "oj";
  std::string password;
  std::string database = "oj";
  // 连接池大小；DAO 层「每线程独立连接」的上限（SPEC.md §5.3）
  int pool_size = 8;
};

struct AuthConfig {
  // JWT 签名密钥。留空即拒绝启动（SPEC.md §10.1：不硬编码进源码，也不允许没有）
  std::string jwt_secret;
  int jwt_expire_hours = 24;
  int pbkdf2_iterations = 210000;
};

struct JudgeConfig {
  int threads = 4;
  int queue_capacity = 100;
  int compile_timeout_ms = 10000;
  std::size_t max_code_bytes = 65536;
  std::size_t max_output_bytes = 16u * 1024 * 1024;
  int max_cases_per_problem = 100;
  int run_result_ttl_seconds = 600;
};

struct PathsConfig {
  std::string data_root = "data/problems";
  std::string build_root = "/tmp/oj_build";
  std::string sandbox_uid = "nobody";
};

struct LogConfig {
  std::string level = "INFO";
};

class Config {
 public:
  // 读取并校验配置文件。
  // 失败时返回 false，*err 为可直接展示给用户的原因（多行，每行一个待修正项）。
  static bool Load(const std::string& path, Config* out, std::string* err);

  const ServerConfig& server() const { return server_; }
  const MysqlConfig& mysql() const { return mysql_; }
  const AuthConfig& auth() const { return auth_; }
  const JudgeConfig& judge() const { return judge_; }
  const PathsConfig& paths() const { return paths_; }
  const LogConfig& log() const { return log_; }

  // 单行启动摘要。刻意不回显 password 与 jwt_secret
  //（SPEC.md §11.4 禁止记录密码与 JWT 原文；密钥同理）。
  std::string Summary() const;

 private:
  ServerConfig server_;
  MysqlConfig mysql_;
  AuthConfig auth_;
  JudgeConfig judge_;
  PathsConfig paths_;
  LogConfig log_;
};

}  // namespace oj
