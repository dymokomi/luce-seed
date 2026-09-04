/* Runtime for the scalar-core C backend. Linked into every lucb build. */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#if defined(__GNUC__)
#define LB_NORETURN __attribute__((noreturn))
#else
#define LB_NORETURN
#endif

LB_NORETURN void lb_trap(const char* message);

int64_t lb_add_i64(int64_t a, int64_t b);
int64_t lb_sub_i64(int64_t a, int64_t b);
int64_t lb_mul_i64(int64_t a, int64_t b);
int64_t lb_div_i64(int64_t a, int64_t b);
int64_t lb_mod_i64(int64_t a, int64_t b);
int64_t lb_neg_i64(int64_t a);

void lb_print_i64(int64_t value);
void lb_print_bool(bool value);
void lb_print_str(const char* value);
