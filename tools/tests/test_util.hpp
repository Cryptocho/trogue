#pragma once
// test_util.hpp —— 无窗口单测的轻量断言与临时场景文件助手。
//
// 设计：不引入第三方测试框架；CHECK 宏失败时打印并累计全局失败数，
// main 返回非零即 ctest 判败。临时场景文件写在 CWD（= CMAKE_SOURCE_DIR，
// 由 add_test 的 WORKING_DIRECTORY 保证），用后即删，支持并发安全（pid+序号）。

#include <cstdio>
#include <string>

namespace tg_test {

inline int g_failures = 0;
inline int g_checks = 0;

// 失败打印 + 计数（断言主体在调用处以显式 if++ 表达，宏只做登记）
inline void record_failure(const char* file, int line, const std::string& msg) {
    std::fprintf(stderr, "[FAIL] %s:%d: %s\n", file, line, msg.c_str());
    ++g_failures;
    ++g_checks;
}
inline void record_ok() { ++g_checks; }

}  // namespace tg_test

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (cond) {                                                      \
            ::tg_test::record_ok();                                      \
        } else {                                                         \
            ::tg_test::record_failure(__FILE__, __LINE__, #cond);        \
        }                                                                \
    } while (0)

// 终止当前测试函数（继续会崩溃/无意义）；仅在确实无法继续时使用。
#define REQUIRE(cond)                                                    \
    do {                                                                 \
        if (!(cond)) {                                                   \
            ::tg_test::record_failure(__FILE__, __LINE__, "REQUIRE " #cond); \
            return false;                                                \
        }                                                                \
    } while (0)
