#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

#include "config.h"
#include "test_util.h"

namespace {

// 一份覆盖全部字段的合法配置，用来验证"每个键都被真的读到了"
const char* const kFullConfig = R"INI(
# 全量配置
[server]
bind_host = 127.0.0.1
port = 9443
static_root = web
cert_file = certs/server.crt
key_file = certs/server.key
max_payload_bytes = 1048576

[mysql]
host = 10.0.0.5
port = 3307
user = oj_rw
password = p#ss;word
database = oj_test
pool_size = 4

[auth]
jwt_secret = 0123456789abcdef0123456789abcdef
jwt_expire_hours = 12
pbkdf2_iterations = 1000

[judge]
threads = 2
queue_capacity = 7
compile_timeout_ms = 5000
max_code_bytes = 1024
max_output_bytes = 2048
max_cases_per_problem = 9
run_result_ttl_seconds = 60

[paths]
data_root = data/test_problems
build_root = /tmp/oj_build_test
sandbox_uid = nobody

[log]
level = DEBUG
)INI";

// 写到一个临时文件，析构时删除
class TempConfig {
 public:
  explicit TempConfig(const std::string& content) {
    path_ = std::filesystem::temp_directory_path() /
            ("oj_config_test_" + std::to_string(::getpid()) + ".ini");
    std::ofstream out(path_);
    out << content;
  }
  ~TempConfig() {
    std::error_code ec;
    std::filesystem::remove(path_, ec);
  }

  TempConfig(const TempConfig&) = delete;
  TempConfig& operator=(const TempConfig&) = delete;

  std::string path() const { return path_.string(); }

 private:
  std::filesystem::path path_;
};

bool LoadOk(const std::string& content, oj::Config* config) {
  TempConfig file(content);
  std::string err;
  const bool ok = oj::Config::Load(file.path(), config, &err);
  if (!ok) std::cout << "        配置错误信息: " << err << std::endl;
  return ok;
}

// 期望加载失败，并返回错误信息
std::string LoadError(const std::string& content) {
  TempConfig file(content);
  oj::Config config;
  std::string err;
  if (oj::Config::Load(file.path(), &config, &err)) {
    std::cout << "        期望加载失败，但成功了" << std::endl;
    return {};
  }
  return err;
}

void TestFullConfig() {
  oj::Config config;
  const bool ok = LoadOk(kFullConfig, &config);
  OJ_CHECK(ok);
  if (!ok) return;

  OJ_CHECK_EQ(config.server().bind_host, std::string("127.0.0.1"));
  OJ_CHECK_EQ(config.server().port, 9443);
  OJ_CHECK_EQ(config.server().static_root, std::string("web"));
  OJ_CHECK_EQ(config.server().cert_file, std::string("certs/server.crt"));
  OJ_CHECK_EQ(config.server().key_file, std::string("certs/server.key"));
  OJ_CHECK_EQ(config.server().max_payload_bytes, std::size_t(1048576));

  OJ_CHECK_EQ(config.mysql().host, std::string("10.0.0.5"));
  OJ_CHECK_EQ(config.mysql().port, 3307);
  OJ_CHECK_EQ(config.mysql().user, std::string("oj_rw"));
  // 行尾不解析注释：值里的 # 和 ; 必须原样保留，否则口令会被截断
  OJ_CHECK_EQ(config.mysql().password, std::string("p#ss;word"));
  OJ_CHECK_EQ(config.mysql().database, std::string("oj_test"));
  OJ_CHECK_EQ(config.mysql().pool_size, 4);

  OJ_CHECK_EQ(config.auth().jwt_secret, std::string("0123456789abcdef0123456789abcdef"));
  OJ_CHECK_EQ(config.auth().jwt_expire_hours, 12);
  OJ_CHECK_EQ(config.auth().pbkdf2_iterations, 1000);

  OJ_CHECK_EQ(config.judge().threads, 2);
  OJ_CHECK_EQ(config.judge().queue_capacity, 7);
  OJ_CHECK_EQ(config.judge().compile_timeout_ms, 5000);
  OJ_CHECK_EQ(config.judge().max_code_bytes, std::size_t(1024));
  OJ_CHECK_EQ(config.judge().max_output_bytes, std::size_t(2048));
  OJ_CHECK_EQ(config.judge().max_cases_per_problem, 9);
  OJ_CHECK_EQ(config.judge().run_result_ttl_seconds, 60);

  OJ_CHECK_EQ(config.paths().data_root, std::string("data/test_problems"));
  OJ_CHECK_EQ(config.paths().build_root, std::string("/tmp/oj_build_test"));
  OJ_CHECK_EQ(config.paths().sandbox_uid, std::string("nobody"));

  OJ_CHECK_EQ(config.log().level, std::string("DEBUG"));
}

void TestMissingKeysUseDefaults() {
  oj::Config config;
  OJ_CHECK(LoadOk("[auth]\njwt_secret = abc\n", &config));

  OJ_CHECK_EQ(config.server().bind_host, std::string("0.0.0.0"));
  OJ_CHECK_EQ(config.server().port, 8443);
  OJ_CHECK_EQ(config.server().max_payload_bytes, std::size_t(32u * 1024 * 1024));
  OJ_CHECK_EQ(config.mysql().database, std::string("oj"));
  OJ_CHECK_EQ(config.mysql().pool_size, 8);
  OJ_CHECK_EQ(config.judge().threads, 4);
  OJ_CHECK_EQ(config.judge().queue_capacity, 100);
  OJ_CHECK_EQ(config.judge().max_code_bytes, std::size_t(65536));
  OJ_CHECK_EQ(config.judge().max_output_bytes, std::size_t(16u * 1024 * 1024));
  OJ_CHECK_EQ(config.paths().build_root, std::string("/tmp/oj_build"));
  OJ_CHECK_EQ(config.log().level, std::string("INFO"));
}

void TestMissingFile() {
  oj::Config config;
  std::string err;
  OJ_CHECK(!oj::Config::Load("/tmp/oj_no_such_config_file_12345.ini", &config, &err));
  OJ_CHECK(err.find("无法打开文件") != std::string::npos);
}

void TestEmptySecretRejected() {
  // 留空 jwt_secret 必须拒绝启动：否则任何人都能伪造管理员 token
  const std::string err = LoadError("[auth]\njwt_secret =\n");
  OJ_CHECK(!err.empty());
  OJ_CHECK(err.find("jwt_secret") != std::string::npos);
}

void TestMissingAuthSectionRejected() {
  const std::string err = LoadError("[server]\nport = 9000\n");
  OJ_CHECK(err.find("jwt_secret") != std::string::npos);
}

void TestBadIntegerRejected() {
  const std::string err = LoadError("[auth]\njwt_secret = abc\n[server]\nport = 84a3\n");
  OJ_CHECK(err.find("server.port") != std::string::npos);
  OJ_CHECK(err.find("不是合法整数") != std::string::npos);
}

void TestOutOfRangeRejected() {
  const std::string err = LoadError("[auth]\njwt_secret = abc\n[server]\nport = 70000\n");
  OJ_CHECK(err.find("server.port") != std::string::npos);
  OJ_CHECK(err.find("超出允许范围") != std::string::npos);
}

void TestZeroByteSizeRejected() {
  // 0 字节上限会让任何请求都失败，属于配置错误而不是"合法但严格"
  const std::string err =
      LoadError("[auth]\njwt_secret = abc\n[judge]\nmax_code_bytes = 0\n");
  OJ_CHECK(err.find("max_code_bytes") != std::string::npos);
}

void TestNegativeSizeRejected() {
  const std::string err =
      LoadError("[auth]\njwt_secret = abc\n[judge]\nmax_output_bytes = -1\n");
  OJ_CHECK(err.find("max_output_bytes") != std::string::npos);
}

void TestDuplicateKeyRejected() {
  const std::string err = LoadError("[auth]\njwt_secret = a\njwt_secret = b\n");
  OJ_CHECK(err.find("重复定义") != std::string::npos);
}

void TestKeyBeforeSectionRejected() {
  const std::string err = LoadError("port = 8443\n");
  OJ_CHECK(err.find("任何 [段] 之前") != std::string::npos);
}

void TestNonKeyValueLineRejected() {
  const std::string err = LoadError("[auth]\nthis line has no equals sign\n");
  OJ_CHECK(err.find("key = value") != std::string::npos);
}

void TestMalformedSectionRejected() {
  const std::string err = LoadError("[auth\njwt_secret = abc\n");
  OJ_CHECK(err.find("右方括号") != std::string::npos);
}

void TestCommentsAndBlankLines() {
  // 整行注释与空行应被忽略；含 '#' 的段名后面仍要能正常解析
  const std::string content =
      "# 整行注释\n"
      "\n"
      "; 另一种注释\n"
      "   \n"
      "[auth]\n"
      "# 段内注释\n"
      "jwt_secret = secret_value\n";
  oj::Config config;
  OJ_CHECK(LoadOk(content, &config));
  OJ_CHECK_EQ(config.auth().jwt_secret, std::string("secret_value"));
}

void TestSummaryHidesSecrets() {
  oj::Config config;
  OJ_CHECK(LoadOk(kFullConfig, &config));

  const std::string summary = config.Summary();
  // jwt_secret 与数据库口令绝不能出现在日志里（SPEC.md §11.4）
  OJ_CHECK(summary.find("0123456789abcdef0123456789abcdef") == std::string::npos);
  OJ_CHECK(summary.find("p#ss;word") == std::string::npos);
  // 非敏感项正常展示，确保摘要是"有用但不泄密"
  OJ_CHECK(summary.find("9443") != std::string::npos);
  OJ_CHECK(summary.find("10.0.0.5") != std::string::npos);
}

}  // namespace

void RunConfigTests() {
  TestFullConfig();
  TestMissingKeysUseDefaults();
  TestMissingFile();
  TestEmptySecretRejected();
  TestMissingAuthSectionRejected();
  TestBadIntegerRejected();
  TestOutOfRangeRejected();
  TestZeroByteSizeRejected();
  TestNegativeSizeRejected();
  TestDuplicateKeyRejected();
  TestKeyBeforeSectionRejected();
  TestNonKeyValueLineRejected();
  TestMalformedSectionRejected();
  TestCommentsAndBlankLines();
  TestSummaryHidesSecrets();
}
