#pragma once

// 分级日志（SPEC.md §11.4）
//
//   - 级别：DEBUG / INFO / WARN / ERROR，由 config 的 [log] level 控制
//   - 输出：stdout（配合 nohup 重定向到文件）
//   - 线程安全：一条日志整体加锁写出，多线程下不会交错
//   - 禁止记录：密码、JWT 原文、源代码内容（调用方负责）
//
// 用法：
//   OJ_LOG_INFO << "server listening on " << host << ':' << port;
//
// 输出格式：
//   [2026-10-12 11:57:03.123] [INFO ] [tid=12345] message
//   [2026-10-12 11:57:03.123] [ERROR] [tid=12345] (log.cpp:88) message
//   WARN/ERROR 额外带源码位置；DEBUG/INFO 不带，避免正常日志被位置信息淹没。

#include <sstream>
#include <string>

namespace oj {

enum class LogLevel : int { kDebug = 0, kInfo = 1, kWarn = 2, kError = 3 };

// 大小写不敏感地解析 "DEBUG" / "INFO" / "WARN"("WARNING") / "ERROR"。
// 无法识别时返回 false，且不修改 *out。
bool ParseLogLevel(const std::string& text, LogLevel* out);
const char* LogLevelName(LogLevel level);

void SetLogLevel(LogLevel level);
LogLevel GetLogLevel();
bool ShouldLog(LogLevel level);

// 线程安全的实际写出。通常不直接调用，用下面的宏。
void LogWrite(LogLevel level, const char* file, int line, const std::string& message);

// 流式日志对象：析构时把累积的内容一次性写出。
// 之所以按「一个完整表达式」为单位而不是每次 << 都写，是为了让并发日志不交错。
class LogMessage {
 public:
  LogMessage(LogLevel level, const char* file, int line)
      : level_(level), file_(file), line_(line) {}
  ~LogMessage() { LogWrite(level_, file_, line_, stream_.str()); }

  LogMessage(const LogMessage&) = delete;
  LogMessage& operator=(const LogMessage&) = delete;

  std::ostringstream& stream() { return stream_; }

 private:
  LogLevel level_;
  const char* file_;
  int line_;
  std::ostringstream stream_;
};

}  // namespace oj

#define OJ_LOG_DEBUG ::oj::LogMessage(::oj::LogLevel::kDebug, __FILE__, __LINE__).stream()
#define OJ_LOG_INFO ::oj::LogMessage(::oj::LogLevel::kInfo, __FILE__, __LINE__).stream()
#define OJ_LOG_WARN ::oj::LogMessage(::oj::LogLevel::kWarn, __FILE__, __LINE__).stream()
#define OJ_LOG_ERROR ::oj::LogMessage(::oj::LogLevel::kError, __FILE__, __LINE__).stream()
