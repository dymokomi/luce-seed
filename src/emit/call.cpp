//==============================================================================================
//
//   emit/call - Calls: direct, method, function-pointer, extern, and the standard modules
//
//   DESCRIPTION:
//       Emission of every call form. Ordinary and generic calls become C calls with `lb_`
//       names; method calls pass the receiver by pointer; function values call through their
//       typedef; extern calls cross the one C boundary with the null check of base.md §17.1
//       and variadic promotion of §17.2. The standard modules (`memory`, `io`, `files`,
//       `process`, `thread`, `sync`, `atomic`, `luce`) are not Base source in the seed, so
//       their calls are spelled here directly as runtime calls or C expressions.
//
//==============================================================================================

#include "emit/emitter.h"

#include "support/literal.h"
#include <cinttypes>
#include <cstdio>

namespace lucb {

static string memorder_of(Node* n) {
    if (n != nullptr && (n->kind == NodeKind::CaseValue || n->kind == NodeKind::Member)) {
        if (n->text == "relaxed") {
            return "memory_order_relaxed";
        }
        if (n->text == "acquire") {
            return "memory_order_acquire";
        }
        if (n->text == "release") {
            return "memory_order_release";
        }
        if (n->text == "acq_rel") {
            return "memory_order_acq_rel";
        }
        if (n->text == "signal") {
            return "signal";
        }
    }
    return "memory_order_seq_cst";
}

auto Emitter::emit_args(Node* args) -> string {
    string s;
    bool first = true;
    for (Node* a = args; a != nullptr; a = a->next) {
        if (!first) {
            s += ", ";
        }
        first = false;
        Type* want = a->resolved != nullptr ? a->resolved->ty : nullptr;
        if (is_u8_cspan(want)) {
            s += emit_as_cspan(a->left);
        } else {
            s += emit_expr(a->left);
        }
    }
    return s;
}

auto Emitter::emit_direct_call(Node* fn, Node* owner, Node* args) -> string {
    string name = func_ident(fn, owner);
    bool split = fn != nullptr && (fn->flags & FlagExport) != 0;
    if (split) {
        split = false;
        for (Node* p = fn != nullptr ? fn->right : nullptr; p != nullptr; p = p->next) {
            if (is_span(p->ty)) {
                split = true;
                break;
            }
        }
    }
    if (!split) {
        return name + "(" + emit_args(args) + ")";
    }
    string s = "({ ";
    string alist;
    bool first = true;
    Node* p = fn->right;
    Node* a = args;
    while (p != nullptr) {
        if (!first) {
            alist += ", ";
        }
        first = false;
        if (is_span(p->ty)) {
            int id = tmp();
            string tn = "_lb_xa" + std::to_string(id);
            string e = a != nullptr && a->left != nullptr ? emit_expr(a->left)
                                                          : string("((lb_cspan){(void*)8,0})");
            s += c_type(p->ty) + " " + tn + " = " + e + "; ";
            alist += tn + ".data, " + tn + ".length";
        } else {
            alist += a != nullptr && a->left != nullptr ? emit_expr(a->left) : string("0");
        }
        p = p->next;
        if (a != nullptr) {
            a = a->next;
        }
    }
    s += name + "(" + alist + "); })";
    return s;
}

bool has_out_params(Node* fn) {
    for (Node* p = fn != nullptr ? fn->right : nullptr; p != nullptr; p = p->next) {
        if (p->flags & FlagOut) {
            return true;
        }
    }
    return false;
}

// `({ int32_t _lb_o1; double _lb_r = frexp(x, &_lb_o1); (tuple){ _lb_r, _lb_o1 }; })`: an
// extern's `out` parameters are locals passed by address and answered after the declared
// result (§17.1).
auto Emitter::emit_extern_out_call(Node* n) -> string {
    Node* fn = n->resolved;
    int id = tmp();
    string prefix = "_lb_o" + std::to_string(id) + "_";
    string s = "({ ";
    int k = 0;
    for (Node* p = fn->right; p != nullptr; p = p->next) {
        if (p->flags & FlagOut) {
            s += c_type(p->ty) + " " + prefix + std::to_string(k++) + "; ";
        }
    }
    bool has_result = fn->ty != nullptr && fn->ty->kind != TypeKind::Unit;
    string result = "_lb_or" + std::to_string(id);
    if (has_result) {
        s += c_type(fn->ty) + " " + result + " = ";
    }
    s += func_ident(fn, nullptr) + "(" + emit_extern_args(n, prefix) + "); ";
    vector<string> parts;
    if (has_result) {
        parts.push_back(result);
    }
    for (int i = 0; i < k; i++) {
        parts.push_back(prefix + std::to_string(i));
    }
    if (parts.size() == 1) {
        s += parts[0] + "; })";
        return s;
    }
    s += "((" + c_type(n->ty) + "){ ";
    for (size_t i = 0; i < parts.size(); i++) {
        s += (i == 0 ? "" : ", ") + parts[i];
    }
    s += " }); })";
    return s;
}

auto Emitter::emit_extern_args(Node* n, const string& out_prefix) -> string {
    string s;
    bool first = true;
    int outs = 0;
    Node* p = n->resolved != nullptr ? n->resolved->right : nullptr;
    for (Node* a = n->body; a != nullptr || (p != nullptr && (p->flags & FlagOut) != 0);) {
        if (!first) {
            s += ", ";
        }
        first = false;
        if (p != nullptr && (p->flags & FlagOut) != 0) {
            s += "&" + out_prefix + std::to_string(outs++);
            p = p->next;
            continue;
        }
        Node* v = a->left;
        Type* pt = p != nullptr && (p->flags & FlagVariadic) == 0 ? p->ty : nullptr;
        if (v != nullptr && v->kind == NodeKind::Literal && v->op == TokenKind::StringLit &&
            (pt == nullptr || (pt != nullptr && pt->kind == TypeKind::CStr))) {
            s += c_escape(decode_lit(v->text));
        } else if (pt != nullptr && pt->kind == TypeKind::CStr && v != nullptr && v->ty != nullptr &&
                   v->ty->kind == TypeKind::Str) {
            s += "(" + emit_expr(v) + ").data";
        } else {
            s += emit_expr(v);
        }
        if (p != nullptr && (p->flags & FlagVariadic) == 0) {
            p = p->next;
        }
        a = a->next;
    }
    return s;
}

auto Emitter::emit_call(Node* n) -> string {
    Node* callee = n->left;
    if (callee != nullptr && callee->kind == NodeKind::Member && callee->resolved != nullptr &&
        callee->resolved->kind == NodeKind::Field && is_func(callee->ty)) {
        // `holder.callback(args)`: a field of function type, called through its value
        return "(" + emit_expr(callee) + ")(" + emit_args(n->body) + ")";
    }
    if (callee != nullptr && callee->kind == NodeKind::Member && callee->left != nullptr &&
        callee->left->kind == NodeKind::Name && callee->left->text == "ErrorCode" &&
        callee->text == "package") {
        if (n->flags & FlagPackageCode) {
            return "((uint32_t)" + std::to_string(n->cached) + "u)";
        }
        Node* arg = n->body != nullptr ? n->body->left : nullptr;
        return "(uint32_t)(" + (arg != nullptr ? emit_expr(arg) : string("0")) + ")";
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "format") {
        return emit_format_call(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "CAllocator") {
        return "lb_heap_alloc()";
    }
    if (callee != nullptr && callee->kind == NodeKind::Member) {
        Type* ot = callee->left != nullptr ? callee->left->ty : nullptr;
        Type* recv = ot;
        if (is_ptr(ot) && ot->elem != nullptr) {
            recv = ot->elem;
        }
        if (ot != nullptr && ot->kind == TypeKind::Module && callee->text == "fence") {
            string ord = memorder_of(n->body != nullptr ? n->body->left : nullptr);
            if (ord == "signal") {
                return "({ __asm__ volatile(\"\" ::: \"memory\"); (void)0; })";
            }
            return "(atomic_thread_fence(" + ord + "))";
        }
        if (ot != nullptr && ot->kind == TypeKind::Module && callee->text == "current") {
            return "((lb_Handle){ (size_t)pthread_self() })";
        }
        if (ot != nullptr && ot->kind == TypeKind::Module && callee->text == "pause") {
            return "(lb_pause())";
        }
        if (ot != nullptr && ot->kind == TypeKind::Module && callee->text == "yield") {
            return "(sched_yield())";
        }
        if (ot != nullptr && ot->kind == TypeKind::Module && callee->text == "sleep") {
            string ms = n->body != nullptr ? emit_expr(n->body->left) : "0";
            int id = tmp();
            string tn = "_lb_ts" + std::to_string(id);
            return "({ struct timespec " + tn + "; " + tn + ".tv_sec = (time_t)((" + ms +
                   ") / 1000); " + tn + ".tv_nsec = (long)(((" + ms +
                   ") % 1000) * 1000000L); nanosleep(&" + tn + ", NULL); (void)0; })";
        }
        if (ot != nullptr && ot->kind == TypeKind::Module && callee->text == "spawn") {
            Node* entry = n->body != nullptr ? n->body->left : nullptr;
            Node* ctx =
                n->body != nullptr && n->body->next != nullptr ? n->body->next->left : nullptr;
            string fn = entry != nullptr && entry->resolved != nullptr
                            ? func_ident(entry->resolved, nullptr)
                            : "lb_unknown";
            string c = ctx != nullptr ? emit_expr(ctx) : "NULL";
            int id = tmp();
            string tn = "_lb_th" + std::to_string(id);
            string rn = "_lb_tr" + std::to_string(id);
            return "({ pthread_t " + tn + "; " + fail_c_name(n->ty) + " " + rn +
                   "; if (pthread_create(&" + tn + ", NULL, (void*(*)(void*))(void*)" + fn +
                   ", (void*)(" + c + ")) != 0) { " + rn + " = (" + fail_c_name(n->ty) +
                   "){ .failed = true, .error = { .code = 1, .message = (lb_str){\"spawn\", 5} } "
                   "}; } else { " +
                   rn + ".failed = false; " + rn + ".value = (lb_Handle){ (size_t)" + tn +
                   " }; } " + rn + "; })";
        }
        if (is_atomic(ot) || is_atomic(recv)) {
            string loc =
                is_ptr(ot) ? emit_expr(callee->left) : ("&(" + emit_expr(callee->left) + ")");
            Type* elem = is_atomic(ot) ? ot->elem : recv->elem;
            string et = c_type(elem);
            if (callee->text == "wait") {
                string exp = n->body != nullptr ? emit_expr(n->body->left) : "0";
                return "({ while (atomic_load_explicit(" + loc + ", memory_order_seq_cst) == (" +
                       et + ")(" + exp + ")) { lb_pause(); } (void)0; })";
            }
            if (callee->text == "wake") {
                return "((void)(" + (n->body != nullptr ? emit_expr(n->body->left) : string("0")) +
                       "))";
            }
            if (callee->text == "cas") {
                Node* a = n->body;
                string exp = a != nullptr ? emit_expr(a->left) : "0";
                a = a != nullptr ? a->next : nullptr;
                string des = a != nullptr ? emit_expr(a->left) : "0";
                a = a != nullptr ? a->next : nullptr;
                string succ = "memory_order_seq_cst";
                string failo = "memory_order_seq_cst";
                string weak = "0";
                if (a != nullptr && a->text != "weak") {
                    succ = memorder_of(a->left);
                    a = a->next;
                    if (a != nullptr && a->text != "weak") {
                        failo = memorder_of(a->left);
                        a = a->next;
                    }
                }
                if (a != nullptr) {
                    weak = emit_expr(a->left);
                }
                int id = tmp();
                string en = "_lb_ce" + std::to_string(id);
                string on = "_lb_co" + std::to_string(id);
                string tn = tup_c_name(n->ty);
                return "({ " + et + " " + en + " = (" + et + ")(" + exp + "); bool " + on +
                       "; if (" + weak + ") { " + on + " = atomic_compare_exchange_weak_explicit(" +
                       loc + ", &" + en + ", (" + et + ")(" + des + "), " + succ + ", " + failo +
                       "); } else { " + on + " = atomic_compare_exchange_strong_explicit(" + loc +
                       ", &" + en + ", (" + et + ")(" + des + "), " + succ + ", " + failo +
                       "); } (" + tn + "){ " + on + ", " + en + " }; })";
            }
            Node* extra = nullptr;
            if (n->body != nullptr && n->body->next != nullptr) {
                extra = n->body->next->left;
            } else if (n->body != nullptr && callee->text == "load") {
                extra = n->body->left;
            }
            string ord = memorder_of(extra);
            if (callee->text == "load") {
                return "(" + et + ")atomic_load_explicit(" + loc + ", " + ord + ")";
            }
            string val = n->body != nullptr ? emit_expr(n->body->left) : "0";
            if (callee->text == "store") {
                return "(atomic_store_explicit(" + loc + ", (" + et + ")(" + val + "), " + ord +
                       "), (void)0)";
            }
            if (callee->text == "max" || callee->text == "min") {
                string cmp = callee->text == "max" ? ">=" : "<=";
                int id = tmp();
                string on = "_lb_mo" + std::to_string(id);
                string nn = "_lb_mn" + std::to_string(id);
                return "({ " + et + " " + on + " = atomic_load_explicit(" + loc + ", " + ord +
                       "); " + et + " " + nn + " = (" + et + ")(" + val + "); for (;;) { " + et +
                       " _w = (" + on + " " + cmp + " " + nn + ") ? " + on + " : " + nn +
                       "; if (atomic_compare_exchange_weak_explicit(" + loc + ", &" + on +
                       ", _w, " + ord + ", " + ord + ")) break; } " + on + "; })";
            }
            const char* op = "atomic_fetch_add_explicit";
            if (callee->text == "sub") {
                op = "atomic_fetch_sub_explicit";
            } else if (callee->text == "set") {
                op = "atomic_fetch_or_explicit";
            } else if (callee->text == "clear") {
                op = "atomic_fetch_and_explicit";
            } else if (callee->text == "flip") {
                op = "atomic_fetch_xor_explicit";
            } else if (callee->text == "swap") {
                op = "atomic_exchange_explicit";
            }
            string arg = val;
            if (callee->text == "clear") {
                arg = "~(" + val + ")";
            }
            return "(" + et + ")" + string(op) + "(" + loc + ", (" + et + ")(" + arg + "), " + ord +
                   ")";
        }
        if (recv != nullptr && recv->kind == TypeKind::Struct && recv->name == "Handle" &&
            callee->text == "join") {
            string h = emit_expr(callee->left);
            return "({ pthread_join((pthread_t)(" + h + ".id), NULL); (" + fail_c_name(n->ty) +
                   "){ .failed = false }; })";
        }
        if (recv != nullptr && recv->kind == TypeKind::Struct && recv->name == "Handle" &&
            callee->text == "detach") {
            string h = emit_expr(callee->left);
            return "(pthread_detach((pthread_t)(" + h + ".id)))";
        }
        if (recv != nullptr && recv->kind == TypeKind::Struct &&
            (recv->name == "Mutex" || recv->name == "Condition" || recv->name == "Once" ||
             recv->name == "Semaphore")) {
            string loc = is_ptr(ot) ? emit_expr(callee->left) : emit_addr(callee->left);
            if (recv->name == "Mutex" && callee->text == "lock") {
                return "(lb_mutex_lock(" + loc + "))";
            }
            if (recv->name == "Mutex" && callee->text == "unlock") {
                return "(lb_mutex_unlock(" + loc + "))";
            }
            if (recv->name == "Mutex" && callee->text == "try") {
                return "(lb_mutex_try(" + loc + "))";
            }
            if (recv->name == "Condition" && callee->text == "wait") {
                string mu = n->body != nullptr ? emit_expr(n->body->left) : "NULL";
                return "(lb_cond_wait(" + loc + ", " + mu + "))";
            }
            if (recv->name == "Condition" && callee->text == "signal") {
                return "(lb_cond_signal(" + loc + "))";
            }
            if (recv->name == "Condition" && callee->text == "broadcast") {
                return "(lb_cond_broadcast(" + loc + "))";
            }
            if (recv->name == "Once" && callee->text == "run") {
                Node* entry = n->body != nullptr ? n->body->left : nullptr;
                string fn = entry != nullptr && entry->resolved != nullptr
                                ? func_ident(entry->resolved, nullptr)
                                : "lb_unknown";
                return "({ if (lb_once_begin(" + loc + ")) { " + fn + "(); lb_once_end(" + loc +
                       "); } else { lb_once_wait(" + loc + "); } (void)0; })";
            }
            if (recv->name == "Semaphore" && callee->text == "acquire") {
                return "(lb_sem_acquire(" + loc + "))";
            }
            if (recv->name == "Semaphore" && callee->text == "release") {
                return "(lb_sem_release(" + loc + "))";
            }
        }
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "discard") {
        Node* arg = n->body != nullptr ? n->body->left : nullptr;
        return "((void)(" + emit_expr(arg) + "))";
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "assert") {
        Node* cond = n->body != nullptr ? n->body->left : nullptr;
        string msg = "\"assert failed\"";
        if (n->body != nullptr && n->body->next != nullptr) {
            msg = "(" + emit_expr(n->body->next->left) + ").data";
        }
        return "((void)((" + emit_expr(cond) + ") ? 0 : (lb_trap(" + msg + "), 0)))";
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "hash") {
        return emit_hash(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "hex") {
        Node* arg = n->body != nullptr ? n->body->left : nullptr;
        Type* t = arg != nullptr ? arg->ty : nullptr;
        string e = emit_expr(arg);
        if (is_ptr(t)) {
            return "lb_show_hex((uint64_t)(uintptr_t)(" + e + "))";
        }
        return "lb_show_hex((uint64_t)(" + e + "))";
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "bin") {
        Node* arg = n->body != nullptr ? n->body->left : nullptr;
        return "lb_show_bin((uint64_t)(" + emit_expr(arg) + "))";
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "pad") {
        Node* arg = n->body != nullptr ? n->body->left : nullptr;
        Node* w = n->body != nullptr && n->body->next != nullptr ? n->body->next->left : nullptr;
        int id = tmp();
        string buf = "_lb_pbuf" + std::to_string(id);
        string bn = "_lb_pb" + std::to_string(id);
        string s = "({ char " + buf + "[256]; lb_fmtbuf " + bn + " = { " + buf + ", 256, 0 }; ";
        s += "(void)" + emit_display_buf(bn, arg) + "; ";
        s += "lb_show_pad(lb_fmtbuf_finish(&" + bn + "), (size_t)(" + emit_expr(w) + ")); })";
        return s;
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "print") {
        Node* arg = n->body != nullptr ? n->body->left : nullptr;
        Type* t = arg != nullptr ? arg->ty : nullptr;
        if (t != nullptr && t->kind == TypeKind::Fmt) {
            if (arg != nullptr && arg->kind == NodeKind::Formatted) {
                return emit_print_formatted(arg);
            }
            return "lb_print_str(" + emit_expr(arg) + ")";
        }
        string e = emit_expr(arg);
        if (t != nullptr && t->kind == TypeKind::Bool) {
            return "lb_print_bool(" + e + ")";
        }
        if (t != nullptr && t->kind == TypeKind::Str) {
            return "lb_print_str(" + e + ")";
        }
        if (is_float(t)) {
            return "lb_print_f64((double)(" + e + "))";
        }
        if (t != nullptr && is_unsigned_int(t)) {
            return "lb_print_u64((uint64_t)(" + e + "))";
        }
        return "lb_print_i64((int64_t)(" + e + "))";
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "trap") {
        Node* arg = n->body != nullptr ? n->body->left : nullptr;
        return "lb_trap((" + emit_expr(arg) + ").data)";
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "error") {
        Node* code = n->body != nullptr ? n->body->left : nullptr;
        Node* msg = n->body != nullptr && n->body->next != nullptr ? n->body->next->left : nullptr;
        return wrap_err(emit_expr(code), emit_expr(msg));
    }
    if (callee != nullptr && callee->kind == NodeKind::Name &&
        (callee->text == "sizeof" || callee->text == "alignof")) {
        Node* arg = n->body != nullptr ? n->body->left : nullptr;
        Type* t = arg != nullptr ? arg->ty : nullptr;
        string ty = c_type(t);
        if (callee->text == "sizeof") {
            return "((size_t)sizeof(" + ty + "))";
        }
        return "((size_t)_Alignof(" + ty + "))";
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "offsetof") {
        Node* tyarg = n->body != nullptr ? n->body->left : nullptr;
        Node* field =
            n->body != nullptr && n->body->next != nullptr ? n->body->next->left : nullptr;
        Type* t = tyarg != nullptr ? tyarg->ty : nullptr;
        string ty = c_type(t);
        string f = field != nullptr ? string(field->text) : "x";
        return "((size_t)offsetof(" + ty + ", " + f + "))";
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "str" &&
        n->body != nullptr) {
        bool checked = n->ty != nullptr && is_fail(n->ty);
        return emit_str_conv(n->body->left, checked);
    }
    if (is_checked_conversion(n)) {
        return emit_conv(n->body->left, n->ty, true);
    }
    if (n->resolved != nullptr && n->resolved->kind == NodeKind::Struct) {
        return emit_ctor(n, n->resolved);
    }
    if (n->resolved != nullptr && n->resolved->kind == NodeKind::Func &&
        n->resolved->text == "init" && callee != nullptr && callee->kind == NodeKind::Name &&
        callee->resolved != nullptr && callee->resolved->kind == NodeKind::Struct) {
        Node* st = callee->resolved;
        int id = tmp();
        string vn = "_lb_iv" + std::to_string(id);
        string rn = "_lb_ir" + std::to_string(id);
        string sty = struct_ident(st);
        string initf = func_ident(n->resolved, st);
        string args = emit_args(n->body);
        string s = "({ " + sty + " " + vn + " = {0}; ";
        s += fail_c_name(n->resolved->ty) + " " + rn + " = " + initf + "(&" + vn;
        if (!args.empty()) {
            s += ", " + args;
        }
        s += "); ";
        if (n->ty != nullptr && is_fail(n->ty)) {
            string orty = fail_c_name(n->ty);
            s += orty + " _lb_io" + std::to_string(id) + "; ";
            s += "if (" + rn + ".failed) { _lb_io" + std::to_string(id) + ".failed = true; _lb_io" +
                 std::to_string(id) + ".error = " + rn + ".error; } else { _lb_io" +
                 std::to_string(id) + ".failed = false; _lb_io" + std::to_string(id) +
                 ".value = " + vn + "; } _lb_io" + std::to_string(id) + "; })";
            return s;
        }
        s += "if (" + rn + ".failed) { return " + rn + "; } " + vn + "; })";
        return s;
    }
    if (callee != nullptr && callee->kind == NodeKind::Member) {
        if (callee->resolved != nullptr && callee->resolved->kind == NodeKind::EnumCase) {
            return emit_enum_value(n);
        }
        Type* lt = callee->left != nullptr ? callee->left->ty : nullptr;
        if (lt != nullptr && lt->kind == TypeKind::Module && n->resolved != nullptr &&
            n->resolved->kind == NodeKind::Func) {
            if (lt->name == "io" && callee->text == "stdout") {
                return "((lb_iface){ (void*)stdout, &lb_vt_file })";
            }
            if (lt->name == "io" && callee->text == "stderr") {
                return "((lb_iface){ (void*)stderr, &lb_vt_file })";
            }
            if (lt->name == "c" && callee->text == "stdin") {
                return "((void*)stdin)";
            }
            if (lt->name == "c" && callee->text == "stdout") {
                return "((void*)stdout)";
            }
            if (lt->name == "c" && callee->text == "stderr") {
                return "((void*)stderr)";
            }
            if (lt->name == "files" && callee->text == "read") {
                Node* parg = n->body != nullptr ? n->body->left : nullptr;
                string p = parg != nullptr ? emit_expr(parg) : "NULL";
                string pathc =
                    parg != nullptr && parg->ty != nullptr && parg->ty->kind == TypeKind::CStr
                        ? p
                        : "((" + p + ").data)";
                string rty = fail_c_name(n->ty);
                return "({ const char* _lb_fp = " + pathc +
                       "; FILE* _lb_f = fopen(_lb_fp, "
                       "\"rb\"); " +
                       rty +
                       " _lb_fr; if (_lb_f == NULL) { _lb_fr.failed = true; _lb_fr.error = "
                       "(lb_error){ .code = 2, .message = (lb_str){\"missing\", 7} }; } else { "
                       "fseek(_lb_f, 0, SEEK_END); long _lb_n = ftell(_lb_f); rewind(_lb_f); "
                       "if (_lb_n < 0) _lb_n = 0; lb_span_opt _lb_ao = "
                       "lb_alloc_call(lb_get_alloc(), "
                       "(size_t)_lb_n, 1); lb_span _lb_b = _lb_ao.value; if (_lb_n > 0 && "
                       "!_lb_ao.present) { _lb_fr.failed = "
                       "true; _lb_fr.error = (lb_error){ .code = 1, .message = "
                       "(lb_str){\"memory.exhausted\", 16} }; fclose(_lb_f); } else { "
                       "if (_lb_n > 0) fread(_lb_b.data, 1, (size_t)_lb_n, _lb_f); "
                       "fclose(_lb_f); _lb_fr.failed = false; _lb_fr.value = _lb_b; } } _lb_fr; })";
            }
            if (lt->name == "memory" && (callee->text == "copy" || callee->text == "move")) {
                Node* to = n->body != nullptr ? n->body->left : nullptr;
                Node* from =
                    n->body != nullptr && n->body->next != nullptr ? n->body->next->left : nullptr;
                Node* count =
                    n->body != nullptr && n->body->next != nullptr && n->body->next->next != nullptr
                        ? n->body->next->next->left
                        : nullptr;
                int id = tmp();
                string tn = "_lb_to" + std::to_string(id);
                string fn = "_lb_fr" + std::to_string(id);
                string cn = "_lb_n" + std::to_string(id);
                string fn_name = callee->text == "move" ? "memmove" : "memcpy";
                string to_ty = to != nullptr && to->ty != nullptr ? c_type(to->ty) : "lb_span";
                string from_ty =
                    from != nullptr && from->ty != nullptr ? c_type(from->ty) : "lb_cspan";
                string to_e = to != nullptr ? emit_expr(to) : "((lb_span){(void*)8, 0})";
                string from_e = from != nullptr ? emit_expr(from) : "((lb_cspan){(void*)8, 0})";
                Type* elem = to != nullptr && to->ty != nullptr ? to->ty->elem : nullptr;
                string esz = elem != nullptr ? "sizeof(" + c_type(elem) + ")" : "1";
                return "({ " + to_ty + " " + tn + " = " + to_e + "; " + from_ty + " " + fn + " = " +
                       from_e + "; size_t " + cn + " = (size_t)(" + emit_expr(count) + "); if (" +
                       cn + " > " + tn + ".length || " + cn + " > " + fn +
                       ".length) lb_trap(\"index out of bounds\"); " + fn_name + "( (void*)" + tn +
                       ".data, " + fn + ".data, " + cn + " * " + esz + "); (void)0; })";
            }
            if (lt->name == "memory" && callee->text == "set") {
                Node* span = n->body != nullptr ? n->body->left : nullptr;
                Node* byte =
                    n->body != nullptr && n->body->next != nullptr ? n->body->next->left : nullptr;
                int id = tmp();
                string sn = "_lb_set" + std::to_string(id);
                string sp;
                string sty = span != nullptr && span->ty != nullptr ? c_type(span->ty) : "lb_span";
                sp = span != nullptr ? emit_expr(span) : "((lb_span){(void*)8, 0})";
                return "({ " + sty + " " + sn + " = " + sp + "; memset((void*)" + sn +
                       ".data, (int)(unsigned char)(" + emit_expr(byte) + "), " + sn +
                       ".length); (void)0; })";
            }
            if (lt->name == "memory" && callee->text == "grow") {
                Node* block = n->body != nullptr ? n->body->left : nullptr;
                Node* size =
                    n->body != nullptr && n->body->next != nullptr ? n->body->next->left : nullptr;
                string rty = fail_c_name(n->ty);
                int id = tmp();
                string bn = "_lb_gb" + std::to_string(id);
                string gn = "_lb_gg" + std::to_string(id);
                return "({ lb_span " + bn + " = " + emit_expr(block) + "; size_t _lb_gs" +
                       std::to_string(id) + " = (size_t)(" + emit_expr(size) +
                       "); lb_iface _lb_ga" + std::to_string(id) + " = lb_get_alloc(); lb_span " +
                       gn + "; if (lb_resize_call(_lb_ga" + std::to_string(id) + ", " + bn +
                       ", _lb_gs" + std::to_string(id) + ")) { " + gn + ".data = " + bn +
                       ".data; " + gn + ".length = _lb_gs" + std::to_string(id) +
                       "; } else { lb_span_opt _lb_go" + std::to_string(id) +
                       " = lb_alloc_call(_lb_ga" + std::to_string(id) + ", _lb_gs" +
                       std::to_string(id) + ", sizeof(void*)); if (!_lb_go" + std::to_string(id) +
                       ".present) { " + gn + ".data = NULL; " + gn + ".length = 0; } else { if (" +
                       bn + ".length > 0 && " + bn + ".data != NULL) memcpy(_lb_go" +
                       std::to_string(id) + ".value.data, " + bn + ".data, " + bn +
                       ".length < _lb_gs" + std::to_string(id) + " ? " + bn + ".length : _lb_gs" +
                       std::to_string(id) + "); lb_release_call(_lb_ga" + std::to_string(id) +
                       ", " + bn + "); " + gn + " = _lb_go" + std::to_string(id) + ".value; } } " +
                       rty + " _lb_gr" + std::to_string(id) + "; if (" + gn +
                       ".data == NULL && _lb_gs" + std::to_string(id) + " != 0) { _lb_gr" +
                       std::to_string(id) + " = " + emit_exhausted_lit(n->ty) +
                       "; } else { _lb_gr" + std::to_string(id) + ".failed = false; _lb_gr" +
                       std::to_string(id) + ".value = " + gn + "; } _lb_gr" + std::to_string(id) +
                       "; })";
            }
            if (lt->name == "memory" && callee->text == "read") {
                Type* t = n->type != nullptr ? n->type->ty : n->ty;
                string addr = n->body != nullptr ? emit_expr(n->body->left) : "NULL";
                int id = tmp();
                string vn = "_lb_rd" + std::to_string(id);
                return "({ " + c_type(t) + " " + vn + "; memcpy(&" + vn + ", " + addr +
                       ", sizeof(" + vn + ")); " + vn + "; })";
            }
            if (lt->name == "memory" && callee->text == "write") {
                Type* t = n->type != nullptr ? n->type->ty : nullptr;
                string addr = n->body != nullptr ? emit_expr(n->body->left) : "NULL";
                string val = n->body != nullptr && n->body->next != nullptr
                                 ? emit_expr(n->body->next->left)
                                 : "0";
                int id = tmp();
                string vn = "_lb_wr" + std::to_string(id);
                string ty = t != nullptr ? c_type(t) : "int64_t";
                return "({ " + ty + " " + vn + " = " + val + "; memcpy(" + addr + ", &" + vn +
                       ", sizeof(" + vn + ")); (void)0; })";
            }
            if (lt->name == "files" && callee->text == "list") {
                Node* parg = n->body != nullptr ? n->body->left : nullptr;
                string p = parg != nullptr ? emit_expr(parg) : "NULL";
                string pathc =
                    parg != nullptr && parg->ty != nullptr && parg->ty->kind == TypeKind::CStr
                        ? p
                        : "((" + p + ").data)";
                string rty = fail_c_name(n->ty);
                int id = tmp();
                return "({ const char* _lb_lp = " + pathc +
                       "; lb_span _lb_ln; int _lb_le = "
                       "lb_files_list(lb_get_alloc(), _lb_lp, &_lb_ln); " +
                       rty + " _lb_lr" + std::to_string(id) + "; if (_lb_le != 0) { _lb_lr" +
                       std::to_string(id) + ".failed = true; _lb_lr" + std::to_string(id) +
                       ".error = (lb_error){ .code = _lb_le == 2 ? 2 : 1, .message = _lb_le == 2 "
                       "? (lb_str){\"missing\", 7} : (lb_str){\"memory.exhausted\", 16} }; } else "
                       "{ _lb_lr" +
                       std::to_string(id) + ".failed = false; _lb_lr" + std::to_string(id) +
                       ".value = _lb_ln; } _lb_lr" + std::to_string(id) + "; })";
            }
            if (lt->name == "process" && callee->text == "run") {
                Node* prog = n->body != nullptr ? n->body->left : nullptr;
                Node* args =
                    n->body != nullptr && n->body->next != nullptr ? n->body->next->left : nullptr;
                string p = prog != nullptr ? emit_expr(prog) : "NULL";
                string pathc =
                    prog != nullptr && prog->ty != nullptr && prog->ty->kind == TypeKind::CStr
                        ? p
                        : "((" + p + ").data)";
                string a = args != nullptr ? emit_expr(args) : "((lb_cspan){(void*)8, 0})";
                string aty = args != nullptr && args->ty != nullptr ? c_type(args->ty) : "lb_cspan";
                string rty = fail_c_name(n->ty);
                Type* payload = n->ty != nullptr && is_fail(n->ty) ? n->ty->elem : n->ty;
                string tty = c_type(payload);
                int id = tmp();
                string an = "_lb_pa" + std::to_string(id);
                string rn = "_lb_pr" + std::to_string(id);
                return "({ const char* _lb_pp = " + pathc + "; " + aty + " " + an + " = " + a +
                       "; int32_t _lb_st = 0; lb_str _lb_so = {NULL, 0}; lb_str _lb_se = {NULL, "
                       "0}; int _lb_rc = lb_process_run(_lb_pp, (const char* const*)" +
                       an + ".data, " + an +
                       ".length, lb_get_alloc(), &_lb_st, &_lb_so, "
                       "&_lb_se); " +
                       rty + " " + rn + "; if (_lb_rc != 0) { " + rn + ".failed = true; " + rn +
                       ".error = (lb_error){ .code = 1, .message = (lb_str){\"run\", 3} }; } else "
                       "{ " +
                       rn + ".failed = false; " + rn + ".value = ((" + tty +
                       "){ .a0 = _lb_st, .a1 = _lb_so, .a2 = _lb_se }); } " + rn + "; })";
            }
            if (lt->name == "files" && callee->text == "write") {
                Node* parg = n->body != nullptr ? n->body->left : nullptr;
                string p = parg != nullptr ? emit_expr(parg) : "NULL";
                string pathc =
                    parg != nullptr && parg->ty != nullptr && parg->ty->kind == TypeKind::CStr
                        ? p
                        : "((" + p + ").data)";
                string b = n->body != nullptr && n->body->next != nullptr
                               ? emit_as_cspan(n->body->next->left)
                               : "((lb_cspan){NULL,0})";
                string rty = fail_c_name(n->ty);
                return "({ const char* _lb_fp = " + pathc + "; lb_cspan _lb_fb = " + b +
                       "; FILE* _lb_f = fopen(_lb_fp, \"wb\"); " + rty +
                       " _lb_fr; if (_lb_f == NULL) { _lb_fr.failed = true; _lb_fr.error = "
                       "(lb_error){ .code = 2, .message = (lb_str){\"missing\", 7} }; } else { "
                       "size_t _lb_n = _lb_fb.length; size_t _lb_w = _lb_n == 0 ? 0 : "
                       "fwrite(_lb_fb.data, 1, _lb_n, _lb_f); fclose(_lb_f); if (_lb_w != _lb_n) { "
                       "_lb_fr.failed = true; _lb_fr.error = (lb_error){ .code = 1, .message = "
                       "(lb_str){\"write\", 5} }; } else { _lb_fr.failed = false; } } _lb_fr; })";
            }
            return func_ident(n->resolved, nullptr) + "(" + emit_args(n->body) + ")";
        }
        if (lt != nullptr && lt->kind == TypeKind::Module && n->resolved != nullptr &&
            n->resolved->kind == NodeKind::Struct) {
            return emit_ctor(n, n->resolved);
        }
        Node* method = callee->resolved;
        Node* obj = callee->left;
        Type* ot = obj != nullptr ? obj->ty : nullptr;
        if (ot != nullptr && is_ptr(ot) && ot->elem != nullptr) {
            ot = ot->elem;
        }
        if (ot != nullptr && ot->kind == TypeKind::Interface && method != nullptr) {
            int id = tmp();
            string vn = "_lb_if" + std::to_string(id);
            string args = emit_args(n->body);
            string call = "((" + vt_type_name(ot) + "*)" + vn + ".vtable)->" +
                          string(method->text) + "(" + vn + ".data";
            if (!args.empty()) {
                call += ", " + args;
            }
            call += ")";
            return "({ lb_iface " + vn + " = " + emit_expr(obj) + "; " + call + "; })";
        }
        if (callee->text == "bits" && method == nullptr && obj != nullptr) {
            return emit_float_bits(obj, n);
        }
        if (callee->text == "compare" && method == nullptr) {
            string L = emit_expr(obj);
            string R = emit_expr(n->body != nullptr ? n->body->left : nullptr);
            Type* rt = obj != nullptr ? obj->ty : nullptr;
            if (rt != nullptr && rt->kind == TypeKind::Str) {
                // text orders by bytes, then by length (§5.5), through the runtime
                return "((int64_t)lb_str_compare(" + L + ", " + R + "))";
            }
            return "((" + L + " < " + R + ") ? -1LL : ((" + L + " > " + R + ") ? 1LL : 0LL))";
        }
        Node* owner = ot != nullptr ? ot->decl : nullptr;
        string name = func_ident(method, owner);
        string args = emit_args(n->body);
        if (method != nullptr && (method->flags & FlagStatic) != 0) {
            bool fixed_over =
                method->text == "over" && ((owner != nullptr && owner->text == "FixedBuffer") ||
                                           (n->ty != nullptr && n->ty->name == "FixedBuffer"));
            if (fixed_over) {
                Node* arg = n->body != nullptr ? n->body->left : nullptr;
                Type* at = arg != nullptr ? arg->ty : nullptr;
                string e = emit_expr(arg);
                if (is_array(at)) {
                    return "((lb_fixed){ .data = (uint8_t*)(" + e +
                           ".d), .cap = " + std::to_string(at->length) + "ULL, .used = 0 })";
                }
                int sid = tmp();
                string sn = "_lb_s" + std::to_string(sid);
                return "({ lb_span " + sn + " = " + e + "; (lb_fixed){ .data = (uint8_t*)" + sn +
                       ".data, .cap = " + sn + ".length, .used = 0 }; })";
            }
            return name + "(" + args + ")";
        }
        // A pointer-typed receiver already is the address the method takes; a place is
        // addressed; a value, `Flags.a.name()` or a call's result, is held in a temporary.
        if (obj != nullptr && !is_ptr(obj->ty) && !is_place_expression(obj)) {
            string tn = "_lb_rc" + std::to_string(tmp());
            string call = name + "(&" + tn + (args.empty() ? "" : ", " + args) + ")";
            return "({ " + c_type(obj->ty) + " " + tn + " = " + emit_expr(obj) + "; " + call + "; })";
        }
        string recv = is_ptr(obj != nullptr ? obj->ty : nullptr) ? emit_expr(obj) : emit_addr(obj);
        if (args.empty()) {
            return name + "(" + recv + ")";
        }
        return name + "(" + recv + ", " + args + ")";
    }
    if (n->resolved != nullptr && n->resolved->kind == NodeKind::EnumCase) {
        return emit_enum_value(n);
    }
    if (callee != nullptr && is_func(callee->ty) &&
        (n->resolved == nullptr ||
         (n->resolved->kind != NodeKind::Func && n->resolved->kind != NodeKind::ExternFunc))) {
        return "(" + emit_expr(callee) + ")(" + emit_args(n->body) + ")";
    }
    if (n->resolved != nullptr &&
        (n->resolved->kind == NodeKind::Func || n->resolved->kind == NodeKind::ExternFunc)) {
        string call;
        if (n->resolved->kind == NodeKind::ExternFunc && has_out_params(n->resolved)) {
            return emit_extern_out_call(n);
        }
        if (n->resolved->kind == NodeKind::ExternFunc) {
            string name = func_ident(n->resolved, nullptr);
            string args = n->body != nullptr ? emit_extern_args(n) : emit_args(n->body);
            call = name + "(" + args + ")";
        } else {
            call = emit_direct_call(n->resolved, nullptr, n->body);
        }
        Type* rt = n->ty != nullptr ? n->ty : n->resolved->ty;
        if (n->resolved->kind == NodeKind::ExternFunc && needs_null_foreign(rt)) {
            int id = tmp();
            string pn = "_lb_fp" + std::to_string(id);
            return "({ " + c_type(rt) + " " + pn + " = " + call + "; if (" + pn +
                   " == NULL) lb_trap(\"null_foreign\"); " + pn + "; })";
        }
        return call;
    }
    return "0";
}

auto Emitter::emit_ctor(Node* n, Node* st) -> string {
    string s = "(" + struct_ident(st) + "){";
    bool first = true;
    if (st != nullptr) {
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
            string val;
            if (provided != nullptr && provided->left != nullptr) {
                val = emit_expr(provided->left);
            } else if (f->left != nullptr) {
                val = emit_expr(f->left);
            } else {
                continue;
            }
            if (!first) {
                s += ", ";
            }
            first = false;
            s += "." + string(f->text) + " = " + val;
        }
    }
    s += "}";
    return s;
}

} // namespace lucb

namespace lucb {

// `f64.bits(u)` and `value.bits()` are a memcpy each way (§7.5), at the float's width.
auto Emitter::emit_float_bits(Node* obj, Node* n) -> string {
    const TypeKind from = obj->kind == NodeKind::Name ? float_kind_named(obj->text) : TypeKind::Error;
    if (from != TypeKind::Error) {
        string in = emit_expr(n->body != nullptr ? n->body->left : nullptr);
        string it = bits_integer_c_name(n->ty);
        string ft = c_type_name(n->ty);
        return "({ " + it + " _lb_b = (" + it + ")(" + in + "); " + ft + " _lb_f; __builtin_memcpy(&_lb_f, &_lb_b, sizeof _lb_f); _lb_f; })";
    }
    string it = bits_integer_c_name(obj->ty);
    string ft = c_type_name(obj->ty);
    return "({ " + ft + " _lb_f = (" + ft + ")(" + emit_expr(obj) + "); " + it + " _lb_b; __builtin_memcpy(&_lb_b, &_lb_f, sizeof _lb_b); _lb_b; })";
}

} // namespace lucb

namespace lucb {

// Whether an expression names storage whose address a method may take: a binding, `self`,
// an element, a dereference, or a member of one of those. An enum case, a literal, and a
// call's result are values.
auto Emitter::is_place_expression(Node* e) -> bool {
    if (e == nullptr) {
        return false;
    }
    switch (e->kind) {
    case NodeKind::Name:
    case NodeKind::Self:
    case NodeKind::Index:
        return true;
    case NodeKind::Unary:
        return e->op == TokenKind::Star;
    case NodeKind::Member:
        if (e->resolved != nullptr && e->resolved->kind == NodeKind::EnumCase) {
            return false;
        }
        return is_place_expression(e->left) || (e->left != nullptr && is_ptr(e->left->ty));
    default:
        return false;
    }
}

} // namespace lucb
