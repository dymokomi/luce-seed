//==============================================================================================
//
//   interp/call - Calls, methods, externs, and the standard modules in the oracle
//
//   DESCRIPTION:
//       Evaluates every call form: user functions and methods through `call_func`, function
//       values, constructors, and the standard modules (`memory`, `io`, `files`, `process`,
//       `thread`, `sync`, `atomic`, `luce`) modelled directly. Extern calls cover the libc
//       subset the agree tests use so the oracle can match the binary; anything else is
//       refused by name.
//
//==============================================================================================

#include "interp/interp_impl.h"

#include "support/literal.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace lucb {

// A fallible result already failed, for a callee the oracle cannot run.
static Value fail_run(Type* ty) {
    Value e;
    e.failed = true;
    e.kind = TypeKind::Fallible;
    e.type = ty;
    e.err_code = 1;
    e.err_msg = "run";
    return e;
}

static bool read_pipe_chunk(int fd, string* buf, bool* open) {
    char tmp[256];
    ssize_t r = read(fd, tmp, sizeof(tmp));
    if (r == 0) {
        *open = false;
        return true;
    }
    if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return true;
        }
        return false;
    }
    buf->append(tmp, static_cast<size_t>(r));
    return true;
}

// `holder.callback(args)`: a field of function type is a call through its value, not a method.
static bool is_field_call(Node* callee) {
    return callee != nullptr && callee->resolved != nullptr &&
           callee->resolved->kind == NodeKind::Field && is_func(callee->ty);
}

auto Interp::invoke_method(Value* self, Node* st, string_view name, const vector<Value>& args)
    -> Value {
    Node* method = nullptr;
    if (st != nullptr) {
        for (Node* m = st->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::Func && m->text == name) {
                method = m;
                break;
            }
        }
    }
    if (method == nullptr || self == nullptr) {
        fail("no allocator method");
        return v_unit();
    }
    Frame frame;
    Slot ss;
    ss.name = "self";
    ss.value = *self;
    frame.slots.push_back(ss);
    Node* p = method->right;
    size_t i = 0;
    while (p != nullptr && i < args.size()) {
        Slot s;
        s.name = p->text;
        s.value = args[i];
        if (p->ty != nullptr) {
            s.value.type = p->ty;
            s.value.kind = p->ty->kind;
        }
        frame.slots.push_back(s);
        p = p->next;
        i++;
    }
    frames.push_back(frame);
    bool saved_ret = returning;
    returning = false;
    Node* saved_fn = current_fn;
    current_fn = method;
    exec(method->body);
    current_fn = saved_fn;
    Frame& top = frames.back();
    for (size_t k = 0; k < top.slots.size(); k++) {
        if (top.slots[k].name == "self") {
            *self = top.slots[k].value;
            break;
        }
    }
    Value result = returning ? ret : v_unit();
    returning = saved_ret;
    frames.pop_back();
    return result;
}

auto Interp::eval_call(Node* n) -> Value {
    Node* callee = n->left;
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "hash") {
        return eval_hash(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "hex") {
        return eval_hex(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "bin") {
        return eval_bin(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "pad") {
        return eval_pad(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "CAllocator") {
        return heap_alloc_value();
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "discard") {
        if (n->body != nullptr) {
            eval(n->body->left);
        }
        return v_unit();
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "assert") {
        Value c = eval(n->body != nullptr ? n->body->left : nullptr);
        if (trapped) {
            return v_unit();
        }
        if (!c.b) {
            string msg = "assert failed";
            if (n->body != nullptr && n->body->next != nullptr) {
                Value m = eval(n->body->next->left);
                if (m.kind == TypeKind::Str) {
                    msg = decode_string(m.str);
                }
            }
            fail(msg);
        }
        return v_unit();
    }
    if (callee != nullptr && callee->kind == NodeKind::Member && !is_field_call(callee) && callee->left != nullptr &&
        callee->left->kind == NodeKind::Name && callee->left->text == "ErrorCode" &&
        callee->text == "package") {
        Value a = n->body != nullptr ? eval(n->body->left) : v_unit();
        return v_int(n->ty, a.u);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "print") {
        Node* arg = n->body != nullptr ? n->body->left : nullptr;
        if (arg != nullptr && arg->kind == NodeKind::Formatted) {
            Value a = eval_formatted(arg);
            if (!trapped) {
                output += show(a);
                output += '\n';
            }
            return v_unit();
        }
        Value a = eval(arg);
        if (!trapped) {
            output += show(a);
            output += '\n';
        }
        return v_unit();
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "format") {
        return eval_format(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "trap") {
        Value a = eval(n->body != nullptr ? n->body->left : nullptr);
        fail(show(a));
        return v_unit();
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "error") {
        Value code = eval(n->body != nullptr ? n->body->left : nullptr);
        Value msg =
            eval(n->body != nullptr && n->body->next != nullptr ? n->body->next->left : nullptr);
        ret.failed = true;
        ret.kind = TypeKind::Fallible;
        ret.err_code = static_cast<int32_t>(as_s(code, code.type));
        err_storage = msg.kind == TypeKind::Str ? decode_string(msg.str) : show(msg);
        ret.err_msg = err_storage;
        returning = true;
        return v_unit();
    }
    if (callee != nullptr && callee->kind == NodeKind::Name &&
        (callee->text == "sizeof" || callee->text == "alignof")) {
        Node* arg = n->body != nullptr ? n->body->left : nullptr;
        Type* t = arg != nullptr ? arg->ty : nullptr;
        uint64_t v = callee->text == "sizeof" ? static_cast<uint64_t>(type_size(t))
                                              : static_cast<uint64_t>(type_align(t));
        return v_int(n->ty, v);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "offsetof") {
        Node* tyarg = n->body != nullptr ? n->body->left : nullptr;
        Node* field =
            n->body != nullptr && n->body->next != nullptr ? n->body->next->left : nullptr;
        Type* t = tyarg != nullptr ? tyarg->ty : nullptr;
        string_view fname = field != nullptr ? field->text : string_view{};
        return v_int(n->ty, static_cast<uint64_t>(type_offset(t, fname)));
    }
    if (n->resolved != nullptr && n->resolved->kind == NodeKind::EnumCase) {
        Value v = v_enum_case(n->resolved, n->ty);
        for (Node* a = n->body; a != nullptr; a = a->next) {
            v.fields.push_back(eval(a->left));
        }
        return v;
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "str" &&
        n->body != nullptr) {
        Value x = eval(n->body->left);
        if (trapped) {
            return v_unit();
        }
        Type* src = n->body->left != nullptr ? n->body->left->ty : x.type;
        bool checked = n->ty != nullptr && is_fail(n->ty);
        return eval_str_conv(x, src, n->ty, checked);
    }
    if (is_checked_conversion(n)) {
        return eval_conv(n->body->left, n->ty, true);
    }
    if (n->resolved != nullptr && n->resolved->kind == NodeKind::Struct) {
        return eval_ctor(n, n->resolved);
    }
    if (n->resolved != nullptr && n->resolved->kind == NodeKind::Func &&
        n->resolved->text == "init" && callee != nullptr && callee->kind == NodeKind::Name &&
        callee->resolved != nullptr && callee->resolved->kind == NodeKind::Struct) {
        Node* st = callee->resolved;
        Value v = zero_of(st->ty);
        call_func(n->resolved, &v, n->body);
        if (trapped) {
            return v_unit();
        }
        if (returning && ret.failed) {
            returning = false;
            ret.failed = true;
            return ret;
        }
        v.failed = false;
        return v;
    }
    if (callee != nullptr && callee->kind == NodeKind::Member && !is_field_call(callee)) {
        Type* lt = callee->left != nullptr ? callee->left->ty : nullptr;
        if (lt != nullptr && lt->kind == TypeKind::Module && n->resolved != nullptr &&
            n->resolved->kind == NodeKind::Func) {
            if (lt->name == "io" && (callee->text == "stdout" || callee->text == "stderr")) {
                stdio_dummy.u = callee->text == "stdout" ? 1 : 2;
                stdio_dummy.type = n->ty;
                Value v;
                v.kind = TypeKind::Interface;
                v.type = n->ty;
                v.u = stdio_dummy.u;
                v.ptr = &stdio_dummy;
                return v;
            }
            if (lt->name == "c" &&
                (callee->text == "stdin" || callee->text == "stdout" || callee->text == "stderr")) {
                Value v;
                v.kind = TypeKind::Pointer;
                v.type = n->ty;
                v.u = callee->text == "stdin" ? 1 : callee->text == "stdout" ? 2 : 3;
                return v;
            }
            if (lt->name == "memory" && (callee->text == "copy" || callee->text == "move")) {
                Value to = n->body != nullptr ? eval(n->body->left) : v_unit();
                Value from = n->body != nullptr && n->body->next != nullptr
                                 ? eval(n->body->next->left)
                                 : v_unit();
                Value cv =
                    n->body != nullptr && n->body->next != nullptr && n->body->next->next != nullptr
                        ? eval(n->body->next->next->left)
                        : v_unit();
                if (trapped) {
                    return v_unit();
                }
                size_t count = static_cast<size_t>(as_u(cv, cv.type));
                size_t tlen = to.length != 0 ? to.length : to.fields.size();
                size_t flen = from.length != 0 ? from.length : from.fields.size();
                if (to.kind == TypeKind::Array && to.type != nullptr) {
                    tlen = static_cast<size_t>(to.type->length);
                }
                if (from.kind == TypeKind::Array && from.type != nullptr) {
                    flen = static_cast<size_t>(from.type->length);
                }
                if (count > tlen || count > flen) {
                    fail("index out of bounds");
                    return v_unit();
                }
                Value* td = to.ptr != nullptr ? to.ptr : to.fields.data();
                Value* fd = from.ptr != nullptr ? from.ptr : from.fields.data();
                if (td == nullptr || fd == nullptr) {
                    return v_unit();
                }
                if (callee->text == "move") {
                    vector<Value> tmp(fd, fd + static_cast<ptrdiff_t>(count));
                    for (size_t i = 0; i < count; i++) {
                        td[i] = tmp[i];
                    }
                } else {
                    for (size_t i = 0; i < count; i++) {
                        td[i] = fd[i];
                    }
                }
                return v_unit();
            }
            if (lt->name == "memory" && callee->text == "set") {
                Value span = n->body != nullptr ? eval(n->body->left) : v_unit();
                Value byte = n->body != nullptr && n->body->next != nullptr
                                 ? eval(n->body->next->left)
                                 : v_unit();
                if (trapped) {
                    return v_unit();
                }
                size_t nlen = span.length != 0 ? span.length : span.fields.size();
                if (span.kind == TypeKind::Array && span.type != nullptr) {
                    nlen = static_cast<size_t>(span.type->length);
                }
                Value* p = span.ptr != nullptr ? span.ptr : span.fields.data();
                Type* et = span.type != nullptr ? span.type->elem : nullptr;
                for (size_t i = 0; i < nlen && p != nullptr; i++) {
                    p[i] = v_int(et, byte.u);
                }
                return v_unit();
            }
            if (lt->name == "memory" && callee->text == "grow") {
                Value block = n->body != nullptr ? eval(n->body->left) : v_unit();
                Value sv = n->body != nullptr && n->body->next != nullptr
                               ? eval(n->body->next->left)
                               : v_unit();
                if (trapped) {
                    return v_unit();
                }
                size_t size = static_cast<size_t>(as_u(sv, sv.type));
                size_t old = block.length != 0 ? block.length : block.fields.size();
                Value* src = block.ptr != nullptr ? block.ptr : block.fields.data();
                if (current_alloc.u == 1 && current_alloc.ptr != nullptr) {
                    Value* fb = current_alloc.ptr;
                    bool tail = bump_fb == fb && bump_ptr == src;
                    size_t cap = fb->fields.size() >= 2 ? static_cast<size_t>(fb->fields[1].u) : 0;
                    size_t used = fb->fields.size() >= 3 ? static_cast<size_t>(fb->fields[2].u) : 0;
                    if (!tail || used < old) {
                        return fail_exhausted(n->ty);
                    }
                    size_t start = used - old;
                    if (start + size < start || start + size > cap) {
                        return fail_exhausted(n->ty);
                    }
                    fb->fields[2].u = start + size;
                }
                vector<Value> elems;
                elems.resize(size);
                Type* elem = n->ty != nullptr && is_fail(n->ty) && n->ty->elem != nullptr
                                 ? n->ty->elem->elem
                                 : nullptr;
                for (size_t i = 0; i < size; i++) {
                    if (i < old && src != nullptr) {
                        elems[i] = src[i];
                    } else {
                        elems[i] = v_int(elem, 0);
                    }
                }
                Type* sp = n->ty != nullptr && is_fail(n->ty) ? n->ty->elem : n->ty;
                Value grown = make_array(sp, std::move(elems));
                grown.kind = TypeKind::Span;
                grown.type = sp;
                if (current_alloc.u == 1) {
                    bump_fb = current_alloc.ptr;
                    bump_ptr = grown.ptr;
                    bump_len = grown.length;
                }
                return ok_payload(grown, n->ty);
            }
            if (lt->name == "memory" && callee->text == "read") {
                Value addr = n->body != nullptr ? eval(n->body->left) : v_unit();
                if (trapped) {
                    return v_unit();
                }
                Type* t = n->type != nullptr ? n->type->ty : n->ty;
                if (addr.ptr != nullptr) {
                    Value v = *addr.ptr;
                    v.type = t;
                    if (t != nullptr) {
                        v.kind = t->kind;
                    }
                    return v;
                }
                return v_zero(t);
            }
            if (lt->name == "memory" && callee->text == "write") {
                Value addr = n->body != nullptr ? eval(n->body->left) : v_unit();
                Value val = n->body != nullptr && n->body->next != nullptr
                                ? eval(n->body->next->left)
                                : v_unit();
                if (trapped) {
                    return v_unit();
                }
                if (addr.ptr != nullptr) {
                    Type* t = n->type != nullptr ? n->type->ty : val.type;
                    val.type = t;
                    if (t != nullptr) {
                        val.kind = t->kind;
                    }
                    *addr.ptr = val;
                }
                return v_unit();
            }
            if (lt->name == "files" && callee->text == "list") {
                Value pv = n->body != nullptr ? eval(n->body->left) : v_unit();
                string path = cstr_text(pv);
                if (path.empty()) {
                    path = decode_string(pv.str);
                }
                DIR* dir = opendir(path.c_str());
                if (dir == nullptr) {
                    Value e;
                    e.failed = true;
                    e.kind = TypeKind::Fallible;
                    e.type = n->ty;
                    e.err_code = 2;
                    e.err_msg = "missing";
                    return e;
                }
                vector<string> names;
                struct dirent* ent;
                while ((ent = readdir(dir)) != nullptr) {
                    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
                        continue;
                    }
                    names.push_back(ent->d_name);
                }
                closedir(dir);
                for (size_t i = 1; i < names.size(); i++) {
                    string key = names[i];
                    size_t j = i;
                    while (j > 0 && names[j - 1] > key) {
                        names[j] = names[j - 1];
                        j--;
                    }
                    names[j] = key;
                }
                vector<Value> elems;
                elems.resize(names.size());
                for (size_t i = 0; i < names.size(); i++) {
                    strings.push_back(names[i]);
                    elems[i] = v_str(strings.back());
                }
                Type* sp = n->ty != nullptr && is_fail(n->ty) ? n->ty->elem : n->ty;
                Value span = make_array(sp, std::move(elems));
                span.kind = TypeKind::Span;
                span.type = sp;
                return ok_payload(span, n->ty);
            }
            if (lt->name == "process" && callee->text == "run") {
                Value pv = n->body != nullptr ? eval(n->body->left) : v_unit();
                Value av = n->body != nullptr && n->body->next != nullptr
                               ? eval(n->body->next->left)
                               : v_unit();
                string prog = cstr_text(pv);
                if (prog.empty()) {
                    prog = decode_string(pv.str);
                }
                size_t nargs = av.length != 0 ? av.length : av.fields.size();
                if (av.kind == TypeKind::Array && av.type != nullptr) {
                    nargs = static_cast<size_t>(av.type->length);
                }
                Value* ap = av.ptr != nullptr ? av.ptr : av.fields.data();
                vector<string> store;
                store.reserve(nargs);
                vector<const char*> argv;
                argv.push_back(prog.c_str());
                for (size_t i = 0; i < nargs; i++) {
                    string a = ap != nullptr ? cstr_text(ap[i]) : string();
                    if (a.empty() && ap != nullptr) {
                        a = decode_string(ap[i].str);
                    }
                    store.push_back(a);
                }
                for (size_t i = 0; i < store.size(); i++) {
                    argv.push_back(store[i].c_str());
                }
                argv.push_back(nullptr);
                int outp[2];
                int errp[2];
                if (pipe(outp) != 0) {
                    return fail_run(n->ty);
                }
                if (pipe(errp) != 0) {
                    close(outp[0]);
                    close(outp[1]);
                    return fail_run(n->ty);
                }
                pid_t pid = fork();
                if (pid < 0) {
                    close(outp[0]);
                    close(outp[1]);
                    close(errp[0]);
                    close(errp[1]);
                    return fail_run(n->ty);
                }
                if (pid == 0) {
                    close(outp[0]);
                    close(errp[0]);
                    if (dup2(outp[1], 1) < 0 || dup2(errp[1], 2) < 0) {
                        _exit(127);
                    }
                    close(outp[1]);
                    close(errp[1]);
                    execvp(prog.c_str(), const_cast<char* const*>(argv.data()));
                    _exit(127);
                }
                close(outp[1]);
                close(errp[1]);
                fcntl(outp[0], F_SETFL, O_NONBLOCK);
                fcntl(errp[0], F_SETFL, O_NONBLOCK);
                string captured_out;
                string captured_err;
                bool oopen = true;
                bool eopen = true;
                bool io_fail = false;
                while ((oopen || eopen) && !io_fail) {
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
                        io_fail = true;
                        break;
                    }
                    if (oopen && (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
                        if (!read_pipe_chunk(outp[0], &captured_out, &oopen)) {
                            io_fail = true;
                        }
                    }
                    if (eopen && (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
                        if (!read_pipe_chunk(errp[0], &captured_err, &eopen)) {
                            io_fail = true;
                        }
                    }
                }
                close(outp[0]);
                close(errp[0]);
                int st = 0;
                if (io_fail || waitpid(pid, &st, 0) < 0 || !WIFEXITED(st)) {
                    return fail_run(n->ty);
                }
                strings.push_back(captured_out);
                Value ov = v_str(strings.back());
                strings.push_back(captured_err);
                Value ev = v_str(strings.back());
                Type* payload = n->ty != nullptr && is_fail(n->ty) ? n->ty->elem : n->ty;
                Type* code_t = is_tup(payload) && payload->ntargs > 0 ? payload->args[0] : nullptr;
                Value tup;
                tup.kind = TypeKind::Tuple;
                tup.type = payload;
                tup.fields.push_back(v_int(code_t, static_cast<uint64_t>(WEXITSTATUS(st))));
                tup.fields.push_back(ov);
                tup.fields.push_back(ev);
                return ok_payload(tup, n->ty);
            }
            if (lt->name == "files" && callee->text == "read") {
                Value pv = n->body != nullptr ? eval(n->body->left) : v_unit();
                string path = decode_string(pv.str);
                FILE* f = fopen(path.c_str(), "rb");
                if (f == nullptr) {
                    Value e;
                    e.failed = true;
                    e.kind = TypeKind::Fallible;
                    e.type = n->ty;
                    e.err_code = 2;
                    e.err_msg = "missing";
                    return e;
                }
                fseek(f, 0, SEEK_END);
                long nbyte = ftell(f);
                rewind(f);
                if (nbyte < 0) {
                    nbyte = 0;
                }
                Type* sp = n->ty != nullptr && is_fail(n->ty) ? n->ty->elem : n->ty;
                Type* elem = sp != nullptr ? sp->elem : nullptr;
                vector<Value> elems;
                if (nbyte > 0) {
                    vector<char> buf(static_cast<size_t>(nbyte));
                    size_t got = fread(buf.data(), 1, static_cast<size_t>(nbyte), f);
                    elems.resize(got);
                    for (size_t i = 0; i < got; i++) {
                        elems[i] = v_int(elem, static_cast<unsigned char>(buf[i]));
                    }
                }
                fclose(f);
                Value span = make_array(sp, std::move(elems));
                span.kind = TypeKind::Span;
                span.type = sp;
                return ok_payload(span, n->ty);
            }
            if (lt->name == "files" && callee->text == "write") {
                Value pv = n->body != nullptr ? eval(n->body->left) : v_unit();
                string path = cstr_text(pv);
                if (path.empty()) {
                    path = decode_string(pv.str);
                }
                Value bv = n->body != nullptr && n->body->next != nullptr
                               ? eval(n->body->next->left)
                               : v_unit();
                FILE* f = fopen(path.c_str(), "wb");
                if (f == nullptr) {
                    Value e;
                    e.failed = true;
                    e.kind = TypeKind::Fallible;
                    e.type = n->ty;
                    e.err_code = 2;
                    e.err_msg = "missing";
                    return e;
                }
                if (bv.kind == TypeKind::Str || bv.kind == TypeKind::Fmt) {
                    bv = as_u8_span(bv);
                }
                string bytes;
                if (bv.kind == TypeKind::Span || bv.kind == TypeKind::Array) {
                    size_t nlen = bv.length != 0 ? bv.length : bv.fields.size();
                    Value* p = bv.ptr != nullptr ? bv.ptr : bv.fields.data();
                    for (size_t i = 0; i < nlen; i++) {
                        bytes.push_back(static_cast<char>(p[i].u));
                    }
                }
                size_t w = bytes.empty() ? 0 : fwrite(bytes.data(), 1, bytes.size(), f);
                fclose(f);
                if (w != bytes.size()) {
                    Value e;
                    e.failed = true;
                    e.kind = TypeKind::Fallible;
                    e.type = n->ty;
                    e.err_code = 1;
                    e.err_msg = "write";
                    return e;
                }
                Value ok;
                ok.failed = false;
                ok.kind = TypeKind::Fallible;
                ok.type = n->ty;
                return ok;
            }
            if (callee->text == "fence" || callee->text == "pause" || callee->text == "yield" ||
                callee->text == "sleep") {
                return v_unit();
            }
            if (callee->text == "current") {
                Value h;
                h.kind = TypeKind::Struct;
                h.type = n->ty;
                Value id;
                id.kind = TypeKind::Usize;
                id.u = 1;
                h.fields.push_back(id);
                return h;
            }
            if (callee->text == "spawn") {
                Node* entry = n->body != nullptr ? n->body->left : nullptr;
                Node* fn = entry != nullptr ? entry->resolved : nullptr;
                Node* ctxn =
                    n->body != nullptr && n->body->next != nullptr ? n->body->next : nullptr;
                if (fn != nullptr) {
                    call_func(fn, nullptr, ctxn);
                }
                Value h;
                h.kind = TypeKind::Struct;
                h.type = n->ty != nullptr && is_fail(n->ty) ? n->ty->elem : n->ty;
                Value id;
                id.kind = TypeKind::Usize;
                id.u = 1;
                h.fields.push_back(id);
                return ok_payload(h, n->ty);
            }
            return call_func(n->resolved, nullptr, n->body);
        }
        Node* method = callee->resolved;
        Type* ot = callee->left != nullptr ? callee->left->ty : nullptr;
        if (is_ptr(ot) && ot->elem != nullptr) {
            ot = ot->elem;
        }
        if (is_atomic(callee->left != nullptr ? callee->left->ty : nullptr) || is_atomic(ot)) {
            Type* at = is_atomic(callee->left->ty) ? callee->left->ty : ot;
            Type* elem = at->elem;
            Value* slot = lvalue(callee->left);
            if (slot == nullptr) {
                fail("atomic needs an lvalue");
                return v_unit();
            }
            Value arg =
                n->body != nullptr && callee->text != "load" ? eval(n->body->left) : v_unit();
            if (callee->text == "load") {
                Value v = *slot;
                v.type = elem;
                v.kind = elem != nullptr ? elem->kind : v.kind;
                return v;
            }
            if (callee->text == "store") {
                arg.type = elem;
                if (elem != nullptr) {
                    arg.kind = elem->kind;
                }
                *slot = arg;
                return v_unit();
            }
            if (callee->text == "wait") {
                return v_unit();
            }
            if (callee->text == "wake") {
                return v_unit();
            }
            if (callee->text == "cas") {
                Value exp = n->body != nullptr ? eval(n->body->left) : v_unit();
                Value des = n->body != nullptr && n->body->next != nullptr
                                ? eval(n->body->next->left)
                                : v_unit();
                bool ok = as_u(*slot, elem) == as_u(exp, elem);
                Value obs = *slot;
                if (ok) {
                    des.type = elem;
                    if (elem != nullptr) {
                        des.kind = elem->kind;
                    }
                    *slot = des;
                }
                Value tup;
                tup.kind = TypeKind::Tuple;
                tup.type = n->ty;
                Value b;
                b.kind = TypeKind::Bool;
                b.b = ok;
                tup.fields.push_back(b);
                obs.type = elem;
                if (elem != nullptr) {
                    obs.kind = elem->kind;
                }
                tup.fields.push_back(obs);
                return tup;
            }
            Value prev = *slot;
            prev.type = elem;
            if (elem != nullptr) {
                prev.kind = elem->kind;
            }
            TokenKind op = TokenKind::PlusPercent;
            if (callee->text == "sub") {
                op = TokenKind::MinusPercent;
            } else if (callee->text == "set") {
                op = TokenKind::Pipe;
            } else if (callee->text == "clear") {
                op = TokenKind::Amp;
                arg.u = ~arg.u;
            } else if (callee->text == "flip") {
                op = TokenKind::Caret;
            } else if (callee->text == "swap") {
                *slot = arg;
                return prev;
            } else if (callee->text == "max" || callee->text == "min") {
                uint64_t a = as_u(*slot, elem);
                uint64_t b = as_u(arg, elem);
                bool take = callee->text == "max" ? a >= b : a <= b;
                if (!take) {
                    arg.type = elem;
                    if (elem != nullptr) {
                        arg.kind = elem->kind;
                    }
                    *slot = arg;
                }
                return prev;
            }
            Value r = arith(elem, *slot, arg, op);
            if (!trapped) {
                *slot = r;
            }
            return prev;
        }
        if (ot != nullptr && ot->kind == TypeKind::Struct && ot->name == "Handle" &&
            callee->text == "join") {
            Value ok;
            ok.kind = TypeKind::Fallible;
            ok.failed = false;
            ok.type = n->ty;
            return ok;
        }
        if (ot != nullptr && ot->kind == TypeKind::Struct && ot->name == "Handle" &&
            callee->text == "detach") {
            return v_unit();
        }
        if (ot != nullptr && ot->kind == TypeKind::Struct &&
            (ot->name == "Mutex" || ot->name == "Condition" || ot->name == "Once" ||
             ot->name == "Semaphore")) {
            Value* slot = lvalue(callee->left);
            if (slot != nullptr && slot->kind == TypeKind::Pointer) {
                slot = slot->ptr;
            }
            if (slot == nullptr) {
                fail("sync needs an lvalue");
                return v_unit();
            }
            if (ot->name == "Mutex" && callee->text == "lock") {
                if (slot->u != 0) {
                    fail("mutex locked");
                    return v_unit();
                }
                slot->u = 1;
                return v_unit();
            }
            if (ot->name == "Mutex" && callee->text == "unlock") {
                slot->u = 0;
                return v_unit();
            }
            if (ot->name == "Mutex" && callee->text == "try") {
                if (slot->u != 0) {
                    return v_bool(false);
                }
                slot->u = 1;
                return v_bool(true);
            }
            if (ot->name == "Condition" && callee->text == "wait") {
                Value mu = n->body != nullptr ? eval(n->body->left) : v_unit();
                if (trapped) {
                    return v_unit();
                }
                if (mu.ptr == nullptr) {
                    fail("null pointer");
                    return v_unit();
                }
                mu.ptr->u = 0;
                mu.ptr->u = 1;
                return v_unit();
            }
            if (ot->name == "Condition" &&
                (callee->text == "signal" || callee->text == "broadcast")) {
                slot->u += 1;
                return v_unit();
            }
            if (ot->name == "Once" && callee->text == "run") {
                if (slot->u == 2) {
                    return v_unit();
                }
                if (slot->u != 0) {
                    fail("once running");
                    return v_unit();
                }
                slot->u = 1;
                Node* entry = n->body != nullptr ? n->body->left : nullptr;
                Node* fn = entry != nullptr ? entry->resolved : nullptr;
                if (fn != nullptr) {
                    call_func(fn, nullptr, nullptr);
                }
                slot->u = 2;
                return v_unit();
            }
            if (ot->name == "Semaphore" && callee->text == "acquire") {
                if (slot->u == 0) {
                    fail("semaphore empty");
                    return v_unit();
                }
                slot->u -= 1;
                return v_unit();
            }
            if (ot->name == "Semaphore" && callee->text == "release") {
                slot->u += 1;
                return v_unit();
            }
            fail("unknown method");
            return v_unit();
        }
        if (ot != nullptr && ot->kind == TypeKind::Interface) {
            Value view = eval(callee->left);
            if (trapped || view.ptr == nullptr) {
                fail("null interface");
                return v_unit();
            }
            if ((view.u == 1 || view.u == 2 || view.u == 3) && callee->text == "write") {
                Value bytes = n->body != nullptr ? eval(n->body->left) : v_unit();
                string s;
                if (bytes.kind == TypeKind::Str || bytes.kind == TypeKind::Fmt) {
                    s = decode_string(bytes.str);
                } else {
                    size_t nlen = bytes.length != 0 ? bytes.length : bytes.fields.size();
                    Value* p = bytes.ptr != nullptr ? bytes.ptr : bytes.fields.data();
                    for (size_t i = 0; i < nlen; i++) {
                        s.push_back(static_cast<char>(p[i].u));
                    }
                }
                size_t nlen = s.size();
                if (view.u == 1) {
                    output += s;
                } else if (view.u == 2) {
                    err += s;
                } else if (!sinks.empty()) {
                    sinks.back()->append(s);
                }
                Value nwritten =
                    v_int(n->ty != nullptr && is_fail(n->ty) ? n->ty->elem : n->ty, nlen);
                return ok_payload(nwritten, n->ty);
            }
            Value* obj = view.ptr;
            Type* ct = obj->type;
            Node* impl = nullptr;
            if (ct != nullptr && ct->decl != nullptr) {
                impl = nullptr;
                for (Node* m = ct->decl->body; m != nullptr; m = m->next) {
                    if (m->kind == NodeKind::Func && m->text == callee->text) {
                        impl = m;
                        break;
                    }
                }
            }
            if (impl == nullptr) {
                fail("unknown method");
                return v_unit();
            }
            return call_func(impl, obj, n->body);
        }
        if (callee->text == "bits" && method == nullptr && callee->left != nullptr) {
            return eval_float_bits(callee, n);
        }
        if (callee->text == "compare" && (method == nullptr || method->kind != NodeKind::Func)) {
            Value L = eval(callee->left);
            Value R = eval(n->body != nullptr ? n->body->left : nullptr);
            if (trapped) {
                return v_unit();
            }
            int64_t cmp = 0;
            Type* lt = callee->left != nullptr ? callee->left->ty : L.type;
            if (is_float(lt) || L.kind == TypeKind::F32 || L.kind == TypeKind::F64) {
                cmp = L.f < R.f ? -1 : L.f > R.f ? 1 : 0;
            } else if (L.kind == TypeKind::Str || (lt != nullptr && lt->kind == TypeKind::Str)) {
                string a = decode_string(L.str);
                string b = decode_string(R.str);
                cmp = a < b ? -1 : a > b ? 1 : 0;
            } else {
                int64_t lv = as_s(L, lt);
                int64_t rv = as_s(
                    R, n->body != nullptr && n->body->left != nullptr ? n->body->left->ty : R.type);
                cmp = lv < rv ? -1 : lv > rv ? 1 : 0;
            }
            return v_i64(cmp);
        }
        if (method == nullptr || method->kind != NodeKind::Func) {
            fail("unknown method");
            return v_unit();
        }
        if ((method->flags & FlagStatic) != 0) {
            Node* owner = lt != nullptr ? lt->decl : nullptr;
            if (method->text == "over" && owner != nullptr && owner->text == "FixedBuffer") {
                Value buf = eval(n->body != nullptr ? n->body->left : nullptr);
                if (trapped) {
                    return v_unit();
                }
                Value fb = zero_of(n->ty);
                fb.kind = TypeKind::Struct;
                fb.type = n->ty;
                fb.fields.clear();
                Value data;
                data.kind = TypeKind::Pointer;
                data.ptr = buf.ptr != nullptr ? buf.ptr : buf.fields.data();
                fb.fields.push_back(data);
                Value cap;
                cap.kind = TypeKind::Usize;
                cap.u = buf.length != 0 ? buf.length : buf.fields.size();
                if (buf.kind == TypeKind::Array && buf.type != nullptr) {
                    cap.u = buf.type->length;
                }
                fb.fields.push_back(cap);
                Value used;
                used.kind = TypeKind::Usize;
                used.u = 0;
                fb.fields.push_back(used);
                return fb;
            }
            return call_func(method, nullptr, n->body);
        }
        // a method on a place takes the place; on a value, `Flags.b.name()` or a call's
        // result, a temporary holds the receiver
        if (is_place_expression(callee->left)) {
            Value* recv = lvalue(callee->left);
            if (recv != nullptr) {
                return call_func(method, recv, n->body);
            }
            if (trapped) {
                return v_unit();
            }
        }
        Value tmp = eval(callee->left);
        return call_func(method, &tmp, n->body);
    }
    if (n->resolved != nullptr && n->resolved->kind == NodeKind::ExternFunc) {
        return eval_extern(n, n->resolved);
    }
    if (n->resolved != nullptr && n->resolved->kind == NodeKind::Func) {
        return call_func(n->resolved, nullptr, n->body);
    }
    if (callee != nullptr && is_func(callee->ty)) {
        Value fv = eval(callee);
        Node* fn = fv.fn;
        if (fn == nullptr) {
            fail("null function");
            return v_unit();
        }
        int listed = 0;
        for (Node* p = fn->right; p != nullptr; p = p->next) {
            listed++;
        }
        int nargs = 0;
        for (Node* a = n->body; a != nullptr; a = a->next) {
            nargs++;
        }
        if (nargs == listed + 1 && (fn->flags & FlagStatic) == 0) {
            Value selfv = n->body != nullptr ? eval(n->body->left) : v_unit();
            Value* self = nullptr;
            if (selfv.kind == TypeKind::Pointer && selfv.ptr != nullptr) {
                self = selfv.ptr;
            } else {
                self = &selfv;
            }
            return call_func(fn, self, n->body != nullptr ? n->body->next : nullptr);
        }
        return call_func(fn, nullptr, n->body);
    }
    fail("unknown call");
    return v_unit();
}

auto Interp::extern_symbol(Node* fn) -> string {
    if (fn == nullptr) {
        return {};
    }
    if (fn->left != nullptr && fn->left->kind == NodeKind::Literal) {
        string s = string(fn->left->text);
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
            return s.substr(1, s.size() - 2);
        }
        return s;
    }
    return string(fn->text);
}

auto Interp::cstr_text(const Value& v) -> string {
    if (v.kind == TypeKind::Str || v.kind == TypeKind::CStr) {
        return decode_string(v.str);
    }
    return {};
}

auto Interp::interp_printf(const string& fmt, const vector<Value>& args) -> string {
    string out;
    size_t ai = 0;
    for (size_t i = 0; i < fmt.size(); i++) {
        if (fmt[i] != '%' || i + 1 >= fmt.size()) {
            out += fmt[i];
            continue;
        }
        i++;
        if (fmt[i] == '%') {
            out += '%';
            continue;
        }
        while (i < fmt.size() && (fmt[i] == 'l' || fmt[i] == 'z' || fmt[i] == 'h')) {
            i++;
        }
        if (i >= fmt.size() || ai >= args.size()) {
            break;
        }
        const Value& a = args[ai++];
        char spec = fmt[i];
        char buf[64];
        if (spec == 'd' || spec == 'i') {
            snprintf(buf, sizeof(buf), "%d", static_cast<int>(as_s(a, a.type)));
            out += buf;
        } else if (spec == 'u') {
            snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(as_u(a, a.type)));
            out += buf;
        } else if (spec == 's') {
            out += cstr_text(a);
        } else if (spec == 'c') {
            out += static_cast<char>(as_u(a, a.type));
        } else if (spec == 'f' || spec == 'g') {
            snprintf(buf, sizeof(buf), "%g", a.f);
            out += buf;
        }
    }
    return out;
}

auto Interp::eval_extern(Node* n, Node* fn) -> Value {
    string name = extern_symbol(fn);
    vector<Value> args;
    for (Node* a = n->body; a != nullptr; a = a->next) {
        args.push_back(eval(a->left));
        if (trapped) {
            return v_unit();
        }
    }
    Type* rt = n->ty != nullptr ? n->ty : fn->ty;
    Value r = v_unit();
    r.type = rt;
    if (name == "abs") {
        int64_t v = args.empty() ? 0 : as_s(args[0], args[0].type);
        if (v < 0) {
            v = -v;
        }
        return v_int(rt, static_cast<uint64_t>(v));
    }
    if (name == "strlen") {
        string s = args.empty() ? string() : cstr_text(args[0]);
        return v_int(rt, s.size());
    }
    if (name == "printf") {
        string fmt = args.empty() ? string() : cstr_text(args[0]);
        vector<Value> rest;
        for (size_t i = 1; i < args.size(); i++) {
            rest.push_back(args[i]);
        }
        string printed = interp_printf(fmt, rest);
        output += printed;
        return v_int(rt, printed.size());
    }
    if (name == "lb_null_probe") {
        r.kind = TypeKind::Pointer;
        r.ptr = nullptr;
    } else {
        fail("unknown extern `" + name + "`");
        return v_unit();
    }
    if (needs_null_foreign(rt)) {
        bool is_null = r.ptr == nullptr;
        if (rt->kind == TypeKind::CStr) {
            is_null = r.str.data() == nullptr;
        }
        if (is_null) {
            fail("null_foreign");
            return v_unit();
        }
    }
    return r;
}

auto Interp::eval_ctor(Node* n, Node* st) -> Value {
    if (st == nullptr) {
        return v_unit();
    }
    // Every omitted field is its zero value, arrays included (base.md §10.1).
    Value v = zero_of(st->ty);
    int i = 0;
    for (Node* f = st->body; f != nullptr; f = f->next) {
        if (f->kind != NodeKind::Field) {
            continue;
        }
        Node* provided = nullptr;
        for (Node* a = n != nullptr ? n->body : nullptr; a != nullptr; a = a->next) {
            if (a->text == f->text) {
                provided = a;
                break;
            }
        }
        Node* init = provided != nullptr && provided->left != nullptr ? provided->left : f->left;
        if (init != nullptr && i < static_cast<int>(v.fields.size())) {
            Value fv = eval(init);
            // An array handed to a span field is viewed, not copied (base.md §5.4).
            if (f->ty != nullptr && f->ty->kind == TypeKind::Span && fv.kind == TypeKind::Array) {
                fv.kind = TypeKind::Span;
                fv.type = f->ty;
            }
            v.fields[static_cast<size_t>(i)] = fv;
        }
        if (trapped) {
            return v_unit();
        }
        i++;
    }
    return v;
}

} // namespace lucb

namespace lucb {

// `f64.bits(u)`, `f32.bits(u)`, and `value.bits()`: a float and its IEEE bits (§7.5).
auto Interp::eval_float_bits(Node* callee, Node* n) -> Value {
    Node* obj = callee->left;
    const bool from_bits = obj->kind == NodeKind::Name && (obj->text == "f32" || obj->text == "f64");
    if (from_bits) {
        Value bits = eval(n->body != nullptr ? n->body->left : nullptr);
        if (trapped) {
            return v_unit();
        }
        if (obj->text == "f32") {
            uint32_t u = static_cast<uint32_t>(bits.u);
            float f = 0;
            std::memcpy(&f, &u, sizeof f);
            return v_float(n->ty, f);
        }
        uint64_t u = bits.u;
        double d = 0;
        std::memcpy(&d, &u, sizeof d);
        return v_float(n->ty, d);
    }
    Value v = eval(obj);
    if (trapped) {
        return v_unit();
    }
    if (v.kind == TypeKind::F32 || (obj->ty != nullptr && obj->ty->kind == TypeKind::F32)) {
        float f = static_cast<float>(v.f);
        uint32_t u = 0;
        std::memcpy(&u, &f, sizeof u);
        return v_int(n->ty, u);
    }
    double d = v.f;
    uint64_t u = 0;
    std::memcpy(&u, &d, sizeof u);
    return v_int(n->ty, u);
}

} // namespace lucb

namespace lucb {

// Whether an expression names storage: a binding, `self`, an element, a dereference, or a
// member of one of those. An enum case or a call's result is a value, not a place.
auto Interp::is_place_expression(Node* e) -> bool {
    if (e == nullptr) {
        return false;
    }
    switch (e->kind) {
    case NodeKind::Name:
    case NodeKind::Self:
        return true;
    case NodeKind::Index:
        return true;
    case NodeKind::Unary:
        return e->op == TokenKind::Star;
    case NodeKind::Member:
        if (e->resolved != nullptr && e->resolved->kind == NodeKind::EnumCase) {
            return false;
        }
        return is_place_expression(e->left) ||
               (e->left != nullptr && is_ptr(e->left->ty));
    default:
        return false;
    }
}

} // namespace lucb
