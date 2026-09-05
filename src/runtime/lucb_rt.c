#include "lucb_rt.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Thread_local lb_alloc lb_current_alloc = {NULL, -1};

lb_alloc lb_heap_alloc(void) {
    lb_alloc a;
    a.ctx = NULL;
    a.kind = 0;
    return a;
}

lb_alloc lb_fixed_alloc(lb_fixed* f) {
    lb_alloc a;
    a.ctx = f;
    a.kind = 1;
    return a;
}

lb_alloc lb_get_alloc(void) {
    if (lb_current_alloc.kind < 0) {
        lb_current_alloc = lb_heap_alloc();
    }
    return lb_current_alloc;
}

void lb_set_alloc(lb_alloc a) { lb_current_alloc = a; }

int lb_fmtbuf_put(lb_fmtbuf* b, const char* s, size_t n) {
    if (b == NULL) {
        return 1;
    }
    if (n > b->cap || b->used > b->cap - n) {
        return 1;
    }
    if (s != NULL && n > 0) {
        memcpy(b->data + b->used, s, n);
    }
    b->used += n;
    return 0;
}

int lb_fmtbuf_i64(lb_fmtbuf* b, int64_t v) {
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), "%" PRId64, v);
    if (n < 0) {
        return 1;
    }
    return lb_fmtbuf_put(b, tmp, (size_t)n);
}

int lb_fmtbuf_u64(lb_fmtbuf* b, uint64_t v) {
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), "%" PRIu64, v);
    if (n < 0) {
        return 1;
    }
    return lb_fmtbuf_put(b, tmp, (size_t)n);
}

int lb_fmtbuf_f64(lb_fmtbuf* b, double v) {
    char tmp[64];
    int n = snprintf(tmp, sizeof(tmp), "%g", v);
    if (n < 0) {
        return 1;
    }
    return lb_fmtbuf_put(b, tmp, (size_t)n);
}

int lb_fmtbuf_bool(lb_fmtbuf* b, bool v) {
    const char* s = v ? "true" : "false";
    return lb_fmtbuf_put(b, s, v ? 4 : 5);
}

lb_str lb_fmtbuf_finish(lb_fmtbuf* b) {
    lb_str s;
    s.data = "";
    s.length = 0;
    if (b == NULL) {
        return s;
    }
    if (b->used < b->cap) {
        b->data[b->used] = 0;
    }
    s.data = b->data;
    s.length = b->used;
    return s;
}

static size_t align_up(size_t n, size_t a) {
    if (a <= 1) {
        return n;
    }
    return (n + (a - 1)) & ~(a - 1);
}

static size_t clamp_align(size_t a) {
    if (a < sizeof(void*)) {
        a = sizeof(void*);
    }
    size_t p = sizeof(void*);
    while (p < a) {
        if (p > ((size_t)-1) / 2) {
            return p;
        }
        p *= 2;
    }
    return p;
}

lb_span lb_alloc_bytes(lb_alloc a, size_t size, size_t align) {
    lb_span s;
    s.data = NULL;
    s.length = 0;
    if (size == 0) {
        s.length = 0;
        s.data = (void*)8;
        return s;
    }
    if (a.kind < 0) {
        lb_trap("memory.unset");
    }
    if (a.kind == 0) {
        align = clamp_align(align);
        void* p = NULL;
        if (posix_memalign(&p, align, size) != 0) {
            return s;
        }
        s.data = p;
        s.length = size;
        return s;
    }
    if (a.kind == 1 && a.ctx != NULL) {
        lb_fixed* f = (lb_fixed*)a.ctx;
        size_t start = align_up(f->used, align < 1 ? 1 : align);
        if (start + size > f->cap) {
            return s;
        }
        f->used = start + size;
        s.data = f->data + start;
        s.length = size;
        return s;
    }
    return s;
}

void lb_release_bytes(lb_alloc a, lb_span block) {
    if (block.data == NULL || block.length == 0) {
        return;
    }
    if (a.kind == 0) {
        free(block.data);
    }
}

void lb_trap(const char* message) {
    fprintf(stderr, "trap: %s\n", message != NULL ? message : "");
    exit(1);
}

void lb_check_utf8(const char* s, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        size_t w = 1;
        if (c < 0x80) {
            w = 1;
        } else if ((c & 0xE0) == 0xC0) {
            w = 2;
        } else if ((c & 0xF0) == 0xE0) {
            w = 3;
        } else if ((c & 0xF8) == 0xF0) {
            w = 4;
        } else {
            lb_trap("invalid_utf8");
        }
        if (i + w > n) {
            lb_trap("invalid_utf8");
        }
        for (size_t k = 1; k < w; k++) {
            if (((unsigned char)s[i + k] & 0xC0) != 0x80) {
                lb_trap("invalid_utf8");
            }
        }
        i += w;
    }
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
