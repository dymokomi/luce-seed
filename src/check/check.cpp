#include "check/check.h"
#include "check/checker.h"

#include <cstdio>
#include <cstring>

namespace lucb {

auto Checker::named_scalar(string_view name) -> Type* {
        if (name == "i8") {
            return ty_i8;
        }
        if (name == "i16") {
            return ty_i16;
        }
        if (name == "i32") {
            return ty_i32;
        }
        if (name == "i64") {
            return ty_i64;
        }
        if (name == "isize") {
            return ty_isize;
        }
        if (name == "u8") {
            return ty_u8;
        }
        if (name == "u16") {
            return ty_u16;
        }
        if (name == "u32") {
            return ty_u32;
        }
        if (name == "u64") {
            return ty_u64;
        }
        if (name == "usize") {
            return ty_usize;
        }
        if (name == "f32") {
            return ty_f32;
        }
        if (name == "f64") {
            return ty_f64;
        }
        if (name == "char") {
            return ty_char;
        }
        if (name == "bool") {
            return ty_bool;
        }
        if (name == "unit") {
            return ty_unit;
        }
        if (name == "never") {
            return ty_never;
        }
        if (name == "str") {
            return ty_str;
        }
        if (name == "cstr") {
            return ty_cstr;
        }
        if (name == "Allocator" || name == "CAllocator") {
            return ty_alloc;
        }
        if (name == "fmt") {
            return ty_fmt;
        }
        return nullptr;
    }

auto Checker::c_alias(string_view name) -> Type* {
        if (name == "c.int") {
            return ty_i32;
        }
        if (name == "c.uint") {
            return ty_u32;
        }
        if (name == "c.short") {
            return ty_i16;
        }
        if (name == "c.ushort") {
            return ty_u16;
        }
        if (name == "c.schar") {
            return ty_i8;
        }
        if (name == "c.uchar") {
            return ty_u8;
        }
        if (name == "c.longlong") {
            return ty_i64;
        }
        if (name == "c.ulonglong") {
            return ty_u64;
        }
        if (name == "c.float") {
            return ty_f32;
        }
        if (name == "c.double") {
            return ty_f64;
        }
        if (name == "c.bool") {
            return ty_bool;
        }
        if (name == "c.size" || name == "c.uintptr") {
            return ty_usize;
        }
        if (name == "c.ssize" || name == "c.ptrdiff" || name == "c.intptr") {
            return ty_isize;
        }
        if (name == "c.long") {
            return ty_i64;
        }
        if (name == "c.ulong") {
            return ty_u64;
        }
        if (name == "c.char") {
            return ty_i8;
        }
        if (name == "c.wchar") {
            return ty_u32;
        }
        return nullptr;
    }

auto Checker::make_type(TypeKind kind, string_view name) -> Type* {
        Type* t = arena->make<Type>();
        t->kind = kind;
        t->name = name;
        return t;
    }

auto Checker::keep(const string& s) -> string_view {
        char* p = static_cast<char*>(arena->alloc(s.size() + 1, 1));
        memcpy(p, s.data(), s.size());
        p[s.size()] = 0;
        return {p, s.size()};
    }

auto Checker::intern_ptr(Type* elem, bool is_const, bool is_vol, bool nullable) -> Type* {
        for (size_t i = 0; i < interned.size(); i++) {
            Type* t = interned[i];
            if (t->kind == TypeKind::Pointer && t->decl == nullptr && t->elem == elem &&
                t->is_const == is_const && t->is_volatile == is_vol && t->is_nullable == nullable) {
                return t;
            }
        }
        Type* t = make_type(TypeKind::Pointer, {});
        t->elem = elem;
        t->is_const = is_const;
        t->is_volatile = is_vol;
        t->is_nullable = nullable;
        t->name = keep(type_name(t));
        interned.push_back(t);
        return t;
    }

auto Checker::intern_arr(Type* elem, uint64_t n) -> Type* {
        for (size_t i = 0; i < interned.size(); i++) {
            Type* t = interned[i];
            if (t->kind == TypeKind::Array && t->elem == elem && t->length == n) {
                return t;
            }
        }
        Type* t = make_type(TypeKind::Array, {});
        t->elem = elem;
        t->length = n;
        t->name = keep(type_name(t));
        interned.push_back(t);
        return t;
    }

auto Checker::intern_iface(Node* decl, bool nullable) -> Type* {
        for (size_t i = 0; i < interned.size(); i++) {
            Type* t = interned[i];
            if (t->kind == TypeKind::Interface && t->decl == decl && t->is_nullable == nullable) {
                return t;
            }
        }
        Type* t = make_type(TypeKind::Interface, decl != nullptr ? decl->text : string_view{});
        t->decl = decl;
        t->is_nullable = nullable;
        t->name = keep(type_name(t));
        interned.push_back(t);
        return t;
    }

auto Checker::intern_opt(Type* elem) -> Type* {
        if (is_ptr(elem) && !elem->is_nullable) {
            return intern_ptr(elem->elem, elem->is_const, elem->is_volatile, true);
        }
        if (elem != nullptr && elem->kind == TypeKind::Interface && !elem->is_nullable) {
            return intern_iface(elem->decl, true);
        }
        for (size_t i = 0; i < interned.size(); i++) {
            Type* t = interned[i];
            if (t->kind == TypeKind::Optional && t->elem == elem) {
                return t;
            }
        }
        Type* t = make_type(TypeKind::Optional, {});
        t->elem = elem;
        t->name = keep(type_name(t));
        interned.push_back(t);
        return t;
    }

auto Checker::intern_fail(Type* elem) -> Type* {
        for (size_t i = 0; i < interned.size(); i++) {
            Type* t = interned[i];
            if (t->kind == TypeKind::Fallible && t->elem == elem) {
                return t;
            }
        }
        Type* t = make_type(TypeKind::Fallible, {});
        t->elem = elem;
        t->name = keep(type_name(t));
        interned.push_back(t);
        return t;
    }

auto Checker::intern_sp(Type* elem, bool is_const) -> Type* {
        for (size_t i = 0; i < interned.size(); i++) {
            Type* t = interned[i];
            if (t->kind == TypeKind::Span && t->elem == elem && t->is_const == is_const) {
                return t;
            }
        }
        Type* t = make_type(TypeKind::Span, {});
        t->elem = elem;
        t->is_const = is_const;
        t->name = keep(type_name(t));
        interned.push_back(t);
        return t;
    }

auto Checker::intern_atomic(Type* elem) -> Type* {
        for (size_t i = 0; i < interned.size(); i++) {
            Type* t = interned[i];
            if (t->kind == TypeKind::Atomic && t->elem == elem) {
                return t;
            }
        }
        Type* t = make_type(TypeKind::Atomic, {});
        t->elem = elem;
        t->name = keep(type_name(t));
        interned.push_back(t);
        return t;
    }

auto Checker::intern_tup(Type** elems, int n) -> Type* {
        for (size_t i = 0; i < interned.size(); i++) {
            Type* t = interned[i];
            if (t->kind != TypeKind::Tuple || t->ntargs != n) {
                continue;
            }
            bool same = true;
            for (int j = 0; j < n; j++) {
                if (t->args[j] != elems[j]) {
                    same = false;
                    break;
                }
            }
            if (same) {
                return t;
            }
        }
        Type* t = make_type(TypeKind::Tuple, {});
        t->ntargs = n;
        t->args = static_cast<Type**>(arena->alloc(sizeof(Type*) * static_cast<size_t>(n),
                                                   alignof(Type*)));
        for (int j = 0; j < n; j++) {
            t->args[j] = elems[j];
        }
        t->name = keep(type_name(t));
        interned.push_back(t);
        return t;
    }

auto Checker::atomic_ok(Type* t) -> bool {
        if (t == nullptr) {
            return false;
        }
        if (is_int(t) || t->kind == TypeKind::Bool || is_ptr(t)) {
            return type_size(t) <= static_cast<int>(sizeof(void*));
        }
        return false;
    }

auto Checker::syn_node(NodeKind k, const char* name) -> Node* {
        Node* n = arena->make<Node>();
        n->kind = k;
        n->text = keep(name);
        n->flags = FlagPub;
        return n;
    }

auto Checker::syn_method(const char* name, Type* result, bool mutating) -> Node* {
        Node* m = syn_node(NodeKind::Func, name);
        m->ty = result;
        if (mutating) {
            m->flags |= FlagMutating;
        }
        return m;
    }

auto Checker::is_fixed(Type* t) -> bool {
        return t != nullptr && t->kind == TypeKind::Struct && t->name == "FixedBuffer";
    }

auto Checker::check_in_allocator(Node* n) -> Type* {
        if (n == nullptr) {
            return ty_alloc;
        }
        Type* t = check_expr(n);
        if (t != nullptr && t->kind == TypeKind::Allocator) {
            return t;
        }
        if (is_fixed(t)) {
            if (!is_mut_place(n)) {
                fail_n(n, "lucb.check.mut", "`with`/`in` needs a `var` FixedBuffer or an Allocator");
            }
            return t;
        }
        fail_n(n, "lucb.check.type", "`with`/`in` needs an Allocator");
        return t_error();
    }

auto Checker::check_new(Node* n) -> Type* {
        if (n->right != nullptr) {
            check_in_allocator(n->right);
        }
        Node* tn = n->type;
        if (tn == nullptr) {
            fail_n(n, "lucb.check.type", "`new` needs a type");
            return intern_fail(t_error());
        }
        if (tn->flags & FlagArray) {
            Type* elem = resolve_type(tn->left);
            if (tn->right != nullptr) {
                Type* ct = check_expr(tn->right, ty_usize);
                if (!is_int(ct) && ct->kind != TypeKind::UntypedInt) {
                    fail_n(tn->right, "lucb.check.type", "`new T[count]` needs a `usize` count");
                }
            } else {
                fail_n(n, "lucb.check.type", "`new T[count]` needs a count");
            }
            return intern_fail(intern_sp(elem, false));
        }
        Type* t = resolve_type(tn);
        if (n->body != nullptr && n->body->kind == NodeKind::CaseValue) {
            check_case_value(n->body, t);
        } else if (n->body != nullptr) {
            if (t->kind != TypeKind::Struct || t->decl == nullptr) {
                fail_n(n, "lucb.check.type", "`new T(...)` needs a struct type");
            } else {
                n->resolved = t->decl;
                check_ctor(n, t->decl);
            }
        } else if (!is_zeroable(t)) {
            fail_n(n, "lucb.check.type", "`new T` needs a zeroable type or an initialiser");
        }
        return intern_fail(intern_ptr(t, false, false, false));
    }

auto Checker::check_alloc(Node* n) -> Type* {
        if (n->right != nullptr) {
            check_in_allocator(n->right);
        }
        if (n->type == nullptr) {
            int nargs = count_args(n->body);
            if (nargs != 2) {
                fail_n(n, "lucb.check.call", "`alloc(size, alignment)` takes two arguments");
            } else {
                check_expr(n->body->left, ty_usize);
                if (n->body->next != nullptr) {
                    check_expr(n->body->next->left, ty_usize);
                }
            }
            return intern_fail(intern_sp(ty_u8, false));
        }
        if (n->type->flags & FlagArray) {
            Type* elem = resolve_type(n->type->left);
            if (n->type->right != nullptr) {
                Type* ct = check_expr(n->type->right, ty_usize);
                if (!is_int(ct) && ct->kind != TypeKind::UntypedInt) {
                    fail_n(n->type->right, "lucb.check.type", "`alloc T[count]` needs a `usize` count");
                }
            }
            return intern_fail(intern_sp(elem, false));
        }
        fail_n(n, "lucb.check.type", "`alloc` needs `T[count]` or `(size, alignment)`");
        return intern_fail(t_error());
    }

auto Checker::check_free(Node* n) -> void {
        Type* t = check_expr(n->left);
        if (!is_ptr(t) && !is_span(t)) {
            fail_n(n, "lucb.check.type", "`free` needs a pointer or a span");
        }
        if (n->right != nullptr) {
            check_in_allocator(n->right);
        }
    }

auto Checker::check_with(Node* n) -> void {
        check_in_allocator(n->left);
        push_scope();
        check_stmt(n->body);
        pop_scope();
    }

auto Checker::bind_memory() -> void {
        if (ty_alloc == nullptr) {
            ty_alloc = make_type(TypeKind::Allocator, "Allocator");
        }
        Node* data = syn_node(NodeKind::Field, "data");
        data->ty = intern_ptr(ty_u8, false, false, false);
        Node* cap = syn_node(NodeKind::Field, "cap");
        cap->ty = ty_usize;
        Node* used = syn_node(NodeKind::Field, "used");
        used->ty = ty_usize;
        data->next = cap;
        cap->next = used;

        Node* over = syn_node(NodeKind::Func, "over");
        over->flags |= FlagStatic;
        Node* bufp = syn_node(NodeKind::Param, "buffer");
        bufp->ty = intern_sp(ty_u8, false);
        over->right = bufp;

        Node* fb = syn_node(NodeKind::Struct, "FixedBuffer");
        fb->body = data;
        used->next = over;
        ty_fixed = make_type(TypeKind::Struct, "FixedBuffer");
        ty_fixed->decl = fb;
        fb->ty = ty_fixed;
        over->ty = ty_fixed;
        fixed_decl = fb;

        Node* alloc_g = syn_node(NodeKind::Global, "allocator");
        alloc_g->flags |= FlagThreadLocal;
        alloc_g->ty = ty_alloc;
        Node* heap_g = syn_node(NodeKind::Const, "heap");
        heap_g->ty = ty_alloc;
        Node* exh = syn_node(NodeKind::Const, "exhausted");
        exh->ty = ty_i32;
        alloc_g->next = heap_g;
        heap_g->next = exh;

        Node* mem = syn_node(NodeKind::Module, "memory");
        mem->body = alloc_g;
        Type* mt = make_type(TypeKind::Module, "memory");
        mt->decl = mem;
        memory_mod = mem;

        bind("Allocator", ty_alloc, false, nullptr);
        bind("CAllocator", ty_alloc, false, nullptr);
        bind("FixedBuffer", ty_fixed, false, fb);
        bind("memory", mt, false, mem);

        if (ty_fmt == nullptr) {
            ty_fmt = make_type(TypeKind::Fmt, "fmt");
        }
        Node* wr = syn_node(NodeKind::Interface, "Writer");
        Node* wfn = syn_node(NodeKind::Func, "write");
        wfn->flags |= FlagMutating | FlagFallible;
        Node* wpar = syn_node(NodeKind::Param, "bytes");
        wpar->ty = intern_sp(ty_u8, true);
        wfn->right = wpar;
        wfn->ty = ty_usize;
        wr->body = wfn;
        ty_writer = intern_iface(wr, false);
        wr->ty = ty_writer;
        bind("Writer", ty_writer, false, wr);

        Node* loc = syn_node(NodeKind::Struct, "Location");
        Node* ffile = syn_node(NodeKind::Field, "file");
        ffile->ty = ty_str;
        Node* fline = syn_node(NodeKind::Field, "line");
        fline->ty = ty_u32;
        Node* ffun = syn_node(NodeKind::Field, "function");
        ffun->ty = ty_str;
        ffile->next = fline;
        fline->next = ffun;
        loc->body = ffile;
        ty_location = make_type(TypeKind::Struct, "Location");
        ty_location->decl = loc;
        loc->ty = ty_location;
        bind("Location", ty_location, false, loc);

        Node* ord = syn_node(NodeKind::Enum, "Ordering");
        Type* ot = make_type(TypeKind::Enum, "Ordering");
        ot->decl = ord;
        ot->elem = ty_i32;
        ord->ty = ot;
        const char* onames[] = {"relaxed", "acquire", "release", "acq_rel", "seq_cst", "signal"};
        const int ovals[] = {0, 2, 3, 4, 5, 6};
        Node* oprev = nullptr;
        for (int i = 0; i < 6; i++) {
            Node* cse = syn_node(NodeKind::EnumCase, onames[i]);
            cse->ty = ot;
            Node* lit = arena->make<Node>();
            lit->kind = NodeKind::Literal;
            lit->op = TokenKind::IntLit;
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", ovals[i]);
            lit->text = keep(buf);
            cse->left = lit;
            if (oprev != nullptr) {
                oprev->next = cse;
            } else {
                ord->body = cse;
            }
            oprev = cse;
        }
        bind("Ordering", ot, false, ord);

        Node* fence = syn_node(NodeKind::Func, "fence");
        Node* oarg = syn_node(NodeKind::Param, "order");
        oarg->ty = ot;
        fence->right = oarg;
        fence->ty = t_unit();
        Node* amod = syn_node(NodeKind::Module, "atomic");
        amod->body = fence;
        Type* amt = make_type(TypeKind::Module, "atomic");
        amt->decl = amod;
        bind("atomic", amt, false, amod);

        Node* hid = syn_node(NodeKind::Field, "id");
        hid->ty = ty_usize;
        Node* join = syn_node(NodeKind::Func, "join");
        join->flags |= FlagFallible | FlagPub;
        join->ty = t_unit();
        Node* det = syn_node(NodeKind::Func, "detach");
        det->flags |= FlagPub;
        det->ty = t_unit();
        hid->next = join;
        join->next = det;
        Node* hs = syn_node(NodeKind::Struct, "Handle");
        hs->body = hid;
        Type* ht = make_type(TypeKind::Struct, "Handle");
        ht->decl = hs;
        hs->ty = ht;

        Node* spawn = syn_node(NodeKind::Func, "spawn");
        spawn->flags |= FlagFallible | FlagPub;
        Node* sp_entry = syn_node(NodeKind::Param, "entry");
        sp_entry->ty = intern_ptr(ty_void, false, false, false);
        Node* sp_ctx = syn_node(NodeKind::Param, "context");
        sp_ctx->ty = intern_ptr(ty_void, false, false, false);
        sp_entry->next = sp_ctx;
        spawn->right = sp_entry;
        spawn->ty = ht;

        Node* cur = syn_node(NodeKind::Func, "current");
        cur->flags |= FlagPub;
        cur->ty = ht;
        Node* pause = syn_node(NodeKind::Func, "pause");
        pause->flags |= FlagPub;
        pause->ty = t_unit();
        Node* yield = syn_node(NodeKind::Func, "yield");
        yield->flags |= FlagPub;
        yield->ty = t_unit();
        Node* slp = syn_node(NodeKind::Func, "sleep");
        slp->flags |= FlagPub;
        Node* ms = syn_node(NodeKind::Param, "milliseconds");
        ms->ty = ty_usize;
        slp->right = ms;
        slp->ty = t_unit();
        spawn->next = cur;
        cur->next = pause;
        pause->next = yield;
        yield->next = slp;
        Node* hpub = syn_node(NodeKind::Struct, "Handle");
        hpub->ty = ht;
        hpub->body = hs->body;
        hpub->flags |= FlagPub;
        slp->next = hpub;
        Node* tmod = syn_node(NodeKind::Module, "thread");
        tmod->body = spawn;
        Type* tt = make_type(TypeKind::Module, "thread");
        tt->decl = tmod;
        bind("thread", tt, false, tmod);

        Node* m_lock = syn_method("lock", t_unit(), true);
        Node* m_unlock = syn_method("unlock", t_unit(), true);
        Node* m_try = syn_method("try", ty_bool, true);
        m_lock->next = m_unlock;
        m_unlock->next = m_try;
        Node* mu = syn_node(NodeKind::Struct, "Mutex");
        mu->body = m_lock;
        Type* tmu = make_type(TypeKind::Struct, "Mutex");
        tmu->decl = mu;
        mu->ty = tmu;

        Node* c_wait = syn_method("wait", t_unit(), true);
        Node* c_mp = syn_node(NodeKind::Param, "mutex");
        c_mp->ty = intern_ptr(tmu, false, false, false);
        c_wait->right = c_mp;
        Node* c_sig = syn_method("signal", t_unit(), true);
        Node* c_bc = syn_method("broadcast", t_unit(), true);
        c_wait->next = c_sig;
        c_sig->next = c_bc;
        Node* cv = syn_node(NodeKind::Struct, "Condition");
        cv->body = c_wait;
        Type* tcv = make_type(TypeKind::Struct, "Condition");
        tcv->decl = cv;
        cv->ty = tcv;

        Node* o_run = syn_method("run", t_unit(), true);
        Node* on = syn_node(NodeKind::Struct, "Once");
        on->body = o_run;
        Type* ton = make_type(TypeKind::Struct, "Once");
        ton->decl = on;
        on->ty = ton;

        Node* s_acq = syn_method("acquire", t_unit(), true);
        Node* s_rel = syn_method("release", t_unit(), true);
        s_acq->next = s_rel;
        Node* sm = syn_node(NodeKind::Struct, "Semaphore");
        sm->body = s_acq;
        Type* tsm = make_type(TypeKind::Struct, "Semaphore");
        tsm->decl = sm;
        sm->ty = tsm;

        mu->next = cv;
        cv->next = on;
        on->next = sm;
        Node* smod = syn_node(NodeKind::Module, "sync");
        smod->body = mu;
        Type* st = make_type(TypeKind::Module, "sync");
        st->decl = smod;
        bind("sync", st, false, smod);
    }

auto Checker::mark_local(Node* n) -> void {
        if (n != nullptr) {
            n->flags |= FlagLocal;
        }
    }

auto Checker::is_local(Node* n) -> bool {
        return n != nullptr && (n->flags & FlagLocal) != 0;
    }

auto Checker::fail(Span span, const char* code, const string& message) -> void {
        diag->add(code, path, span, message);
    }

auto Checker::fail_n(Node* n, const char* code, const string& message) -> void {
        fail(n != nullptr ? n->span : Span{}, code, message);
    }

auto Checker::pop_scope() -> void {
        while (!scope.empty() && scope.back().depth == depth) {
            scope.pop_back();
        }
        depth--;
    }

auto Checker::lookup(string_view name) -> Binding* {
        for (int i = static_cast<int>(scope.size()) - 1; i >= 0; i--) {
            if (scope[static_cast<size_t>(i)].name == name) {
                return &scope[static_cast<size_t>(i)];
            }
        }
        return nullptr;
    }

auto Checker::bind(string_view name, Type* type, bool mut, Node* decl, Node* import_src) -> bool {
        if (lookup(name) != nullptr) {
            fail_n(decl, "lucb.check.shadow", "this name is already in scope");
            return false;
        }
        Binding b;
        b.name = name;
        b.type = type;
        b.mut = mut;
        b.decl = decl;
        b.import_src = import_src;
        b.depth = depth;
        scope.push_back(b);
        return true;
    }

auto Checker::mark_import(Binding* b) -> void {
        if (b != nullptr && b->import_src != nullptr) {
            b->import_src->flags |= FlagImportUsed;
        }
    }

auto Checker::pub_member(Node* mod, string_view name) -> Node* {
        if (mod == nullptr) {
            return nullptr;
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->text == name && (d->flags & FlagPub) != 0 &&
                (d->kind == NodeKind::Func || d->kind == NodeKind::Struct ||
                 d->kind == NodeKind::Enum || d->kind == NodeKind::Union ||
                 d->kind == NodeKind::Interface || d->kind == NodeKind::Const ||
                 d->kind == NodeKind::Global)) {
                return d;
            }
        }
        return nullptr;
    }

auto Checker::decl_type(Node* d) -> Type* {
        if (d == nullptr) {
            return t_error();
        }
        if (d->ty != nullptr) {
            return d->ty;
        }
        if (d->kind == NodeKind::Func) {
            return t_unit();
        }
        return t_error();
    }

auto Checker::last_component(string_view path) -> string {
        size_t dot = path.rfind('.');
        if (dot == string_view::npos) {
            return string(path);
        }
        return string(path.substr(dot + 1));
    }

auto Checker::set_from_local(string_view name, bool from_local) -> void {
        Binding* b = lookup(name);
        if (b != nullptr) {
            b->from_local = from_local;
        }
    }

auto Checker::is_core_name(string_view name) -> bool {
        return name == "print" || name == "assert" || name == "discard" || name == "error" ||
               name == "trap" || name == "hash" || name == "format" || name == "location" ||
               name == "sizeof" || name == "alignof" || name == "offsetof" || name == "hex" ||
               named_scalar(name) != nullptr || name == "f16" || name == "cstr" || name == "fmt" ||
               name == "FixedBuffer" || name == "memory" || name == "fmt" || name == "format" ||
               name == "location" || name == "Writer" || name == "Location";
    }

auto Checker::const_u64(Node* n, uint64_t* out) -> bool {
        if (n == nullptr || out == nullptr) {
            return false;
        }
        if (n->kind == NodeKind::Literal && n->op == TokenKind::IntLit) {
            ParsedInt p = parse_int_literal(n->text);
            if (!p.ok) {
                return false;
            }
            *out = p.value;
            return true;
        }
        if (n->kind == NodeKind::Group) {
            return const_u64(n->left, out);
        }
        if (n->kind == NodeKind::Call && n->left != nullptr && n->left->kind == NodeKind::Name &&
            n->left->text == "sizeof") {
            if (n->body == nullptr || n->body->left == nullptr) {
                return false;
            }
            Type* t = type_from_expr_or_name(n->body->left);
            *out = static_cast<uint64_t>(type_size(t));
            return true;
        }
        return false;
    }

auto Checker::resolve_type(Node* n) -> Type* {
        if (n == nullptr) {
            return t_unit();
        }
        if (n->ty != nullptr) {
            return n->ty;
        }
        if ((n->flags & k_type_flags_unsupported) != 0) {
            fail_n(n, "lucb.check.unsupported", "this type is not in this slice");
            n->ty = t_error();
            return n->ty;
        }
        if (n->flags & FlagTupleType) {
            int nfields = 0;
            for (Node* a = n->body; a != nullptr; a = a->next) {
                nfields++;
            }
            Type* elems[8];
            if (nfields < 2 || nfields > 8) {
                fail_n(n, "lucb.check.type", "a tuple needs between 2 and 8 elements");
                n->ty = t_error();
                return n->ty;
            }
            int i = 0;
            for (Node* a = n->body; a != nullptr; a = a->next) {
                elems[i++] = resolve_type(a);
            }
            n->ty = intern_tup(elems, nfields);
            return n->ty;
        }
        if (n->flags & FlagAtomic) {
            Type* inner = resolve_type(n->left);
            if (!atomic_ok(inner)) {
                fail_n(n, "lucb.check.type", "`@T` needs an integer, `bool`, or pointer of at most pointer width");
                n->ty = t_error();
                return n->ty;
            }
            n->ty = intern_atomic(inner);
            return n->ty;
        }
        if (n->flags & FlagFallible) {
            Type* inner = t_unit();
            if (n->left != nullptr) {
                inner = resolve_type(n->left);
            } else if (n->text == "unit" || n->text.empty()) {
                inner = t_unit();
            } else {
                inner = named_scalar(n->text);
                if (inner == nullptr) {
                    inner = t_unit();
                }
            }
            n->ty = intern_fail(inner);
            return n->ty;
        }
        if (n->flags & FlagOptional) {
            Type* inner = resolve_type(n->left);
            n->ty = intern_opt(inner);
            return n->ty;
        }
        if (n->flags & FlagStar) {
            Type* elem = resolve_type(n->left);
            if (elem->kind == TypeKind::Void ||
                (n->left != nullptr && (n->left->flags & FlagVoid))) {
                elem = ty_void;
            }
            n->ty = intern_ptr(elem, (n->flags & FlagConst) != 0, (n->flags & FlagVolatile) != 0,
                               false);
            return n->ty;
        }
        if (n->flags & FlagSpan) {
            Type* elem = resolve_type(n->left);
            n->ty = intern_sp(elem, (n->flags & FlagConst) != 0);
            return n->ty;
        }
        if (n->flags & FlagArray) {
            Type* elem = resolve_type(n->left);
            uint64_t len = 0;
            if (!const_u64(n->right, &len) || len == 0) {
                fail_n(n, "lucb.check.type", "array length must be a positive constant");
                n->ty = t_error();
                return n->ty;
            }
            n->ty = intern_arr(elem, len);
            return n->ty;
        }
        if (n->flags & FlagVoid) {
            n->ty = ty_void;
            return n->ty;
        }
        if (n->left != nullptr && n->text.empty() && n->flags == 0) {
            n->ty = resolve_type(n->left);
            return n->ty;
        }
        if (n->text == "f16") {
            fail_n(n, "lucb.check.unsupported", "`f16` is not in this slice");
            n->ty = t_error();
            return n->ty;
        }
        Type* named = named_scalar(n->text);
        if (named != nullptr) {
            n->ty = named;
            return n->ty;
        }
        Type* calias = c_alias(n->text);
        if (calias != nullptr) {
            n->ty = calias;
            return n->ty;
        }
        Binding* b = lookup(n->text);
        if (b != nullptr && b->type != nullptr) {
            bool type_bind = b->decl == nullptr || b->decl->kind == NodeKind::Struct ||
                             b->decl->kind == NodeKind::Enum || b->decl->kind == NodeKind::Union ||
                             b->decl->kind == NodeKind::GenericParam ||
                             b->decl->kind == NodeKind::Interface ||
                             b->decl->kind == NodeKind::ExternType ||
                             b->decl->kind == NodeKind::ExternStruct ||
                             b->decl->kind == NodeKind::ExternUnion ||
                             b->type->kind == TypeKind::Allocator ||
                             b->type->kind == TypeKind::Param ||
                             b->type->kind == TypeKind::Interface;
            if (type_bind) {
                Type* t = b->type;
                if (n->body != nullptr) {
                    if (b->decl == nullptr || !is_generic_decl(b->decl)) {
                        fail_n(n, "lucb.check.type",
                               "`" + string(n->text) + "` does not take type arguments");
                    } else {
                        vector<Type*> targs;
                        for (Node* a = n->body; a != nullptr; a = a->next) {
                            targs.push_back(resolve_type(a));
                        }
                        if (static_cast<int>(targs.size()) != count_generics(b->decl)) {
                            fail_n(n, "lucb.check.type", "wrong number of type arguments");
                            t = t_error();
                        } else {
                            t = instantiate_struct(b->decl, targs, n);
                        }
                    }
                } else if (b->decl != nullptr && is_generic_decl(b->decl) &&
                           !checking_generic_template) {
                    fail_n(n, "lucb.check.type",
                           "`" + string(n->text) + "` needs type arguments");
                }
                n->ty = t;
                n->resolved = b->decl;
                return n->ty;
            }
        }
        size_t dot = n->text.find('.');
        if (dot != string_view::npos && dot > 0 && dot + 1 < n->text.size()) {
            Binding* mb = lookup(n->text.substr(0, dot));
            if (mb != nullptr && mb->type != nullptr && mb->type->kind == TypeKind::Module) {
                Node* d = pub_member(mb->type->decl, n->text.substr(dot + 1));
                if (d != nullptr) {
                    Type* t = decl_type(d);
                    n->ty = t;
                    n->resolved = d;
                    return n->ty;
                }
            }
        }
        fail_n(n, "lucb.check.type", "unknown type `" + string(n->text) + "`");
        n->ty = t_error();
        return n->ty;
    }

auto Checker::struct_member(Node* st, string_view name, NodeKind kind) -> Node* {
        if (st == nullptr) {
            return nullptr;
        }
        for (Node* m = st->body; m != nullptr; m = m->next) {
            if (m->kind == kind && m->text == name) {
                return m;
            }
        }
        return nullptr;
    }

auto Checker::enum_case(Node* en, string_view name) -> Node* {
        return struct_member(en, name, NodeKind::EnumCase);
    }

auto Checker::enum_tag(Node* en, Node* cse) -> int {
        int i = 0;
        if (en == nullptr) {
            return -1;
        }
        for (Node* m = en->body; m != nullptr; m = m->next) {
            if (m->kind != NodeKind::EnumCase) {
                continue;
            }
            if (m == cse) {
                return i;
            }
            i++;
        }
        return -1;
    }

auto Checker::is_c_repr(Type* t) -> bool {
        if (t == nullptr) {
            return true;
        }
        if (t->kind == TypeKind::Unit || t->kind == TypeKind::Never || t->kind == TypeKind::Void) {
            return true;
        }
        if (is_int(t) || is_float(t) || t->kind == TypeKind::Bool || t->kind == TypeKind::Char ||
            t->kind == TypeKind::CStr) {
            return true;
        }
        if (is_ptr(t) || t->kind == TypeKind::Struct || t->kind == TypeKind::Union ||
            is_int_enum(t)) {
            return true;
        }
        return false;
    }

auto Checker::check_foreign_sig(Node* fn, bool exported) -> void {
        if (is_generic_decl(fn)) {
            fail_n(fn, "lucb.check.unsupported", "a generic cannot be `extern` or `export`");
            return;
        }
        if (exported && (fn->flags & FlagFallible) != 0) {
            fail_n(fn, "lucb.check.unsupported", "a fallible `export` is not in this slice");
        }
        const char* where = exported ? "an `export` signature cannot use `str`; write `cstr`"
                                     : "an `extern` signature cannot use `str`; write `cstr`";
        for (Node* p = fn->right; p != nullptr; p = p->next) {
            if (p->flags & FlagVariadic) {
                continue;
            }
            if (p->flags & FlagOut) {
                fail_n(p, "lucb.check.unsupported", "`out` parameters are not in this slice");
            }
            if (p->ty != nullptr && p->ty->kind == TypeKind::Str) {
                fail_n(p, "lucb.check.type", where);
            } else if (p->ty != nullptr && !is_c_repr(p->ty)) {
                fail_n(p, "lucb.check.type", "this type is not C-representable");
            }
        }
        if (fn->ty != nullptr && fn->ty->kind == TypeKind::Str) {
            fail_n(fn, "lucb.check.type", where);
        } else if (fn->ty != nullptr && !is_c_repr(fn->ty)) {
            fail_n(fn, "lucb.check.type", "this type is not C-representable");
        }
    }

auto Checker::check_params(Node* fn) -> void {
        for (Node* p = fn->right; p != nullptr; p = p->next) {
            if (p->text == "self") {
                fail_n(p, "lucb.check.self",
                       "do not write `self` as a parameter; methods take it implicitly");
            }
            p->ty = resolve_type(p->type);
            bind(p->text, p->ty, false, p);
        }
    }

auto Checker::check_func(Node* fn, Node* owner) -> void {
        bool generic = is_generic_decl(fn);
        bool saved_generic = checking_generic_template;
        if (generic) {
            checking_generic_template = true;
            push_scope();
            bind_generic_params(fn);
        }
        Type* result = t_unit();
        if (fn->type != nullptr) {
            result = resolve_type(fn->type);
        }
        if (is_fail(result)) {
            fn->flags |= FlagFallible;
            result = result->elem != nullptr ? result->elem : t_unit();
        }
        fn->ty = result;
        Node* saved_fn = current_fn;
        Node* saved_st = current_struct;
        Type* saved_ret = return_type;
        bool saved_fail = fallible_fn;
        current_fn = fn;
        current_struct = owner;
        return_type = result;
        fallible_fn = (fn->flags & FlagFallible) != 0;
        push_scope();
        if (owner != nullptr && (fn->flags & FlagStatic) == 0) {
            bool mut = (fn->flags & FlagMutating) != 0;
            bind("self", owner->ty, mut, owner);
        }
        check_params(fn);
        check_stmt(fn->body);
        if (!type_eq(result, t_unit()) && !always_returns(fn->body)) {
            fail_n(fn, "lucb.check.return", "this function must return a value on every path");
        }
        pop_scope();
        current_fn = saved_fn;
        current_struct = saved_st;
        return_type = saved_ret;
        fallible_fn = saved_fail;
        if (generic) {
            pop_scope();
            checking_generic_template = saved_generic;
        }
    }

auto Checker::check_struct(Node* st) -> void {
        bool generic = is_generic_decl(st);
        bool saved_generic = checking_generic_template;
        if (generic) {
            checking_generic_template = true;
            push_scope();
            bind_generic_params(st);
            for (Node* m = st->body; m != nullptr; m = m->next) {
                if (m->kind == NodeKind::Field) {
                    m->ty = resolve_type(m->type);
                }
            }
        }
        for (Node* m = st->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::Field) {
                if (struct_member(st, m->text, NodeKind::Func) != nullptr) {
                    fail_n(m, "lucb.check.shadow", "a method already uses this name");
                }
                for (Node* o = st->body; o != m; o = o->next) {
                    if (o->kind == NodeKind::Field && o->text == m->text) {
                        fail_n(m, "lucb.check.shadow", "duplicate field");
                    }
                }
            }
        }
        for (Node* m = st->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::Func) {
                check_func(m, st);
            } else if (m->kind != NodeKind::Field) {
                fail_n(m, "lucb.check.unsupported", "this member is not in the scalar core yet");
            }
        }
        if (generic) {
            pop_scope();
            checking_generic_template = saved_generic;
        }
        check_implements(st);
    }

auto Checker::sig_matches(Node* impl, Node* req) -> bool {
        if (impl == nullptr || req == nullptr) {
            return false;
        }
        Node* ip = impl->right;
        Node* rp = req->right;
        while (ip != nullptr && rp != nullptr) {
            if (!type_eq(ip->ty, rp->ty) && !can_widen(ip->ty, rp->ty)) {
                return false;
            }
            ip = ip->next;
            rp = rp->next;
        }
        if (ip != nullptr || rp != nullptr) {
            return false;
        }
        Type* ir = impl->ty != nullptr ? impl->ty : t_unit();
        Type* rr = req->ty != nullptr ? req->ty : t_unit();
        if ((req->flags & FlagFallible) != 0) {
            if ((impl->flags & FlagFallible) != 0) {
                return type_eq(ir, rr);
            }
            return type_eq(ir, rr);
        }
        if ((impl->flags & FlagFallible) != 0) {
            return false;
        }
        return type_eq(ir, rr);
    }

auto Checker::check_implements(Node* st) -> void {
        for (Node* t = st->right; t != nullptr; t = t->next) {
            Type* iface = resolve_type(t);
            if (iface == nullptr || iface->kind != TypeKind::Interface || iface->decl == nullptr) {
                fail_n(t, "lucb.check.type", "`implements` needs an interface");
                continue;
            }
            t->ty = iface;
            for (Node* req = iface->decl->body; req != nullptr; req = req->next) {
                if (req->kind != NodeKind::Func) {
                    continue;
                }
                Node* impl = struct_member(st, req->text, NodeKind::Func);
                if (impl == nullptr) {
                    fail_n(st, "lucb.check.type",
                           "`" + string(st->text) + "` is missing `" + string(req->text) +
                               "` for `" + string(iface->decl->text) + "`");
                    continue;
                }
                if ((req->flags & FlagMutating) != 0 && (impl->flags & FlagMutating) == 0) {
                    fail_n(impl, "lucb.check.mut",
                           "`" + string(req->text) + "` must be `mutating`");
                }
                if (!sig_matches(impl, req)) {
                    fail_n(impl, "lucb.check.type",
                           "`" + string(impl->text) + "` does not match `" +
                               string(iface->decl->text) + "." + string(req->text) + "`");
                }
            }
        }
    }

auto Checker::check_interface(Node* iface) -> void {
        if (is_generic_decl(iface)) {
            fail_n(iface, "lucb.check.unsupported", "generic interfaces are not in this slice");
            return;
        }
        for (Node* m = iface->body; m != nullptr; m = m->next) {
            if (m->kind != NodeKind::Func) {
                fail_n(m, "lucb.check.unsupported", "an interface may only declare methods");
                continue;
            }
            if (is_generic_decl(m)) {
                fail_n(m, "lucb.check.unsupported", "generic methods are not in this slice");
            }
            resolve_sig(m);
        }
    }

auto Checker::collect_type_decl(Node* d, TypeKind kind) -> void {
        if (lookup(d->text) != nullptr) {
            fail_n(d, "lucb.check.shadow", "this name is already in scope");
            return;
        }
        Type* t = make_type(kind, d->text);
        t->decl = d;
        t->packed = (d->flags & FlagPacked) != 0;
        uint64_t al = 0;
        if (const_u64(d->type, &al)) {
            t->align_to = static_cast<int>(al);
        }
        d->ty = t;
        if (kind == TypeKind::Interface) {
            interned.push_back(t);
        }
        bind(d->text, t, false, d);
    }

auto Checker::collect_module(Node* mod) -> void {
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Struct) {
                collect_type_decl(d, TypeKind::Struct);
            } else if (d->kind == NodeKind::Enum) {
                collect_type_decl(d, TypeKind::Enum);
            } else if (d->kind == NodeKind::Union) {
                collect_type_decl(d, TypeKind::Union);
            } else if (d->kind == NodeKind::Interface) {
                collect_type_decl(d, TypeKind::Interface);
            } else if (d->kind == NodeKind::Func || d->kind == NodeKind::ExternFunc) {
                if (is_core_name(d->text)) {
                    fail_n(d, "lucb.check.shadow", "this name belongs to the language");
                }
                if (lookup(d->text) != nullptr) {
                    fail_n(d, "lucb.check.shadow", "this name is already in scope");
                    continue;
                }
                bind(d->text, t_unit(), false, d);
            } else if (d->kind == NodeKind::ExternType) {
                Type* t = nullptr;
                if (d->type != nullptr) {
                    t = resolve_type(d->type);
                } else {
                    t = make_type(TypeKind::Pointer, d->text);
                    t->elem = ty_void;
                    t->decl = d;
                }
                d->ty = t;
                bind(d->text, t, false, d);
            } else if (d->kind == NodeKind::ExternVar) {
                bind(d->text, t_error(), true, d);
            } else if (d->kind == NodeKind::ExternStruct) {
                collect_type_decl(d, TypeKind::Struct);
            } else if (d->kind == NodeKind::ExternUnion) {
                collect_type_decl(d, TypeKind::Union);
            } else if (d->kind == NodeKind::Global) {
                bind(d->text, t_error(), true, d);
            } else if (d->kind == NodeKind::Const) {
                bind(d->text, t_error(), false, d);
            } else if (d->kind == NodeKind::Import || d->kind == NodeKind::FromImport ||
                       d->kind == NodeKind::Test || d->kind == NodeKind::Assert ||
                       d->kind == NodeKind::Asm) {
                continue;
            } else {
                fail_n(d, "lucb.check.unsupported",
                       "this declaration is not in this slice");
            }
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if ((d->kind == NodeKind::Struct || d->kind == NodeKind::Union ||
                 d->kind == NodeKind::ExternStruct || d->kind == NodeKind::ExternUnion) &&
                !is_generic_decl(d)) {
                for (Node* m = d->body; m != nullptr; m = m->next) {
                    if (m->kind == NodeKind::Field) {
                        m->ty = resolve_type(m->type);
                    }
                }
            } else if (d->kind == NodeKind::Enum) {
                if (d->right != nullptr && d->right->kind == NodeKind::Type) {
                    Type* backing = resolve_type(d->right);
                    if (!is_int(backing)) {
                        fail_n(d, "lucb.check.type", "an integer-backed enum needs an integer type");
                        backing = ty_u32;
                    }
                    if (d->ty != nullptr) {
                        d->ty->elem = backing;
                    }
                }
                for (Node* m = d->body; m != nullptr; m = m->next) {
                    if (m->kind == NodeKind::EnumCase) {
                        if (m->text == "none") {
                            fail_n(m, "lucb.check.type", "a case may not be named `none`");
                        }
                        m->ty = d->ty;
                        for (Node* p = m->body; p != nullptr; p = p->next) {
                            p->ty = resolve_type(p->type);
                        }
                    }
                }
            } else if (d->kind == NodeKind::ExternVar) {
                Type* t = d->type != nullptr ? resolve_type(d->type) : t_error();
                d->ty = t;
                Binding* b = lookup(d->text);
                if (b != nullptr && b->decl == d) {
                    b->type = t;
                }
            } else if (d->kind == NodeKind::Global || d->kind == NodeKind::Const) {
                Type* t = d->type != nullptr ? resolve_type(d->type) : nullptr;
                if (d->left != nullptr) {
                    Type* init = check_expr(d->left, t);
                    if (t == nullptr) {
                        if (init != nullptr && init->kind == TypeKind::UntypedInt) {
                            init = coerce(d->left, init, t_i64());
                            d->left->ty = init;
                        }
                        t = init;
                    }
                } else if (t == nullptr) {
                    fail_n(d, "lucb.check.type", "this binding needs a type or an initialiser");
                    t = t_error();
                } else if (d->kind == NodeKind::Global && !is_zeroable(t) &&
                           (d->flags & FlagUninit) == 0) {
                    fail_n(d, "lucb.check.type", "this type has no zero value; write an initialiser");
                }
                d->ty = t;
                Binding* b = lookup(d->text);
                if (b != nullptr && b->decl == d) {
                    b->type = t;
                }
            }
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Func || d->kind == NodeKind::ExternFunc) {
                resolve_sig(d);
                Binding* b = lookup(d->text);
                if (b != nullptr && b->decl == d) {
                    b->type = d->ty;
                }
                if (d->kind == NodeKind::ExternFunc) {
                    check_foreign_sig(d, false);
                } else if ((d->flags & FlagExport) != 0) {
                    check_foreign_sig(d, true);
                }
            } else if (d->kind == NodeKind::Interface) {
                for (Node* m = d->body; m != nullptr; m = m->next) {
                    if (m->kind == NodeKind::Func) {
                        resolve_sig(m);
                    }
                }
            } else if (d->kind == NodeKind::Struct || d->kind == NodeKind::Enum ||
                       d->kind == NodeKind::Union) {
                bool g = is_generic_decl(d);
                if (g) {
                    push_scope();
                    bind_generic_params(d);
                }
                for (Node* m = d->body; m != nullptr; m = m->next) {
                    if (m->kind == NodeKind::Func) {
                        resolve_sig(m);
                        if ((m->flags & FlagExport) != 0) {
                            check_foreign_sig(m, true);
                        }
                    }
                }
                if (g) {
                    pop_scope();
                }
            }
        }
    }

auto Checker::check_enum(Node* en) -> void {
        bool saw_payload = false;
        bool saw_value = false;
        for (Node* m = en->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::EnumCase) {
                if (m->body != nullptr) {
                    saw_payload = true;
                }
                if (m->left != nullptr) {
                    saw_value = true;
                    uint64_t v = 0;
                    if (!const_u64(m->left, &v)) {
                        fail_n(m, "lucb.check.type", "enum case value must be a constant");
                    }
                }
                for (Node* o = en->body; o != m; o = o->next) {
                    if (o->kind == NodeKind::EnumCase && o->text == m->text) {
                        fail_n(m, "lucb.check.shadow", "duplicate case");
                    }
                }
            } else if (m->kind == NodeKind::Func) {
                check_func(m, en);
            }
        }
        if (saw_payload && (saw_value || is_int_enum(en->ty))) {
            fail_n(en, "lucb.check.type", "payload cases cannot mix with integer-backed values");
        }
    }

auto Checker::check_union(Node* un) -> void {
        for (Node* m = un->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::Field) {
                for (Node* o = un->body; o != m; o = o->next) {
                    if (o->kind == NodeKind::Field && o->text == m->text) {
                        fail_n(m, "lucb.check.shadow", "duplicate member");
                    }
                }
            } else if (m->kind == NodeKind::Func) {
                check_func(m, un);
            }
        }
    }

auto Checker::resolve_sig(Node* fn) -> void {
        bool generic = is_generic_decl(fn);
        if (generic) {
            push_scope();
            bind_generic_params(fn);
        }
        Type* result = t_unit();
        if (fn->type != nullptr) {
            result = resolve_type(fn->type);
        }
        if (is_fail(result)) {
            fn->flags |= FlagFallible;
            result = result->elem != nullptr ? result->elem : t_unit();
        }
        fn->ty = result;
        for (Node* p = fn->right; p != nullptr; p = p->next) {
            if (p->flags & FlagVariadic) {
                continue;
            }
            p->ty = resolve_type(p->type);
        }
        if (generic) {
            pop_scope();
        }
    }

auto Checker::bind_imports(Node* mod) -> void {
        vector<string_view> seen;
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind != NodeKind::Import && d->kind != NodeKind::FromImport) {
                continue;
            }
            for (size_t i = 0; i < seen.size(); i++) {
                if (seen[i] == d->text) {
                    fail_n(d, "lucb.check.import", "duplicate import");
                }
            }
            seen.push_back(d->text);
            Node* other = d->resolved;
            if (other == nullptr || other->kind != NodeKind::Module) {
                fail_n(d, "lucb.check.import", "cannot find module `" + string(d->text) + "`");
                continue;
            }
            if (d->kind == NodeKind::Import) {
                string alias = d->left != nullptr && !d->left->text.empty()
                                   ? string(d->left->text)
                                   : last_component(d->text);
                Type* mt = make_type(TypeKind::Module, d->text);
                mt->decl = other;
                bind(keep(alias), mt, false, other, d);
            } else {
                for (Node* nm = d->body; nm != nullptr; nm = nm->next) {
                    Node* p = pub_member(other, nm->text);
                    if (p == nullptr) {
                        fail_n(nm, "lucb.check.import",
                               "no public `" + string(nm->text) + "` in `" + string(d->text) + "`");
                        continue;
                    }
                    bool mut = p->kind == NodeKind::Global;
                    bind(nm->text, decl_type(p), mut, p, nm);
                }
            }
        }
    }

auto Checker::check_unused_imports(Node* mod) -> void {
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Import) {
                if ((d->flags & FlagImportUsed) == 0) {
                    fail_n(d, "lucb.check.import", "unused import `" + string(d->text) + "`");
                }
            } else if (d->kind == NodeKind::FromImport) {
                for (Node* nm = d->body; nm != nullptr; nm = nm->next) {
                    if ((nm->flags & FlagImportUsed) == 0) {
                        fail_n(nm, "lucb.check.import",
                               "unused import `" + string(nm->text) + "`");
                    }
                }
            }
        }
    }

auto Checker::check_test(Node* t) -> void {
        Node* saved_fn = current_fn;
        Type* saved_ret = return_type;
        bool saved_fail = fallible_fn;
        current_fn = t;
        return_type = t_unit();
        fallible_fn = true;
        push_scope();
        check_stmt(t->body);
        pop_scope();
        current_fn = saved_fn;
        return_type = saved_ret;
        fallible_fn = saved_fail;
    }

auto Checker::check_main(Node* fn) -> void {
        if ((fn->flags & FlagPub) == 0) {
            fail_n(fn, "lucb.check.type", "`main` must be `pub`");
        }
        int nparams = 0;
        for (Node* p = fn->right; p != nullptr; p = p->next) {
            nparams++;
        }
        if (nparams != 1) {
            fail_n(fn, "lucb.check.type", "`main` takes `arguments: str[]` or `cstr[]`");
        } else {
            Type* a = fn->right->ty;
            bool ok = is_span(a) && a->elem != nullptr &&
                      (a->elem->kind == TypeKind::Str || a->elem->kind == TypeKind::CStr);
            if (!ok) {
                fail_n(fn, "lucb.check.type", "`main` takes `arguments: str[]` or `cstr[]`");
            }
        }
        if (fn->ty == nullptr || fn->ty->kind != TypeKind::I32) {
            fail_n(fn, "lucb.check.type", "`main` returns `i32` or `i32!`");
        }
    }

auto Checker::check_module(Node* mod) -> void {
        current_module = mod;
        push_scope();
        bind_memory();
        bind_imports(mod);
        collect_module(mod);
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Interface) {
                check_interface(d);
            }
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Struct) {
                check_struct(d);
            } else if (d->kind == NodeKind::Enum) {
                check_enum(d);
            } else if (d->kind == NodeKind::Union) {
                check_union(d);
            }
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Func && is_generic_decl(d)) {
                check_func(d, nullptr);
            }
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Func && !is_generic_decl(d)) {
                check_func(d, nullptr);
                if (d->text == "main") {
                    check_main(d);
                }
            } else if (d->kind == NodeKind::Test) {
                check_test(d);
            } else if (d->kind == NodeKind::Assert) {
                check_assert(d);
            } else if (d->kind == NodeKind::Asm) {
                check_asm(d);
            }
        }
        vector<string> export_syms;
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Func && (d->flags & FlagExport) != 0) {
                string sym = string(d->text);
                for (size_t i = 0; i < export_syms.size(); i++) {
                    if (export_syms[i] == sym) {
                        fail_n(d, "lucb.check.shadow", "duplicate export symbol");
                    }
                }
                export_syms.push_back(sym);
            } else if (d->kind == NodeKind::Struct) {
                for (Node* m = d->body; m != nullptr; m = m->next) {
                    if (m->kind != NodeKind::Func || (m->flags & FlagExport) == 0) {
                        continue;
                    }
                    string sym = string(d->text) + "_" + string(m->text);
                    for (size_t i = 0; i < export_syms.size(); i++) {
                        if (export_syms[i] == sym) {
                            fail_n(m, "lucb.check.shadow", "duplicate export symbol");
                        }
                    }
                    export_syms.push_back(sym);
                }
            }
        }
        for (size_t i = 0; i < pending_clones.size(); i++) {
            append_node(&mod->body, pending_clones[i]);
        }
        pending_clones.clear();
        check_unused_imports(mod);
        pop_scope();
    }

bool check_module(Node* module, Arena& arena, DiagnosticBag& diagnostics, string_view path) {
    if (module == nullptr) {
        return false;
    }
    Checker c;
    c.arena = &arena;
    c.diag = &diagnostics;
    c.path = string(path);
    c.ty_error = c.make_type(TypeKind::Error, "");
    c.ty_never = c.make_type(TypeKind::Never, "never");
    c.ty_unit = c.make_type(TypeKind::Unit, "unit");
    c.ty_bool = c.make_type(TypeKind::Bool, "bool");
    c.ty_i8 = c.make_type(TypeKind::I8, "i8");
    c.ty_i16 = c.make_type(TypeKind::I16, "i16");
    c.ty_i32 = c.make_type(TypeKind::I32, "i32");
    c.ty_i64 = c.make_type(TypeKind::I64, "i64");
    c.ty_isize = c.make_type(TypeKind::Isize, "isize");
    c.ty_u8 = c.make_type(TypeKind::U8, "u8");
    c.ty_u16 = c.make_type(TypeKind::U16, "u16");
    c.ty_u32 = c.make_type(TypeKind::U32, "u32");
    c.ty_u64 = c.make_type(TypeKind::U64, "u64");
    c.ty_usize = c.make_type(TypeKind::Usize, "usize");
    c.ty_f32 = c.make_type(TypeKind::F32, "f32");
    c.ty_f64 = c.make_type(TypeKind::F64, "f64");
    c.ty_char = c.make_type(TypeKind::Char, "char");
    c.ty_str = c.make_type(TypeKind::Str, "str");
    c.ty_cstr = c.make_type(TypeKind::CStr, "cstr");
    c.ty_untyped = c.make_type(TypeKind::UntypedInt, "<integer>");
    c.ty_void = c.make_type(TypeKind::Void, "void");
    c.ty_err = c.make_type(TypeKind::ErrorVal, "Error");
    c.ty_alloc = c.make_type(TypeKind::Allocator, "Allocator");
    c.ty_fmt = c.make_type(TypeKind::Fmt, "fmt");
    c.check_module(module);
    return diagnostics.empty();
}

bool check_program(const vector<Node*>& modules, Arena& arena, DiagnosticBag& diagnostics,
                   string_view path) {
    if (modules.empty()) {
        return false;
    }
    Checker c;
    c.arena = &arena;
    c.diag = &diagnostics;
    c.path = string(path);
    c.ty_error = c.make_type(TypeKind::Error, "");
    c.ty_never = c.make_type(TypeKind::Never, "never");
    c.ty_unit = c.make_type(TypeKind::Unit, "unit");
    c.ty_bool = c.make_type(TypeKind::Bool, "bool");
    c.ty_i8 = c.make_type(TypeKind::I8, "i8");
    c.ty_i16 = c.make_type(TypeKind::I16, "i16");
    c.ty_i32 = c.make_type(TypeKind::I32, "i32");
    c.ty_i64 = c.make_type(TypeKind::I64, "i64");
    c.ty_isize = c.make_type(TypeKind::Isize, "isize");
    c.ty_u8 = c.make_type(TypeKind::U8, "u8");
    c.ty_u16 = c.make_type(TypeKind::U16, "u16");
    c.ty_u32 = c.make_type(TypeKind::U32, "u32");
    c.ty_u64 = c.make_type(TypeKind::U64, "u64");
    c.ty_usize = c.make_type(TypeKind::Usize, "usize");
    c.ty_f32 = c.make_type(TypeKind::F32, "f32");
    c.ty_f64 = c.make_type(TypeKind::F64, "f64");
    c.ty_char = c.make_type(TypeKind::Char, "char");
    c.ty_str = c.make_type(TypeKind::Str, "str");
    c.ty_cstr = c.make_type(TypeKind::CStr, "cstr");
    c.ty_untyped = c.make_type(TypeKind::UntypedInt, "<integer>");
    c.ty_void = c.make_type(TypeKind::Void, "void");
    c.ty_err = c.make_type(TypeKind::ErrorVal, "Error");
    c.ty_alloc = c.make_type(TypeKind::Allocator, "Allocator");
    c.ty_fmt = c.make_type(TypeKind::Fmt, "fmt");
    for (int i = static_cast<int>(modules.size()) - 1; i >= 0; i--) {
        if (modules[static_cast<size_t>(i)] != nullptr) {
            c.check_module(modules[static_cast<size_t>(i)]);
        }
    }
    return diagnostics.empty();
}

} // namespace lucb
