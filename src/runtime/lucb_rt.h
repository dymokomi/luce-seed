/* Runtime for the C backend. Linked into every lucb build. base.md §7. */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__)
#define LB_NORETURN __attribute__((noreturn))
#else
#define LB_NORETURN
#endif

LB_NORETURN void lb_trap(const char* message);

int64_t lb_add_s(int64_t a, int64_t b, int bits);
uint64_t lb_add_u(uint64_t a, uint64_t b, int bits);
int64_t lb_sub_s(int64_t a, int64_t b, int bits);
uint64_t lb_sub_u(uint64_t a, uint64_t b, int bits);
int64_t lb_mul_s(int64_t a, int64_t b, int bits);
uint64_t lb_mul_u(uint64_t a, uint64_t b, int bits);
int64_t lb_div_s(int64_t a, int64_t b, int bits);
uint64_t lb_div_u(uint64_t a, uint64_t b, int bits);
int64_t lb_mod_s(int64_t a, int64_t b, int bits);
uint64_t lb_mod_u(uint64_t a, uint64_t b, int bits);
int64_t lb_neg_s(int64_t a, int bits);

int64_t lb_addw_s(int64_t a, int64_t b, int bits);
uint64_t lb_addw_u(uint64_t a, uint64_t b, int bits);
int64_t lb_subw_s(int64_t a, int64_t b, int bits);
uint64_t lb_subw_u(uint64_t a, uint64_t b, int bits);
int64_t lb_mulw_s(int64_t a, int64_t b, int bits);
uint64_t lb_mulw_u(uint64_t a, uint64_t b, int bits);
int64_t lb_negw_s(int64_t a, int bits);
uint64_t lb_negw_u(uint64_t a, int bits);

int64_t lb_adds_s(int64_t a, int64_t b, int bits);
uint64_t lb_adds_u(uint64_t a, uint64_t b, int bits);
int64_t lb_subs_s(int64_t a, int64_t b, int bits);
uint64_t lb_subs_u(uint64_t a, uint64_t b, int bits);
int64_t lb_muls_s(int64_t a, int64_t b, int bits);
uint64_t lb_muls_u(uint64_t a, uint64_t b, int bits);

int64_t lb_shl_s(int64_t a, uint64_t n, int bits);
uint64_t lb_shl_u(uint64_t a, uint64_t n, int bits);
int64_t lb_shr_s(int64_t a, uint64_t n, int bits);
uint64_t lb_shr_u(uint64_t a, uint64_t n, int bits);
uint64_t lb_not_u(uint64_t a, int bits);

/* mode 0 = checked T(x), mode 1 = C cast (truncate / bit-reinterpret). */
int64_t lb_conv_s(int64_t a, int from_bits, int from_signed, int to_bits, int to_signed, int mode);
uint64_t lb_conv_u(uint64_t a, int from_bits, int from_signed, int to_bits, int to_signed, int mode);

int64_t lb_f_to_s(double a, int bits, int mode);
uint64_t lb_f_to_u(double a, int bits, int mode);
double lb_to_f(int64_t a, int from_signed);
float lb_f64_to_f32(double a);

typedef struct lb_str {
    const char* data;
    size_t length;
} lb_str;

typedef struct lb_span {
    void* data;
    size_t length;
} lb_span;

typedef struct lb_cspan {
    const void* data;
    size_t length;
} lb_cspan;

void lb_print_i64(int64_t value);
void lb_print_u64(uint64_t value);
void lb_print_bool(bool value);
void lb_print_str(lb_str value);
void lb_print_f64(double value);
void lb_check_index(uint64_t i, uint64_t n);
void lb_check_utf8(const char* s, size_t n);

typedef struct lb_error {
    int32_t code;
    lb_str message;
} lb_error;

#define LB_OPT(T, name)                                                                            \
    typedef struct name {                                                                          \
        T value;                                                                                   \
        bool present;                                                                              \
    } name

#define LB_RES(T, name)                                                                            \
    typedef struct name {                                                                          \
        T value;                                                                                   \
        lb_error error;                                                                            \
        bool failed;                                                                               \
    } name

typedef struct lb_r_unit {
    lb_error error;
    bool failed;
} lb_r_unit;

int lb_qadd_s(int64_t a, int64_t b, int bits, int64_t* out);
int lb_qadd_u(uint64_t a, uint64_t b, int bits, uint64_t* out);
int lb_qsub_s(int64_t a, int64_t b, int bits, int64_t* out);
int lb_qsub_u(uint64_t a, uint64_t b, int bits, uint64_t* out);
int lb_qmul_s(int64_t a, int64_t b, int bits, int64_t* out);
int lb_qmul_u(uint64_t a, uint64_t b, int bits, uint64_t* out);

/* Compatibility with the scalar-core helpers. */
int64_t lb_add_i64(int64_t a, int64_t b);
int64_t lb_sub_i64(int64_t a, int64_t b);
int64_t lb_mul_i64(int64_t a, int64_t b);
int64_t lb_div_i64(int64_t a, int64_t b);
int64_t lb_mod_i64(int64_t a, int64_t b);
int64_t lb_neg_i64(int64_t a);
