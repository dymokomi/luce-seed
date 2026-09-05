#include "lucb_rt.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

void lb_trap(const char* message) {
    fprintf(stderr, "trap: %s\n", message != NULL ? message : "");
    exit(1);
}

static uint64_t mask_bits(int bits) {
    if (bits >= 64) {
        return ~(uint64_t)0;
    }
    if (bits <= 0) {
        return 0;
    }
    return ((uint64_t)1 << bits) - 1;
}

static int64_t smin(int bits) {
    if (bits >= 64) {
        return INT64_MIN;
    }
    return -((int64_t)1 << (bits - 1));
}

static int64_t smax(int bits) {
    if (bits >= 64) {
        return INT64_MAX;
    }
    return ((int64_t)1 << (bits - 1)) - 1;
}

static int64_t sext(int64_t a, int bits) {
    if (bits >= 64) {
        return a;
    }
    uint64_t u = (uint64_t)a & mask_bits(bits);
    if (u & ((uint64_t)1 << (bits - 1))) {
        return (int64_t)(u | ~mask_bits(bits));
    }
    return (int64_t)u;
}

static uint64_t zext(uint64_t a, int bits) {
    return a & mask_bits(bits);
}

int64_t lb_add_s(int64_t a, int64_t b, int bits) {
    a = sext(a, bits);
    b = sext(b, bits);
    int64_t r;
    if (__builtin_add_overflow(a, b, &r) || r < smin(bits) || r > smax(bits)) {
        lb_trap("integer overflow");
    }
    return r;
}

uint64_t lb_add_u(uint64_t a, uint64_t b, int bits) {
    a = zext(a, bits);
    b = zext(b, bits);
    if (bits >= 64) {
        if (a > UINT64_MAX - b) {
            lb_trap("integer overflow");
        }
        return a + b;
    }
    uint64_t r = a + b;
    if (r > mask_bits(bits)) {
        lb_trap("integer overflow");
    }
    return r;
}

int64_t lb_sub_s(int64_t a, int64_t b, int bits) {
    a = sext(a, bits);
    b = sext(b, bits);
    int64_t r;
    if (__builtin_sub_overflow(a, b, &r) || r < smin(bits) || r > smax(bits)) {
        lb_trap("integer overflow");
    }
    return r;
}

uint64_t lb_sub_u(uint64_t a, uint64_t b, int bits) {
    a = zext(a, bits);
    b = zext(b, bits);
    if (a < b) {
        lb_trap("integer overflow");
    }
    return a - b;
}

int64_t lb_mul_s(int64_t a, int64_t b, int bits) {
    a = sext(a, bits);
    b = sext(b, bits);
    int64_t r;
    if (__builtin_mul_overflow(a, b, &r) || r < smin(bits) || r > smax(bits)) {
        lb_trap("integer overflow");
    }
    return r;
}

uint64_t lb_mul_u(uint64_t a, uint64_t b, int bits) {
    a = zext(a, bits);
    b = zext(b, bits);
    if (b != 0 && a > mask_bits(bits) / b) {
        lb_trap("integer overflow");
    }
    return zext(a * b, bits);
}

int64_t lb_div_s(int64_t a, int64_t b, int bits) {
    a = sext(a, bits);
    b = sext(b, bits);
    if (b == 0) {
        lb_trap("division by zero");
    }
    if (a == smin(bits) && b == -1) {
        lb_trap("integer overflow");
    }
    return a / b;
}

uint64_t lb_div_u(uint64_t a, uint64_t b, int bits) {
    a = zext(a, bits);
    b = zext(b, bits);
    if (b == 0) {
        lb_trap("division by zero");
    }
    return a / b;
}

int64_t lb_mod_s(int64_t a, int64_t b, int bits) {
    a = sext(a, bits);
    b = sext(b, bits);
    if (b == 0) {
        lb_trap("division by zero");
    }
    if (a == smin(bits) && b == -1) {
        lb_trap("integer overflow");
    }
    return a % b;
}

uint64_t lb_mod_u(uint64_t a, uint64_t b, int bits) {
    a = zext(a, bits);
    b = zext(b, bits);
    if (b == 0) {
        lb_trap("division by zero");
    }
    return a % b;
}

int lb_qadd_s(int64_t a, int64_t b, int bits, int64_t* out) {
    a = sext(a, bits);
    b = sext(b, bits);
    int64_t r;
    if (__builtin_add_overflow(a, b, &r) || r < smin(bits) || r > smax(bits)) {
        return 0;
    }
    *out = r;
    return 1;
}

int lb_qadd_u(uint64_t a, uint64_t b, int bits, uint64_t* out) {
    a = zext(a, bits);
    b = zext(b, bits);
    if (bits >= 64) {
        if (a > UINT64_MAX - b) {
            return 0;
        }
        *out = a + b;
        return 1;
    }
    uint64_t r = a + b;
    if (r > mask_bits(bits)) {
        return 0;
    }
    *out = r;
    return 1;
}

int lb_qsub_s(int64_t a, int64_t b, int bits, int64_t* out) {
    a = sext(a, bits);
    b = sext(b, bits);
    int64_t r;
    if (__builtin_sub_overflow(a, b, &r) || r < smin(bits) || r > smax(bits)) {
        return 0;
    }
    *out = r;
    return 1;
}

int lb_qsub_u(uint64_t a, uint64_t b, int bits, uint64_t* out) {
    a = zext(a, bits);
    b = zext(b, bits);
    if (a < b) {
        return 0;
    }
    *out = a - b;
    return 1;
}

int lb_qmul_s(int64_t a, int64_t b, int bits, int64_t* out) {
    a = sext(a, bits);
    b = sext(b, bits);
    int64_t r;
    if (__builtin_mul_overflow(a, b, &r) || r < smin(bits) || r > smax(bits)) {
        return 0;
    }
    *out = r;
    return 1;
}

int lb_qmul_u(uint64_t a, uint64_t b, int bits, uint64_t* out) {
    a = zext(a, bits);
    b = zext(b, bits);
    if (b != 0 && a > mask_bits(bits) / b) {
        return 0;
    }
    *out = zext(a * b, bits);
    return 1;
}

int64_t lb_neg_s(int64_t a, int bits) {
    a = sext(a, bits);
    if (a == smin(bits)) {
        lb_trap("integer overflow");
    }
    return -a;
}

int64_t lb_addw_s(int64_t a, int64_t b, int bits) {
    uint64_t r = (uint64_t)sext(a, bits) + (uint64_t)sext(b, bits);
    return sext((int64_t)(r & mask_bits(bits)), bits);
}

uint64_t lb_addw_u(uint64_t a, uint64_t b, int bits) {
    return zext(a + b, bits);
}

int64_t lb_subw_s(int64_t a, int64_t b, int bits) {
    uint64_t r = (uint64_t)sext(a, bits) - (uint64_t)sext(b, bits);
    return sext((int64_t)(r & mask_bits(bits)), bits);
}

uint64_t lb_subw_u(uint64_t a, uint64_t b, int bits) {
    return zext(a - b, bits);
}

int64_t lb_mulw_s(int64_t a, int64_t b, int bits) {
    uint64_t r = (uint64_t)sext(a, bits) * (uint64_t)sext(b, bits);
    return sext((int64_t)(r & mask_bits(bits)), bits);
}

uint64_t lb_mulw_u(uint64_t a, uint64_t b, int bits) {
    return zext(a * b, bits);
}

int64_t lb_negw_s(int64_t a, int bits) {
    uint64_t r = 0u - (uint64_t)sext(a, bits);
    return sext((int64_t)(r & mask_bits(bits)), bits);
}

uint64_t lb_negw_u(uint64_t a, int bits) {
    return zext(0u - zext(a, bits), bits);
}

int64_t lb_adds_s(int64_t a, int64_t b, int bits) {
    a = sext(a, bits);
    b = sext(b, bits);
    int64_t r;
    if (__builtin_add_overflow(a, b, &r) || r < smin(bits) || r > smax(bits)) {
        return (a < 0) ? smin(bits) : smax(bits);
    }
    return r;
}

uint64_t lb_adds_u(uint64_t a, uint64_t b, int bits) {
    a = zext(a, bits);
    b = zext(b, bits);
    if (bits >= 64) {
        if (a > UINT64_MAX - b) {
            return UINT64_MAX;
        }
        return a + b;
    }
    uint64_t r = a + b;
    if (r > mask_bits(bits)) {
        return mask_bits(bits);
    }
    return r;
}

int64_t lb_subs_s(int64_t a, int64_t b, int bits) {
    a = sext(a, bits);
    b = sext(b, bits);
    int64_t r;
    if (__builtin_sub_overflow(a, b, &r) || r < smin(bits) || r > smax(bits)) {
        return (a < 0) ? smin(bits) : smax(bits);
    }
    return r;
}

uint64_t lb_subs_u(uint64_t a, uint64_t b, int bits) {
    a = zext(a, bits);
    b = zext(b, bits);
    if (a < b) {
        return 0;
    }
    return a - b;
}

int64_t lb_muls_s(int64_t a, int64_t b, int bits) {
    a = sext(a, bits);
    b = sext(b, bits);
    int64_t r;
    if (__builtin_mul_overflow(a, b, &r) || r < smin(bits) || r > smax(bits)) {
        return ((a < 0) != (b < 0)) ? smin(bits) : smax(bits);
    }
    return r;
}

uint64_t lb_muls_u(uint64_t a, uint64_t b, int bits) {
    a = zext(a, bits);
    b = zext(b, bits);
    if (b != 0 && a > mask_bits(bits) / b) {
        return mask_bits(bits);
    }
    return zext(a * b, bits);
}

int64_t lb_shl_s(int64_t a, uint64_t n, int bits) {
    a = sext(a, bits);
    if (n >= (uint64_t)bits) {
        lb_trap("shift count out of range");
    }
    return sext((int64_t)(((uint64_t)a << n) & mask_bits(bits)), bits);
}

uint64_t lb_shl_u(uint64_t a, uint64_t n, int bits) {
    a = zext(a, bits);
    if (n >= (uint64_t)bits) {
        lb_trap("shift count out of range");
    }
    return zext(a << n, bits);
}

int64_t lb_shr_s(int64_t a, uint64_t n, int bits) {
    a = sext(a, bits);
    if (n >= (uint64_t)bits) {
        lb_trap("shift count out of range");
    }
    return a >> n;
}

uint64_t lb_shr_u(uint64_t a, uint64_t n, int bits) {
    a = zext(a, bits);
    if (n >= (uint64_t)bits) {
        lb_trap("shift count out of range");
    }
    return a >> n;
}

uint64_t lb_not_u(uint64_t a, int bits) {
    return zext(~a, bits);
}

static int fits_s(int64_t v, int bits) {
    return v >= smin(bits) && v <= smax(bits);
}

static int fits_u(uint64_t v, int bits) {
    return v <= mask_bits(bits);
}

int64_t lb_conv_s(int64_t a, int from_bits, int from_signed, int to_bits, int to_signed, int mode) {
    if (from_signed) {
        a = sext(a, from_bits);
    } else {
        a = (int64_t)zext((uint64_t)a, from_bits);
    }
    if (mode == 0) {
        if (to_signed) {
            if (!fits_s(a, to_bits)) {
                lb_trap("integer conversion out of range");
            }
            return a;
        }
        if (a < 0 || !fits_u((uint64_t)a, to_bits)) {
            lb_trap("integer conversion out of range");
        }
        return a;
    }
    uint64_t bits = (uint64_t)a & mask_bits(to_bits);
    if (to_signed) {
        return sext((int64_t)bits, to_bits);
    }
    return (int64_t)bits;
}

uint64_t lb_conv_u(uint64_t a, int from_bits, int from_signed, int to_bits, int to_signed, int mode) {
    int64_t s = from_signed ? sext((int64_t)a, from_bits) : (int64_t)zext(a, from_bits);
    if (mode == 0) {
        if (to_signed) {
            if (!fits_s(s, to_bits)) {
                lb_trap("integer conversion out of range");
            }
            return (uint64_t)s;
        }
        if (s < 0 || !fits_u((uint64_t)s, to_bits)) {
            lb_trap("integer conversion out of range");
        }
        return (uint64_t)s;
    }
    return zext((uint64_t)s, to_bits);
}

int64_t lb_f_to_s(double a, int bits, int mode) {
    if (mode == 0) {
        if (!isfinite(a) || a < (double)smin(bits) || a > (double)smax(bits)) {
            lb_trap("integer conversion out of range");
        }
        return (int64_t)a;
    }
    if (!isfinite(a)) {
        return 0;
    }
    if (a <= (double)smin(bits)) {
        return smin(bits);
    }
    if (a >= (double)smax(bits)) {
        return smax(bits);
    }
    return (int64_t)a;
}

uint64_t lb_f_to_u(double a, int bits, int mode) {
    double hi = (double)mask_bits(bits);
    if (mode == 0) {
        if (!isfinite(a) || a < 0 || a > hi) {
            lb_trap("integer conversion out of range");
        }
        return (uint64_t)a;
    }
    if (!isfinite(a) || a < 0) {
        return 0;
    }
    if (a >= hi) {
        return mask_bits(bits);
    }
    return (uint64_t)a;
}

double lb_to_f(int64_t a, int from_signed) {
    if (from_signed) {
        return (double)a;
    }
    return (double)(uint64_t)a;
}

float lb_f64_to_f32(double a) {
    return (float)a;
}

void lb_print_i64(int64_t value) {
    printf("%" PRId64 "\n", value);
}

void lb_print_u64(uint64_t value) {
    printf("%" PRIu64 "\n", value);
}

void lb_print_bool(bool value) {
    puts(value ? "true" : "false");
}

void lb_print_str(lb_str value) {
    printf("%.*s\n", (int)value.length, value.data != NULL ? value.data : "");
}

void lb_check_index(uint64_t i, uint64_t n) {
    if (i >= n) {
        lb_trap("index out of bounds");
    }
}

void lb_print_f64(double value) {
    printf("%g\n", value);
}

int64_t lb_add_i64(int64_t a, int64_t b) {
    return lb_add_s(a, b, 64);
}
int64_t lb_sub_i64(int64_t a, int64_t b) {
    return lb_sub_s(a, b, 64);
}
int64_t lb_mul_i64(int64_t a, int64_t b) {
    return lb_mul_s(a, b, 64);
}
int64_t lb_div_i64(int64_t a, int64_t b) {
    return lb_div_s(a, b, 64);
}
int64_t lb_mod_i64(int64_t a, int64_t b) {
    return lb_mod_s(a, b, 64);
}
int64_t lb_neg_i64(int64_t a) {
    return lb_neg_s(a, 64);
}
