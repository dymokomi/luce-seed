/* Direct helper calls cover widths that well-typed Base programs cannot emit. */
#include "lucb_rt.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const int empty_widths[] = {INT_MIN, -1, 0};
    for (size_t i = 0; i < sizeof(empty_widths) / sizeof(empty_widths[0]); ++i) {
        int bits = empty_widths[i];
        assert(smin(bits) == 0 && smax(bits) == 0);
        assert(sext(INT64_MIN, bits) == 0 && sext(INT64_MAX, bits) == 0);
    }
    for (int bits = 1; bits < 64; ++bits) {
        int64_t magnitude = (int64_t)(UINT64_C(1) << (bits - 1));
        assert(smin(bits) == -magnitude && smax(bits) == magnitude - 1);
        assert(sext(-1, bits) == -1 && sext(0, bits) == 0);
        assert(sext(magnitude, bits) == -magnitude);
        assert(sext(magnitude - 1, bits) == magnitude - 1);
    }
    const int full_widths[] = {64, 65, INT_MAX};
    for (size_t i = 0; i < sizeof(full_widths) / sizeof(full_widths[0]); ++i) {
        int bits = full_widths[i];
        assert(smin(bits) == INT64_MIN && smax(bits) == INT64_MAX);
        assert(sext(INT64_MIN, bits) == INT64_MIN);
        assert(sext(INT64_MAX, bits) == INT64_MAX);
    }
    char *bytes = malloc(3);
    assert(bytes != NULL);
    memcpy(bytes, "abc", 3);
    assert(lb_cstr_of((lb_str){bytes, 3}) == bytes);
    assert(lb_cstr_of((lb_str){bytes + 3, 0}) == bytes + 3);
    assert(lb_cstr_of((lb_str){NULL, 0}) == NULL);
    free(bytes);
    return 0;
}
