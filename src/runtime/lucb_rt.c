#include "lucb_rt.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

void lb_trap(const char* message) {
    fprintf(stderr, "trap: %s\n", message != NULL ? message : "");
    exit(1);
}

int64_t lb_add_i64(int64_t a, int64_t b) {
    int64_t r;
    if (__builtin_add_overflow(a, b, &r)) {
        lb_trap("integer overflow");
    }
    return r;
}

int64_t lb_sub_i64(int64_t a, int64_t b) {
    int64_t r;
    if (__builtin_sub_overflow(a, b, &r)) {
        lb_trap("integer overflow");
    }
    return r;
}

int64_t lb_mul_i64(int64_t a, int64_t b) {
    int64_t r;
    if (__builtin_mul_overflow(a, b, &r)) {
        lb_trap("integer overflow");
    }
    return r;
}

int64_t lb_div_i64(int64_t a, int64_t b) {
    if (b == 0) {
        lb_trap("division by zero");
    }
    if (a == INT64_MIN && b == -1) {
        lb_trap("integer overflow");
    }
    return a / b;
}

int64_t lb_mod_i64(int64_t a, int64_t b) {
    if (b == 0) {
        lb_trap("division by zero");
    }
    if (a == INT64_MIN && b == -1) {
        lb_trap("integer overflow");
    }
    return a % b;
}

int64_t lb_neg_i64(int64_t a) {
    if (a == INT64_MIN) {
        lb_trap("integer overflow");
    }
    return -a;
}

void lb_print_i64(int64_t value) {
    printf("%" PRId64 "\n", value);
}

void lb_print_bool(bool value) {
    puts(value ? "true" : "false");
}

void lb_print_str(const char* value) {
    puts(value != NULL ? value : "");
}
