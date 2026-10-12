#include "util/log.h"

#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>

namespace oj {
namespace {

std::atomic<LogLevel> g_level{LogLevel::kInfo};
std::mutex g_write_mutex;

// 用内核 tid 而不是 std::thread::id：判题线程出问题时能直接和 ps -L / gdb 对上。
// SYS_gettid 在所有 glibc 版本上都可用，不依赖 glibc 2.30+ 的 gettid()。
long ThreadId() { return static_cast<long>(::syscall(SYS_gettid)); }

void AppendTimestamp(std::string* out) {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const auto millis = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
  const std::time_t seconds = system_clock::to_time_t(now);

  std::tm tm_buf{};
  localtime_r(&seconds, &tm_buf);

  char date_buf[32];
  std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %H:%M:%S", &tm_buf);

  char full_buf[48];
  std::snprintf(full_buf, sizeof(full_buf), "%s.%03d", date_buf,
                static_cast<int>(millis.count()));
  out->append(full_buf);
}

const char* BaseName(const char* path) {
  const char* slash = std::strrchr(path, '/');
  return slash ? slash + 1 : path;
}

}  // namespace

bool ParseLogLevel(const std::string& text, LogLevel* out) {
  std::string upper;
  upper.reserve(text.size());
  for (const char c : text) {
    upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }

  if (upper == "DEBUG") {
    *out = LogLevel::kDebug;
  } else if (upper == "INFO") {
    *out = LogLevel::kInfo;
  } else if (upper == "WARN" || upper == "WARNING") {
    *out = LogLevel::kWarn;
  } else if (upper == "ERROR") {
    *out = LogLevel::kError;
  } else {
    return false;
  }
  return true;
}

const char* LogLevelName(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug:
      return "DEBUG";
    case LogLevel::kInfo:
      return "INFO";
    case LogLevel::kWarn:
      return "WARN";
    case LogLevel::kError:
      return "ERROR";
  }
  return "?";
}

void SetLogLevel(LogLevel level) { g_level.store(level, std::memory_order_relaxed); }

LogLevel GetLogLevel() { return g_level.load(std::memory_order_relaxed); }

bool ShouldLog(LogLevel level) {
  return static_cast<int>(level) >= static_cast<int>(GetLogLevel());
}

void LogWrite(LogLevel level, const char* file, int line, const std::string& message) {
  if (!ShouldLog(level)) return;

  std::string text;
  text.reserve(message.size() + 80);

  text += '[';
  AppendTimestamp(&text);
  text += "] [";
  text += LogLevelName(level);
  // 补一个空格让 5 个字符宽的级别名对齐，日志更好扫读
  if (level == LogLevel::kDebug || level == LogLevel::kInfo) text += ' ';
  text += "] [tid=";
  text += std::to_string(ThreadId());
  text += ']';

  if (level == LogLevel::kWarn || level == LogLevel::kError) {
    text += " (";
    text += BaseName(file);
    text += ':';
    text += std::to_string(line);
    text += ')';
  }

  text += ' ';
  text += message;
  text += '\n';

  std::lock_guard<std::mutex> lock(g_write_mutex);
  std::fwrite(text.data(), 1, text.size(), stdout);
  std::fflush(stdout);
}

}  // namespace oj
