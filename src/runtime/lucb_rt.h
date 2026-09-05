/* Runtime for the C backend. Linked into every lucb build. base.md §7. */

#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__)
#define LB_NORETURN __attribute__((noreturn))
#else
#define LB_NORETURN
#endif

LB_NORETURN void lb_trap(const char* message);
void lb_pause(void);

typedef struct lb_Mutex {
    _Atomic uint32_t state;
} lb_Mutex;

typedef struct lb_Cond {
    _Atomic uint32_t seq;
} lb_Cond;

typedef struct lb_Once {
    _Atomic uint32_t state;
} lb_Once;

typedef struct lb_Sem {
    _Atomic uint32_t count;
} lb_Sem;

void lb_mutex_lock(lb_Mutex* m);
void lb_mutex_unlock(lb_Mutex* m);
bool lb_mutex_try(lb_Mutex* m);
void lb_cond_wait(lb_Cond* c, lb_Mutex* m);
void lb_cond_signal(lb_Cond* c);
void lb_cond_broadcast(lb_Cond* c);
int lb_once_begin(lb_Once* o);
void lb_once_end(lb_Once* o);
void lb_once_wait(lb_Once* o);
void lb_sem_acquire(lb_Sem* s);
void lb_sem_release(lb_Sem* s);

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

typedef struct lb_iface {
    void* data;
    const void* vtable;
} lb_iface;

typedef struct lb_Location {
    lb_str file;
    uint32_t line;
    lb_str function;
} lb_Location;

typedef struct lb_fmtbuf {
    char* data;
    size_t cap;
    size_t used;
} lb_fmtbuf;

int lb_fmtbuf_put(lb_fmtbuf* b, const char* s, size_t n);
int lb_fmtbuf_i64(lb_fmtbuf* b, int64_t v);
int lb_fmtbuf_u64(lb_fmtbuf* b, uint64_t v);
int lb_fmtbuf_f64(lb_fmtbuf* b, double v);
int lb_fmtbuf_bool(lb_fmtbuf* b, bool v);
lb_str lb_fmtbuf_finish(lb_fmtbuf* b);

typedef struct lb_alloc {
    void* ctx;
    int kind; /* 0 heap, 1 fixed buffer, -1 unset */
} lb_alloc;

typedef struct lb_fixed {
    uint8_t* data;
    size_t cap;
    size_t used;
} lb_fixed;

typedef struct lb_span_opt {
    lb_span value;
    bool present;
} lb_span_opt;

typedef struct lb_vt_Allocator {
    lb_span_opt (*allocate)(void* self, size_t size, size_t alignment);
    bool (*resize)(void* self, lb_span block, size_t size);
    void (*release)(void* self, lb_span block);
} lb_vt_Allocator;

lb_alloc lb_heap_raw(void);
lb_alloc lb_fixed_raw(lb_fixed* f);
lb_span lb_alloc_bytes(lb_alloc a, size_t size, size_t align);
lb_span lb_resize_bytes(lb_alloc a, lb_span block, size_t size);
void lb_release_bytes(lb_alloc a, lb_span block);

lb_iface lb_heap_alloc(void);
lb_iface lb_fixed_alloc(lb_fixed* f);
lb_iface lb_get_alloc(void);
void lb_set_alloc(lb_iface a);
lb_span_opt lb_alloc_call(lb_iface a, size_t size, size_t alignment);
bool lb_resize_call(lb_iface a, lb_span block, size_t size);
void lb_release_call(lb_iface a, lb_span block);

int lb_utf8_ok(const char* s, size_t n);
int lb_files_list(lb_iface a, const char* path, lb_span* out);
int lb_process_run(const char* program, const char* const* args, size_t nargs, int32_t* status);

uint64_t lb_hash_seed(void);
uint64_t lb_hash_mix(uint64_t h, uint64_t x);
uint64_t lb_hash_bytes(uint64_t h, const void* p, size_t n);
lb_str lb_show_hex(uint64_t v);
lb_str lb_show_bin(uint64_t v);
lb_str lb_show_pad(lb_str inner, size_t width);
int lb_fmtbuf_hex(lb_fmtbuf* b, uint64_t v);
int lb_fmtbuf_bin(lb_fmtbuf* b, uint64_t v);

#define LB_MEMORY_EXHAUSTED 1
#define LB_FILES_MISSING 2
#define LB_INVALID_UTF8 3

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
