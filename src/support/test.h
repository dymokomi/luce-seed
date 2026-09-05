//==============================================================================================
//
//   support/test - The test harness
//
//   DESCRIPTION:
//       `TEST`, `CHECK`, `CHECK_EQ`, and `CHECK_STREQ`; tests register themselves and
//       `run_all` runs them. No third-party framework.
//
//==============================================================================================

#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace lucb {
namespace test {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int count = 0;
    return count;
}

struct Register {
    Register(const char* name, void (*fn)()) {
        registry().push_back(Case{name, fn});
    }
};

int run_all();

} // namespace test
} // namespace lucb

#define TEST(name)                                                                                 \
    static void test_##name();                                                                     \
    static ::lucb::test::Register register_##name(#name, test_##name);                             \
    static void test_##name()

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "  %s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);        \
            ::lucb::test::failures()++;                                                            \
        }                                                                                          \
    } while (0)

#define CHECK_EQ(a, b)                                                                             \
    do {                                                                                           \
        auto _va = (a);                                                                            \
        auto _vb = (b);                                                                            \
        if (!(_va == _vb)) {                                                                       \
            std::fprintf(stderr, "  %s:%d: CHECK_EQ failed: %s == %s\n", __FILE__, __LINE__, #a,   \
                         #b);                                                                      \
            ::lucb::test::failures()++;                                                            \
        }                                                                                          \
    } while (0)

#define CHECK_STREQ(a, b)                                                                          \
    do {                                                                                           \
        std::string _sa = (a);                                                                     \
        std::string _sb = (b);                                                                     \
        if (_sa != _sb) {                                                                          \
            std::fprintf(stderr, "  %s:%d: CHECK_STREQ failed:\n    left:  %s\n    right: %s\n",   \
                         __FILE__, __LINE__, _sa.c_str(), _sb.c_str());                            \
            ::lucb::test::failures()++;                                                            \
        }                                                                                          \
    } while (0)
