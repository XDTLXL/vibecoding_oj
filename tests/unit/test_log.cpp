#include <string>

#include "test_util.h"
#include "util/log.h"

namespace {

void TestParseLogLevel() {
  oj::LogLevel level = oj::LogLevel::kError;

  OJ_CHECK(oj::ParseLogLevel("DEBUG", &level));
  OJ_CHECK(level == oj::LogLevel::kDebug);

  // 大小写不敏感：配置文件里写 debug 也应当被接受
  OJ_CHECK(oj::ParseLogLevel("debug", &level));
  OJ_CHECK(level == oj::LogLevel::kDebug);

  OJ_CHECK(oj::ParseLogLevel("Info", &level));
  OJ_CHECK(level == oj::LogLevel::kInfo);
  OJ_CHECK(oj::ParseLogLevel("WARN", &level));
  OJ_CHECK(level == oj::LogLevel::kWarn);
  OJ_CHECK(oj::ParseLogLevel("WARNING", &level));
  OJ_CHECK(level == oj::LogLevel::kWarn);
  OJ_CHECK(oj::ParseLogLevel("ERROR", &level));
  OJ_CHECK(level == oj::LogLevel::kError);

  // 非法输入必须返回 false，且不能把 *out 改成半吊子值
  level = oj::LogLevel::kInfo;
  OJ_CHECK(!oj::ParseLogLevel("", &level));
  OJ_CHECK(!oj::ParseLogLevel("VERBOSE", &level));
  OJ_CHECK(!oj::ParseLogLevel("INFO2", &level));
  OJ_CHECK(level == oj::LogLevel::kInfo);
}

void TestLevelThreshold() {
  const oj::LogLevel original = oj::GetLogLevel();

  oj::SetLogLevel(oj::LogLevel::kWarn);
  OJ_CHECK(oj::GetLogLevel() == oj::LogLevel::kWarn);
  OJ_CHECK(!oj::ShouldLog(oj::LogLevel::kDebug));
  OJ_CHECK(!oj::ShouldLog(oj::LogLevel::kInfo));
  OJ_CHECK(oj::ShouldLog(oj::LogLevel::kWarn));
  OJ_CHECK(oj::ShouldLog(oj::LogLevel::kError));

  // 被级别过滤掉的日志既不输出也不崩
  oj::LogWrite(oj::LogLevel::kDebug, __FILE__, __LINE__, "此行不应出现在输出里");

  oj::SetLogLevel(oj::LogLevel::kDebug);
  OJ_CHECK(oj::ShouldLog(oj::LogLevel::kDebug));

  oj::SetLogLevel(original);
}

void TestLevelName() {
  OJ_CHECK_EQ(std::string(oj::LogLevelName(oj::LogLevel::kDebug)), std::string("DEBUG"));
  OJ_CHECK_EQ(std::string(oj::LogLevelName(oj::LogLevel::kInfo)), std::string("INFO"));
  OJ_CHECK_EQ(std::string(oj::LogLevelName(oj::LogLevel::kWarn)), std::string("WARN"));
  OJ_CHECK_EQ(std::string(oj::LogLevelName(oj::LogLevel::kError)), std::string("ERROR"));
}

}  // namespace

void RunLogTests() {
  TestParseLogLevel();
  TestLevelThreshold();
  TestLevelName();
}
