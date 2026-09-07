//==============================================================================================
//
//   runtime/lucb_rt - The C runtime linked into every Base program
//
//   DESCRIPTION:
//       Traps with their reason, the wrapping and saturating arithmetic families,
//       conversions, the heap and fixed-buffer allocators behind `memory`, formatted display
//       of scalars, UTF-8 validation, hashing, `files`, `process`, threads over pthreads, and
//       the `sync` primitives over atomic wait/wake. Base has no runtime of its own (base.md
//       §1.3); this is the startup shim, trap reporter, and standard modules the seed
//       supplies.
//
//==============================================================================================

#include "lucb_rt.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static _Thread_local lb_iface lb_current_alloc = {NULL, NULL};

lb_alloc lb_heap_raw(void) {
    lb_alloc a;
    a.ctx = NULL;
    a.kind = 0;
    return a;
}

lb_alloc lb_fixed_raw(lb_fixed* f) {
    lb_alloc a;
    a.ctx = f;
    a.kind = 1;
    return a;
}

static lb_span_opt lb_heap_allocate(void* self, size_t size, size_t alignment) {
    (void)self;
    lb_span s = lb_alloc_bytes(lb_heap_raw(), size, alignment);
    lb_span_opt o;
    if (size != 0 && s.data == NULL) {
        o.present = false;
        o.value.data = NULL;
        o.value.length = 0;
        return o;
    }
    o.present = true;
    o.value = s;
    return o;
}

static bool lb_heap_resize(void* self, lb_span block, size_t size) {
    (void)self;
    return size <= block.length;
}

static void lb_heap_release(void* self, lb_span block) {
    (void)self;
    lb_release_bytes(lb_heap_raw(), block);
}

static const lb_vt_Allocator lb_vt_heap = {
    .allocate = lb_heap_allocate,
    .resize = lb_heap_resize,
    .release = lb_heap_release,
};

static lb_span_opt lb_fixed_allocate(void* self, size_t size, size_t alignment) {
    lb_span s = lb_alloc_bytes(lb_fixed_raw((lb_fixed*)self), size, alignment);
    lb_span_opt o;
    if (size != 0 && s.data == NULL) {
        o.present = false;
        o.value.data = NULL;
        o.value.length = 0;
        return o;
    }
    o.present = true;
    o.value = s;
    return o;
}

static bool lb_fixed_resize(void* self, lb_span block, size_t size) {
    lb_span n = lb_resize_bytes(lb_fixed_raw((lb_fixed*)self), block, size);
    return n.data != NULL || size == 0;
}

static void lb_fixed_release(void* self, lb_span block) {
    lb_release_bytes(lb_fixed_raw((lb_fixed*)self), block);
}

static const lb_vt_Allocator lb_vt_fixed = {
    .allocate = lb_fixed_allocate,
    .resize = lb_fixed_resize,
    .release = lb_fixed_release,
};

lb_iface lb_heap_alloc(void) {
    lb_iface a;
    a.data = NULL;
    a.vtable = &lb_vt_heap;
    return a;
}

lb_iface lb_fixed_alloc(lb_fixed* f) {
    lb_iface a;
    a.data = f;
    a.vtable = &lb_vt_fixed;
    return a;
}

lb_iface lb_get_alloc(void) {
    if (lb_current_alloc.vtable == NULL) {
        lb_current_alloc = lb_heap_alloc();
    }
    return lb_current_alloc;
}

void lb_set_alloc(lb_iface a) {
    lb_current_alloc = a;
}

lb_span_opt lb_alloc_call(lb_iface a, size_t size, size_t alignment) {
    if (a.vtable == NULL) {
        lb_trap("memory.unset");
    }
    return ((const lb_vt_Allocator*)a.vtable)->allocate(a.data, size, alignment);
}

bool lb_resize_call(lb_iface a, lb_span block, size_t size) {
    if (a.vtable == NULL) {
        lb_trap("memory.unset");
    }
    return ((const lb_vt_Allocator*)a.vtable)->resize(a.data, block, size);
}

void lb_release_call(lb_iface a, lb_span block) {
    if (a.vtable == NULL) {
        return;
    }
    ((const lb_vt_Allocator*)a.vtable)->release(a.data, block);
}

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

size_t lb_utf8_encode(uint32_t cp, char out[4]) {
    size_t n = 0;
    if (cp < 0x80) {
        out[n++] = (char)cp;
    } else if (cp < 0x800) {
        out[n++] = (char)(0xC0 | (cp >> 6));
        out[n++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out[n++] = (char)(0xE0 | (cp >> 12));
        out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[n++] = (char)(0x80 | (cp & 0x3F));
    } else {
        out[n++] = (char)(0xF0 | (cp >> 18));
        out[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[n++] = (char)(0x80 | (cp & 0x3F));
    }
    return n;
}

int lb_fmtbuf_char(lb_fmtbuf* b, uint32_t cp) {
    char out[4];
    return lb_fmtbuf_put(b, out, lb_utf8_encode(cp, out));
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

lb_span lb_resize_bytes(lb_alloc a, lb_span block, size_t size) {
    lb_span s;
    s.data = NULL;
    s.length = 0;
    if (size == block.length) {
        return block;
    }
    if (a.kind < 0) {
        lb_trap("memory.unset");
    }
    if (a.kind == 0) {
        if (size == 0) {
            lb_release_bytes(a, block);
            s.data = (void*)8;
            s.length = 0;
            return s;
        }
        void* p = NULL;
        if (posix_memalign(&p, clamp_align(sizeof(void*)), size) != 0) {
            return s;
        }
        size_t n = size < block.length ? size : block.length;
        if (n > 0 && block.data != NULL && block.length > 0) {
            memcpy(p, block.data, n);
        }
        if (block.length > 0 && block.data != NULL) {
            free(block.data);
        }
        s.data = p;
        s.length = size;
        return s;
    }
    if (a.kind == 1 && a.ctx != NULL) {
        lb_fixed* f = (lb_fixed*)a.ctx;
        uint8_t* start = (uint8_t*)block.data;
        if (f->data != NULL && start >= f->data && start + block.length == f->data + f->used) {
            if (start + size < start) {
                return s;
            }
            if (start + size <= f->data + f->cap) {
                f->used = (size_t)(start - f->data) + size;
                s.data = start;
                s.length = size;
                return s;
            }
        }
        return s;
    }
    return s;
}

void lb_trap(const char* message) {
    fprintf(stderr, "trap: %s\n", message != NULL ? message : "");
    exit(1);
}

void lb_pause(void) {
#if defined(__aarch64__)
    __asm__ volatile("yield");
#elif defined(__x86_64__)
    __builtin_ia32_pause();
#else
    (void)0;
#endif
}

void lb_mutex_lock(lb_Mutex* m) {
    if (m == NULL) {
        lb_trap("null pointer");
    }
    for (;;) {
        uint32_t expected = 0;
        if (atomic_compare_exchange_strong(&m->state, &expected, 1u)) {
            return;
        }
        while (atomic_load(&m->state) != 0) {
            lb_pause();
        }
    }
}

void lb_mutex_unlock(lb_Mutex* m) {
    if (m == NULL) {
        lb_trap("null pointer");
    }
    atomic_store(&m->state, 0);
}

bool lb_mutex_try(lb_Mutex* m) {
    if (m == NULL) {
        lb_trap("null pointer");
    }
    uint32_t expected = 0;
    return atomic_compare_exchange_strong(&m->state, &expected, 1u);
}

void lb_cond_wait(lb_Cond* c, lb_Mutex* m) {
    if (c == NULL || m == NULL) {
        lb_trap("null pointer");
    }
    uint32_t s = atomic_load(&c->seq);
    lb_mutex_unlock(m);
    while (atomic_load(&c->seq) == s) {
        lb_pause();
    }
    lb_mutex_lock(m);
}

void lb_cond_signal(lb_Cond* c) {
    if (c == NULL) {
        lb_trap("null pointer");
    }
    atomic_fetch_add(&c->seq, 1u);
}

void lb_cond_broadcast(lb_Cond* c) {
    lb_cond_signal(c);
}

int lb_once_begin(lb_Once* o) {
    if (o == NULL) {
        lb_trap("null pointer");
    }
    uint32_t expected = 0;
    if (atomic_compare_exchange_strong(&o->state, &expected, 1u)) {
        return 1;
    }
    return 0;
}

void lb_once_end(lb_Once* o) {
    if (o == NULL) {
        lb_trap("null pointer");
    }
    atomic_store(&o->state, 2u);
}

void lb_once_wait(lb_Once* o) {
    if (o == NULL) {
        lb_trap("null pointer");
    }
    while (atomic_load(&o->state) != 2u) {
        lb_pause();
    }
}

void lb_sem_acquire(lb_Sem* s) {
    if (s == NULL) {
        lb_trap("null pointer");
    }
    for (;;) {
        uint32_t n = atomic_load(&s->count);
        if (n > 0 && atomic_compare_exchange_weak(&s->count, &n, n - 1u)) {
            return;
        }
        while (atomic_load(&s->count) == 0) {
            lb_pause();
        }
    }
}

void lb_sem_release(lb_Sem* s) {
    if (s == NULL) {
        lb_trap("null pointer");
    }
    atomic_fetch_add(&s->count, 1u);
}

int64_t* lb_null_probe(void) {
    return NULL;
}

int lb_str_compare(lb_str a, lb_str b) {
    size_t n = a.length < b.length ? a.length : b.length;
    int c = n == 0 ? 0 : memcmp(a.data, b.data, n);
    if (c != 0) {
        return c < 0 ? -1 : 1;
    }
    if (a.length < b.length) {
        return -1;
    }
    return a.length > b.length ? 1 : 0;
}

int lb_utf8_ok(const char* s, size_t n) {
    size_t i = 0;
    if (s == NULL) {
        return n == 0;
    }
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        size_t w = 1;
        uint32_t cp = 0;
        if (c < 0x80) {
            w = 1;
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            w = 2;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0) == 0xE0) {
            w = 3;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8) == 0xF0) {
            w = 4;
            cp = c & 0x07u;
        } else {
            return 0;
        }
        if (i + w > n) {
            return 0;
        }
        for (size_t k = 1; k < w; k++) {
            unsigned char x = (unsigned char)s[i + k];
            if ((x & 0xC0) != 0x80) {
                return 0;
            }
            cp = (cp << 6) | (uint32_t)(x & 0x3Fu);
        }
        if (w == 2 && cp < 0x80) {
            return 0;
        }
        if (w == 3 && cp < 0x800) {
            return 0;
        }
        if (w == 4 && cp < 0x10000) {
            return 0;
        }
        if (cp >= 0xD800 && cp <= 0xDFFF) {
            return 0;
        }
        if (cp > 0x10FFFFu) {
            return 0;
        }
        i += w;
    }
    return 1;
}

uint32_t lb_utf8_scalar(const char* s, size_t n, size_t i, size_t* width) {
    const unsigned char* b = (const unsigned char*)s;
    unsigned char c = b[i];
    size_t w = 1;
    uint32_t cp = c;
    if (c >= 0xF0) {
        w = 4;
        cp = c & 0x07u;
    } else if (c >= 0xE0) {
        w = 3;
        cp = c & 0x0Fu;
    } else if (c >= 0xC0) {
        w = 2;
        cp = c & 0x1Fu;
    }
    if (i + w > n) {
        w = n - i;
    }
    for (size_t k = 1; k < w; k++) {
        cp = (cp << 6) | (b[i + k] & 0x3Fu);
    }
    *width = w;
    return cp;
}

void lb_check_utf8(const char* s, size_t n) {
    if (!lb_utf8_ok(s, n)) {
        lb_trap("invalid_utf8");
    }
}

typedef struct lb_name_item {
    char* name;
    size_t len;
} lb_name_item;

static int lb_name_cmp(const void* a, const void* b) {
    const lb_name_item* x = (const lb_name_item*)a;
    const lb_name_item* y = (const lb_name_item*)b;
    size_t n = x->len < y->len ? x->len : y->len;
    int c = memcmp(x->name, y->name, n);
    if (c != 0) {
        return c;
    }
    if (x->len < y->len) {
        return -1;
    }
    if (x->len > y->len) {
        return 1;
    }
    return 0;
}

int lb_files_list(lb_iface a, const char* path, lb_span* out) {
    if (out == NULL) {
        return 1;
    }
    out->data = (void*)8;
    out->length = 0;
    if (path == NULL) {
        return 2;
    }
    DIR* dir = opendir(path);
    if (dir == NULL) {
        return 2;
    }
    lb_name_item* items = NULL;
    size_t n = 0;
    size_t cap = 0;
    size_t bytes = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        size_t len = strlen(ent->d_name);
        if (n == cap) {
            size_t next = cap == 0 ? 8 : cap * 2;
            lb_name_item* grown = (lb_name_item*)realloc(items, next * sizeof(lb_name_item));
            if (grown == NULL) {
                for (size_t i = 0; i < n; i++) {
                    free(items[i].name);
                }
                free(items);
                closedir(dir);
                return 1;
            }
            items = grown;
            cap = next;
        }
        char* copy = (char*)malloc(len + 1);
        if (copy == NULL) {
            for (size_t i = 0; i < n; i++) {
                free(items[i].name);
            }
            free(items);
            closedir(dir);
            return 1;
        }
        memcpy(copy, ent->d_name, len + 1);
        items[n].name = copy;
        items[n].len = len;
        n++;
        bytes += len + 1;
    }
    closedir(dir);
    if (n > 1) {
        qsort(items, n, sizeof(lb_name_item), lb_name_cmp);
    }
    if (n == 0) {
        free(items);
        return 0;
    }
    size_t need = n * sizeof(lb_str) + bytes;
    lb_span_opt got = lb_alloc_call(a, need, sizeof(void*));
    lb_span block = got.value;
    if (!got.present) {
        for (size_t i = 0; i < n; i++) {
            free(items[i].name);
        }
        free(items);
        return 1;
    }
    lb_str* names = (lb_str*)block.data;
    char* store = (char*)(names + n);
    for (size_t i = 0; i < n; i++) {
        names[i].data = store;
        names[i].length = items[i].len;
        memcpy(store, items[i].name, items[i].len);
        store[items[i].len] = 0;
        store += items[i].len + 1;
        free(items[i].name);
    }
    free(items);
    out->data = names;
    out->length = n;
    return 0;
}

static int lb_store_captured(lb_iface a, const char* src, size_t n, lb_str* out) {
    if (out == NULL) {
        return 0;
    }
    if (n == 0 || src == NULL) {
        out->data = "";
        out->length = 0;
        return 0;
    }
    lb_span_opt got = lb_alloc_call(a, n + 1, 1);
    if (!got.present || got.value.data == NULL) {
        return 1;
    }
    memcpy(got.value.data, src, n);
    ((char*)got.value.data)[n] = 0;
    out->data = (const char*)got.value.data;
    out->length = n;
    return 0;
}

static int lb_read_more(int fd, char** buf, size_t* used, size_t* cap, int* open) {
    if (*cap - *used < 64) {
        size_t next = *cap == 0 ? 256 : *cap * 2;
        char* nbuf = (char*)realloc(*buf, next);
        if (nbuf == NULL) {
            return 1;
        }
        *buf = nbuf;
        *cap = next;
    }
    ssize_t r = read(fd, *buf + *used, *cap - *used);
    if (r == 0) {
        *open = 0;
        return 0;
    }
    if (r < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return 1;
    }
    *used += (size_t)r;
    return 0;
}

int lb_process_run(const char* program, const char* const* args, size_t nargs, lb_iface alloc,
                   int32_t* status, lb_str* out, lb_str* err) {
    if (program == NULL) {
        return 1;
    }
    int outp[2];
    int errp[2];
    if (pipe(outp) != 0) {
        return 1;
    }
    if (pipe(errp) != 0) {
        close(outp[0]);
        close(outp[1]);
        return 1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(outp[0]);
        close(outp[1]);
        close(errp[0]);
        close(errp[1]);
        return 1;
    }
    if (pid == 0) {
        close(outp[0]);
        close(errp[0]);
        if (dup2(outp[1], 1) < 0 || dup2(errp[1], 2) < 0) {
            _exit(127);
        }
        close(outp[1]);
        close(errp[1]);
        const char** argv = (const char**)malloc((nargs + 2) * sizeof(char*));
        if (argv == NULL) {
            _exit(127);
        }
        argv[0] = program;
        for (size_t i = 0; i < nargs; i++) {
            argv[i + 1] = args != NULL ? args[i] : "";
        }
        argv[nargs + 1] = NULL;
        execvp(program, (char* const*)argv);
        _exit(127);
    }
    close(outp[1]);
    close(errp[1]);
    fcntl(outp[0], F_SETFL, O_NONBLOCK);
    fcntl(errp[0], F_SETFL, O_NONBLOCK);
    char* obuf = NULL;
    char* ebuf = NULL;
    size_t oused = 0;
    size_t ocap = 0;
    size_t eused = 0;
    size_t ecap = 0;
    int oopen = 1;
    int eopen = 1;
    int fail = 0;
    while ((oopen || eopen) && !fail) {
        struct pollfd fds[2];
        fds[0].fd = oopen ? outp[0] : -1;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = eopen ? errp[0] : -1;
        fds[1].events = POLLIN;
        fds[1].revents = 0;
        int pr = poll(fds, 2, -1);
        if (pr < 0) {
            if (errno == EINTR) {
                continue;
            }
            fail = 1;
            break;
        }
        if (oopen && (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            if (lb_read_more(outp[0], &obuf, &oused, &ocap, &oopen) != 0) {
                fail = 1;
            }
        }
        if (eopen && (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            if (lb_read_more(errp[0], &ebuf, &eused, &ecap, &eopen) != 0) {
                fail = 1;
            }
        }
    }
    close(outp[0]);
    close(errp[0]);
    int st = 0;
    if (waitpid(pid, &st, 0) < 0 || !WIFEXITED(st)) {
        fail = 1;
    }
    if (!fail) {
        if (status != NULL) {
            *status = (int32_t)WEXITSTATUS(st);
        }
        if (lb_store_captured(alloc, obuf, oused, out) != 0 ||
            lb_store_captured(alloc, ebuf, eused, err) != 0) {
            fail = 1;
        }
    }
    free(obuf);
    free(ebuf);
    return fail;
}

static uint64_t lb_seed;
static int lb_seed_set;

uint64_t lb_hash_seed(void) {
    if (!lb_seed_set) {
        lb_seed = 0x9E3779B97F4A7C15ULL ^ (uint64_t)(uintptr_t)&lb_seed;
        lb_seed_set = 1;
    }
    return lb_seed;
}

uint64_t lb_hash_mix(uint64_t h, uint64_t x) {
    h ^= x;
    h *= 0x9E3779B97F4A7C15ULL;
    h ^= h >> 32;
    return h;
}

uint64_t lb_hash_bytes(uint64_t h, const void* p, size_t n) {
    const unsigned char* b = (const unsigned char*)p;
    for (size_t i = 0; i < n; i++) {
        h = lb_hash_mix(h, b[i]);
    }
    h = lb_hash_mix(h, (uint64_t)n);
    return h;
}

#define LB_SHOW_SLOTS 4
#define LB_SHOW_CAP 256

static _Thread_local char lb_show_buf[LB_SHOW_SLOTS][LB_SHOW_CAP];
static _Thread_local int lb_show_i;

static lb_str lb_show_take(const char* s, size_t n) {
    int slot = lb_show_i++ & (LB_SHOW_SLOTS - 1);
    if (n >= LB_SHOW_CAP) {
        n = LB_SHOW_CAP - 1;
    }
    if (s != NULL && n > 0) {
        memcpy(lb_show_buf[slot], s, n);
    }
    lb_show_buf[slot][n] = 0;
    lb_str r;
    r.data = lb_show_buf[slot];
    r.length = n;
    return r;
}

lb_str lb_show_hex(uint64_t v) {
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), "%" PRIx64, v);
    if (n < 0) {
        n = 0;
    }
    return lb_show_take(tmp, (size_t)n);
}

lb_str lb_show_bin(uint64_t v) {
    char tmp[65];
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        char rev[64];
        int m = 0;
        while (v != 0 && m < 64) {
            rev[m++] = (char)('0' + (v & 1u));
            v >>= 1;
        }
        while (m > 0) {
            tmp[n++] = rev[--m];
        }
    }
    tmp[n] = 0;
    return lb_show_take(tmp, (size_t)n);
}

lb_str lb_show_pad(lb_str inner, size_t width) {
    size_t n = inner.length;
    const char* s = inner.data != NULL ? inner.data : "";
    if (n >= width) {
        return lb_show_take(s, n);
    }
    char tmp[LB_SHOW_CAP];
    size_t pad = width - n;
    if (width >= LB_SHOW_CAP) {
        pad = LB_SHOW_CAP - 1 - (n < LB_SHOW_CAP - 1 ? n : LB_SHOW_CAP - 1);
        if (n > LB_SHOW_CAP - 1) {
            n = LB_SHOW_CAP - 1;
            pad = 0;
        }
    }
    for (size_t i = 0; i < pad; i++) {
        tmp[i] = ' ';
    }
    if (n > 0) {
        memcpy(tmp + pad, s, n);
    }
    return lb_show_take(tmp, pad + n);
}

int lb_fmtbuf_hex(lb_fmtbuf* b, uint64_t v) {
    lb_str s = lb_show_hex(v);
    return lb_fmtbuf_put(b, s.data, s.length);
}

int lb_fmtbuf_bin(lb_fmtbuf* b, uint64_t v) {
    lb_str s = lb_show_bin(v);
    return lb_fmtbuf_put(b, s.data, s.length);
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

uint64_t lb_conv_u(uint64_t a, int from_bits, int from_signed, int to_bits, int to_signed,
                   int mode) {
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
