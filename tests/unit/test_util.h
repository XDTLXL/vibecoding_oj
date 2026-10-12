#pragma once

// 极简测试脚手架（零依赖）
//
// 不用 gtest 是刻意的：项目原则是不引入额外依赖（SPEC.md §6 F 类、§4 D15），
// 而单元测试只需要"断言 + 计数 + 退出码"这三件事。
//
// 用法：
//   OJ_CHECK(expression);
//   OJ_CHECK_EQ(actual, expected);
// 断言失败不中断执行（继续跑完看全貌），最终由 main 依据失败计数返回退出码。

#include <iostream>
#include <string>

namespace ojtest {

extern int g_checks;
extern int g_failures;

inline void ReportCheck(const char* file, int line, const std::string& expression, bool ok) {
  ++g_checks;
  if (ok) return;
  ++g_failures;
  std::cout << "  [FAIL] " << file << ':' << line << "  " << expression << std::endl;
}

template <typename A, typename B>
void ReportEq(const char* file, int line, const char* expr_a, const char* expr_b, const A& a,
              const B& b) {
  ++g_checks;
  if (a == b) return;
  ++g_failures;
  std::cout << "  [FAIL] " << file << ':' << line << "  " << expr_a << " == " << expr_b
            << "\n         实际: " << a << " != " << b << std::endl;
}

}  // namespace ojtest

#define OJ_CHECK(expression) ::ojtest::ReportCheck(__FILE__, __LINE__, #expression, (expression))

#define OJ_CHECK_EQ(actual, expected) \
  ::ojtest::ReportEq(__FILE__, __LINE__, #actual, #expected, (actual), (expected))
