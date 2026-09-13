//==============================================================================================
//
//   tests/harness - Test runner entry
//
//   DESCRIPTION:
//       Runs every registered test and reports the tally.
//
//==============================================================================================

#include "support/test.h"
#include <cstdlib>
#include <cstring>

int lucb::test::run_all() {
    int failed_tests = 0;
    size_t selected = 0;
    const char* filter = std::getenv("LUCB_TEST_FILTER");
    const std::vector<Case>& cases = registry();
    std::fprintf(stdout, "running %zu tests\n", cases.size());
    std::fflush(stdout);
    for (const Case& test : cases) {
        if (filter && !std::strstr(test.name, filter)) continue;
        ++selected;
        const int before = failures();
        test.fn();
        if (failures() > before) {
            std::fprintf(stderr, "FAIL  %s\n", test.name);
            failed_tests++;
        } else {
            std::fprintf(stdout, "ok    %s\n", test.name);
        }
    }
    if (failed_tests == 0) {
        std::fprintf(stdout, "%zu passed\n", selected);
        return 0;
    }
    std::fprintf(stderr, "%d failed, %zu passed\n", failed_tests, selected - failed_tests);
    return 1;
}

int main() {
    return lucb::test::run_all();
}
