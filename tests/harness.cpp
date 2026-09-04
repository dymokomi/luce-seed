#include "support/test.h"

int lucb::test::run_all() {
    int failed_tests = 0;
    const std::vector<Case>& cases = registry();
    std::fprintf(stdout, "running %zu tests\n", cases.size());
    for (const Case& test : cases) {
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
        std::fprintf(stdout, "%zu passed\n", cases.size());
        return 0;
    }
    std::fprintf(stderr, "%d failed, %zu passed\n", failed_tests, cases.size() - failed_tests);
    return 1;
}

int main() { return lucb::test::run_all(); }
