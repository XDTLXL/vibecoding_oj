#include "config.h"

#include <charconv>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace oj {
namespace {

std::string Trim(const std::string& text) {
  constexpr const char* kWhitespace = " \t\r\n\f\v";
  const std::size_t begin = text.find_first_not_of(kWhitespace);
  if (begin == std::string::npos) return {};
  const std::size_t end = text.find_last_not_of(kWhitespace);
  return text.substr(begin, end - begin + 1);
}

struct IniEntry {
  std::string value;
  int line = 0;
};

using IniSection = std::map<std::string, IniEntry>;
using IniFile = std::map<std::string, IniSection>;

// 只支持整行注释（行首 # 或 ;）。
// 刻意不支持行尾注释：口令里常含 # 和 ;，支持行尾注释就会把口令的后半截当注释吃掉，
// 而且这种错误极难排查（认证失败，但配置看上去"没问题"）。
bool ParseIni(const std::string& text, IniFile* out, std::string* err) {
  std::istringstream stream(text);
  std::string raw;
  std::string section;
  int lineno = 0;

  while (std::getline(stream, raw)) {
    ++lineno;
    const std::string line = Trim(raw);
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;

    if (line[0] == '[') {
      if (line.back() != ']') {
        *err = "第 " + std::to_string(lineno) + " 行：段名缺少右方括号 ']' —— " + line;
        return false;
      }
      section = Trim(line.substr(1, line.size() - 2));
      if (section.empty()) {
        *err = "第 " + std::to_string(lineno) + " 行：段名为空";
        return false;
      }
      (*out)[section];  // 登记空段，便于后续区分"段存在但键缺失"
      continue;
    }

    const std::size_t eq = line.find('=');
    if (eq == std::string::npos) {
      *err = "第 " + std::to_string(lineno) + " 行：不是 key = value 形式 —— " + line;
      return false;
    }

    const std::string key = Trim(line.substr(0, eq));
    const std::string value = Trim(line.substr(eq + 1));
    if (key.empty()) {
      *err = "第 " + std::to_string(lineno) + " 行：键名为空";
      return false;
    }
    if (section.empty()) {
      *err = "第 " + std::to_string(lineno) + " 行：键 " + key + " 出现在任何 [段] 之前";
      return false;
    }

    const auto inserted = (*out)[section].emplace(key, IniEntry{value, lineno});
    if (!inserted.second) {
      // 重复定义按错误处理：多半是改配置时只改了其中一处，
      // 静默取后一个会出现"改了不生效"的诡异现象
      *err = "第 " + std::to_string(lineno) + " 行：键 " + section + "." + key +
             " 重复定义（首次出现在第 " + std::to_string(inserted.first->second.line) + " 行）";
      return false;
    }
  }
  return true;
}

bool ParseInteger(const std::string& text, long long* out) {
  if (text.empty()) return false;
  const char* begin = text.data();
  const char* end = begin + text.size();
  const std::from_chars_result result = std::from_chars(begin, end, *out);
  return result.ec == std::errc() && result.ptr == end;
}

bool ParseByteSize(const std::string& text, unsigned long long* out) {
  // from_chars 对无符号类型不接受前导 '-'，这里额外挡一次，让报错信息更明确
  if (text.empty() || text[0] == '-') return false;
  const char* begin = text.data();
  const char* end = begin + text.size();
  const std::from_chars_result result = std::from_chars(begin, end, *out);
  return result.ec == std::errc() && result.ptr == end;
}

// 按段/键名读取配置，缺失时返回调用方给的默认值；
// 存在但非法时记入 problems_（不是静默取默认值，否则配置写错会毫无提示）。
class Reader {
 public:
  Reader(const IniFile& ini, std::vector<std::string>* problems)
      : ini_(ini), problems_(problems) {}

  std::string Str(const char* section, const char* key, const std::string& fallback) const {
    const IniEntry* entry = Find(section, key);
    return entry ? entry->value : fallback;
  }

  int Int(const char* section, const char* key, int fallback, int min_value, int max_value) const {
    const IniEntry* entry = Find(section, key);
    if (!entry) return fallback;

    long long parsed = 0;
    if (!ParseInteger(entry->value, &parsed)) {
      AddProblem(section, key, "不是合法整数：" + entry->value, entry->line);
      return fallback;
    }
    if (parsed < min_value || parsed > max_value) {
      AddProblem(section, key,
                 "超出允许范围 [" + std::to_string(min_value) + ", " + std::to_string(max_value) +
                     "]：" + entry->value,
                 entry->line);
      return fallback;
    }
    return static_cast<int>(parsed);
  }

  std::size_t Size(const char* section, const char* key, std::size_t fallback,
                   unsigned long long max_value) const {
    const IniEntry* entry = Find(section, key);
    if (!entry) return fallback;

    unsigned long long parsed = 0;
    if (!ParseByteSize(entry->value, &parsed) || parsed == 0 || parsed > max_value) {
      AddProblem(section, key,
                 "不是合法的字节数（1 ~ " + std::to_string(max_value) + "）：" + entry->value,
                 entry->line);
      return fallback;
    }
    return static_cast<std::size_t>(parsed);
  }

 private:
  const IniEntry* Find(const char* section, const char* key) const {
    const auto section_it = ini_.find(section);
    if (section_it == ini_.end()) return nullptr;
    const auto entry_it = section_it->second.find(key);
    return entry_it == section_it->second.end() ? nullptr : &entry_it->second;
  }

  void AddProblem(const char* section, const char* key, const std::string& reason,
                  int line) const {
    problems_->push_back(std::string(section) + "." + key + "（第 " + std::to_string(line) +
                         " 行）：" + reason);
  }

  const IniFile& ini_;
  std::vector<std::string>* problems_;
};

std::string JoinProblems(const std::vector<std::string>& problems) {
  std::string joined;
  for (const std::string& problem : problems) {
    if (!joined.empty()) joined += '\n';
    joined += "  - " + problem;
  }
  return joined;
}

}  // namespace

bool Config::Load(const std::string& path, Config* out, std::string* err) {
  if (out == nullptr || err == nullptr) return false;
  err->clear();

  std::ifstream file(path);
  if (!file) {
    *err = "  - 无法打开文件：" + path +
           "\n    首次使用执行：cp config/config.ini.example config/config.ini";
    return false;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();

  IniFile ini;
  std::string parse_error;
  if (!ParseIni(buffer.str(), &ini, &parse_error)) {
    *err = "  - " + parse_error;
    return false;
  }

  std::vector<std::string> problems;
  const Reader reader(ini, &problems);

  Config cfg;
  cfg.server_.bind_host = reader.Str("server", "bind_host", cfg.server_.bind_host);
  cfg.server_.port = reader.Int("server", "port", cfg.server_.port, 1, 65535);
  cfg.server_.static_root = reader.Str("server", "static_root", cfg.server_.static_root);
  cfg.server_.cert_file = reader.Str("server", "cert_file", cfg.server_.cert_file);
  cfg.server_.key_file = reader.Str("server", "key_file", cfg.server_.key_file);
  cfg.server_.max_payload_bytes =
      reader.Size("server", "max_payload_bytes", cfg.server_.max_payload_bytes, 1ull << 40);

  cfg.mysql_.host = reader.Str("mysql", "host", cfg.mysql_.host);
  cfg.mysql_.port = reader.Int("mysql", "port", cfg.mysql_.port, 1, 65535);
  cfg.mysql_.user = reader.Str("mysql", "user", cfg.mysql_.user);
  cfg.mysql_.password = reader.Str("mysql", "password", cfg.mysql_.password);
  cfg.mysql_.database = reader.Str("mysql", "database", cfg.mysql_.database);
  cfg.mysql_.pool_size = reader.Int("mysql", "pool_size", cfg.mysql_.pool_size, 1, 1024);

  cfg.auth_.jwt_secret = reader.Str("auth", "jwt_secret", cfg.auth_.jwt_secret);
  cfg.auth_.jwt_expire_hours =
      reader.Int("auth", "jwt_expire_hours", cfg.auth_.jwt_expire_hours, 1, 24 * 365);
  cfg.auth_.pbkdf2_iterations =
      reader.Int("auth", "pbkdf2_iterations", cfg.auth_.pbkdf2_iterations, 1000, 10000000);

  cfg.judge_.threads = reader.Int("judge", "threads", cfg.judge_.threads, 1, 64);
  cfg.judge_.queue_capacity =
      reader.Int("judge", "queue_capacity", cfg.judge_.queue_capacity, 1, 100000);
  cfg.judge_.compile_timeout_ms =
      reader.Int("judge", "compile_timeout_ms", cfg.judge_.compile_timeout_ms, 100, 600000);
  cfg.judge_.max_code_bytes =
      reader.Size("judge", "max_code_bytes", cfg.judge_.max_code_bytes, 1ull << 30);
  cfg.judge_.max_output_bytes =
      reader.Size("judge", "max_output_bytes", cfg.judge_.max_output_bytes, 1ull << 40);
  cfg.judge_.max_cases_per_problem = reader.Int("judge", "max_cases_per_problem",
                                                cfg.judge_.max_cases_per_problem, 1, 10000);
  cfg.judge_.run_result_ttl_seconds = reader.Int("judge", "run_result_ttl_seconds",
                                                 cfg.judge_.run_result_ttl_seconds, 1, 86400);

  cfg.paths_.data_root = reader.Str("paths", "data_root", cfg.paths_.data_root);
  cfg.paths_.build_root = reader.Str("paths", "build_root", cfg.paths_.build_root);
  cfg.paths_.sandbox_uid = reader.Str("paths", "sandbox_uid", cfg.paths_.sandbox_uid);

  cfg.log_.level = reader.Str("log", "level", cfg.log_.level);

  // —— 必填项校验 ——
  // 这里只拦"配置非法"，不拦"配置缺失"：缺失项有合理默认值。
  if (cfg.auth_.jwt_secret.empty()) {
    problems.push_back(
        "auth.jwt_secret 为空：留空等于任何人都能伪造任意用户的 token，因此拒绝启动。"
        "生成方式：openssl rand -hex 32，把结果填进 config/config.ini 的 [auth] jwt_secret");
  }
  if (cfg.server_.static_root.empty()) problems.push_back("server.static_root 为空");
  if (cfg.server_.cert_file.empty()) problems.push_back("server.cert_file 为空");
  if (cfg.server_.key_file.empty()) problems.push_back("server.key_file 为空");
  if (cfg.mysql_.host.empty()) problems.push_back("mysql.host 为空");
  if (cfg.mysql_.database.empty()) problems.push_back("mysql.database 为空");
  if (cfg.paths_.data_root.empty()) problems.push_back("paths.data_root 为空");
  if (cfg.paths_.build_root.empty()) problems.push_back("paths.build_root 为空");
  if (cfg.paths_.sandbox_uid.empty()) problems.push_back("paths.sandbox_uid 为空");

  if (!problems.empty()) {
    *err = JoinProblems(problems);
    return false;
  }

  *out = std::move(cfg);
  return true;
}

std::string Config::Summary() const {
  std::ostringstream out;
  out << "server=" << server_.bind_host << ':' << server_.port
      << " static_root=" << server_.static_root << " max_payload=" << server_.max_payload_bytes
      << "B"
      << " | mysql=" << mysql_.user << '@' << mysql_.host << ':' << mysql_.port << '/'
      << mysql_.database << " pool=" << mysql_.pool_size
      // 口令与密钥只回显"是否已设置"，不回显内容（SPEC.md §11.4）
      << " password=" << (mysql_.password.empty() ? "<未设置>" : "<已设置>")
      << " | auth=jwt_secret<" << auth_.jwt_secret.size() << "字符>"
      << " expire=" << auth_.jwt_expire_hours << "h"
      << " pbkdf2_iter=" << auth_.pbkdf2_iterations
      << " | judge=threads=" << judge_.threads << " queue=" << judge_.queue_capacity
      << " max_code=" << judge_.max_code_bytes << "B"
      << " max_output=" << judge_.max_output_bytes << "B"
      << " compile_timeout=" << judge_.compile_timeout_ms << "ms"
      << " cases_per_problem<=" << judge_.max_cases_per_problem
      << " | paths=data_root=" << paths_.data_root << " build_root=" << paths_.build_root
      << " sandbox_uid=" << paths_.sandbox_uid << " | log=" << log_.level;
  return out.str();
}

}  // namespace oj
