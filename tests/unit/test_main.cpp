#include <iostream>

#include "test_util.h"

namespace ojtest {
int g_checks = 0;
int g_failures = 0;
}  // namespace ojtest

void RunLogTests();
void RunConfigTests();

int main() {
  std::cout << "== 单元测试 ==" << std::endl;

  std::cout << "-- util/log --" << std::endl;
  RunLogTests();

  std::cout << "-- config --" << std::endl;
  RunConfigTests();

  std::cout << "== 断言 " << ojtest::g_checks << " 条，失败 " << ojtest::g_failures << " 条 =="
            << std::endl;
  if (ojtest::g_failures != 0) {
    std::cout << "[FAIL] 单元测试未通过" << std::endl;
    return 1;
  }
  std::cout << "[OK] 单元测试全部通过" << std::endl;
  return 0;
}
