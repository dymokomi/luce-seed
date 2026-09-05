//==============================================================================================
//
//   check/generic - Generics: declaration-time checking and monomorphisation
//
//   DESCRIPTION:
//       A generic function or struct is checked once against its written constraints (base.md
//       §13), then instantiated per distinct type-argument list. Inference reads type
//       arguments from the call's arguments; `Comparable`, `Equatable`, and `Hashable` are
//       the derived bounds the compiler knows.
//
//==============================================================================================

#include "check/checker.h"

namespace lucb {

auto Checker::is_generic_decl(Node* n) -> bool {
    return n != nullptr && n->left != nullptr && n->left->kind == NodeKind::GenericParam;
}

auto Checker::count_generics(Node* n) -> int {
    int c = 0;
    if (n == nullptr) {
        return 0;
    }
    for (Node* g = n->left; g != nullptr; g = g->next) {
        if (g->kind == NodeKind::GenericParam) {
            c++;
        }
    }
    return c;
}

auto Checker::sanitize_ty(const string& s) -> string {
    string o;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            o += c;
        } else {
            o += '_';
        }
    }
    return o;
}

auto Checker::mangle_inst(string_view base, const vector<Type*>& args) -> string {
    string s(base);
    for (size_t i = 0; i < args.size(); i++) {
        s += "__";
        s += sanitize_ty(type_name(args[i]));
    }
    return s;
}

auto Checker::index_of_param(Node* generic, Type* p) -> int {
    int i = 0;
    if (generic == nullptr || p == nullptr) {
        return -1;
    }
    for (Node* g = generic->left; g != nullptr; g = g->next) {
        if (g->kind != NodeKind::GenericParam) {
            continue;
        }
        if (g->ty == p || g == p->decl) {
            return i;
        }
        i++;
    }
    return -1;
}

auto Checker::apply_bounds(Node* g, Type* t) -> void {
    if (g == nullptr || t == nullptr || g->type == nullptr) {
        return;
    }
    for (Node* b = g->type; b != nullptr; b = b->next) {
        string_view name = b->text;
        if (b->kind == NodeKind::Type && b->left != nullptr && name.empty()) {
            name = b->left->text;
        }
        if (name == "Comparable") {
            t->bounds |= BoundComparable;
        } else if (name == "Equatable") {
            t->bounds |= BoundEquatable;
        } else if (name == "Hashable") {
            t->bounds |= BoundHashable;
        } else if (!name.empty()) {
            Binding* ib = lookup(name);
            if (ib != nullptr && ib->type != nullptr && ib->type->kind == TypeKind::Interface) {
                t->bounds |= BoundIface;
                t->elem = ib->type;
            } else {
                fail_n(b, "lucb.check.type", "unknown constraint `" + string(name) + "`");
            }
        }
    }
}

auto Checker::bind_generic_params(Node* gen) -> void {
    if (gen == nullptr) {
        return;
    }
    for (Node* g = gen->left; g != nullptr; g = g->next) {
        if (g->kind != NodeKind::GenericParam) {
            continue;
        }
        if (g->ty == nullptr || g->ty->kind != TypeKind::Param) {
            Type* t = make_type(TypeKind::Param, g->text);
            t->decl = g;
            t->name = g->text;
            apply_bounds(g, t);
            g->ty = t;
        }
        bind(g->text, g->ty, false, g);
    }
}

auto Checker::clone_chain(Node* n) -> Node* {
    Node* head = nullptr;
    for (; n != nullptr; n = n->next) {
        append_node(&head, clone_node(n));
    }
    return head;
}

auto Checker::clone_node(Node* n) -> Node* {
    if (n == nullptr) {
        return nullptr;
    }
    Node* c = arena->make<Node>();
    c->kind = n->kind;
    c->span = n->span;
    c->text = n->text;
    c->op = n->op;
    c->flags = n->flags;
    c->left = clone_chain(n->left);
    c->right = clone_chain(n->right);
    c->type = clone_chain(n->type);
    c->body = clone_chain(n->body);
    return c;
}

auto Checker::args_eq(const vector<Type*>& a, const vector<Type*>& b) -> bool {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

auto Checker::is_identity_args(Node* generic, const vector<Type*>& args) -> bool {
    int i = 0;
    for (Node* g = generic != nullptr ? generic->left : nullptr; g != nullptr; g = g->next) {
        if (g->kind != NodeKind::GenericParam) {
            continue;
        }
        if (i >= static_cast<int>(args.size()) || args[static_cast<size_t>(i)] != g->ty) {
            return false;
        }
        i++;
    }
    return i == static_cast<int>(args.size());
}

auto Checker::find_inst(Node* generic, const vector<Type*>& args) -> Inst* {
    for (size_t i = 0; i < insts.size(); i++) {
        if (insts[i].generic == generic && args_eq(insts[i].args, args)) {
            return &insts[i];
        }
    }
    return nullptr;
}

auto Checker::comparable_type(Type* t) -> bool {
    if (t == nullptr) {
        return false;
    }
    if (is_int(t) || is_float(t) || t->kind == TypeKind::Char || t->kind == TypeKind::Str) {
        return true;
    }
    if (t->kind == TypeKind::Param && (t->bounds & BoundComparable) != 0) {
        return true;
    }
    if (t->kind == TypeKind::Struct && t->decl != nullptr) {
        return struct_member(t->decl, "compare", NodeKind::Func) != nullptr;
    }
    return false;
}

auto Checker::struct_implements(Node* st, Type* iface) -> bool {
    if (st == nullptr || iface == nullptr || iface->decl == nullptr) {
        return false;
    }
    for (Node* t = st->right; t != nullptr; t = t->next) {
        Type* it = t->ty;
        if (it == nullptr && t->kind == NodeKind::Type) {
            it = resolve_type(t);
        }
        if (it != nullptr && it->decl == iface->decl) {
            return true;
        }
    }
    return false;
}

auto Checker::iface_has_mutating(Type* iface) -> bool {
    if (iface == nullptr || iface->decl == nullptr) {
        return false;
    }
    for (Node* m = iface->decl->body; m != nullptr; m = m->next) {
        if (m->kind == NodeKind::Func && (m->flags & FlagMutating) != 0) {
            return true;
        }
    }
    return false;
}

auto Checker::satisfies_bounds(Type* t, Node* g, Node* at) -> bool {
    uint32_t bounds = g != nullptr && g->ty != nullptr ? g->ty->bounds : 0;
    if (bounds == 0) {
        return true;
    }
    if ((bounds & BoundComparable) != 0 && !comparable_type(t)) {
        fail_n(at, "lucb.check.type", "`" + type_name(t) + "` does not satisfy `Comparable`");
        return false;
    }
    if ((bounds & BoundHashable) != 0 && !is_hashable(t)) {
        fail_n(at, "lucb.check.type", "`" + type_name(t) + "` does not satisfy `Hashable`");
        return false;
    }
    if ((bounds & BoundIface) != 0) {
        Type* iface = g != nullptr && g->ty != nullptr ? g->ty->elem : nullptr;
        Node* st = t != nullptr ? t->decl : nullptr;
        if (is_ptr(t)) {
            st = t->elem != nullptr ? t->elem->decl : nullptr;
        }
        if (!struct_implements(st, iface) &&
            !(t != nullptr && iface != nullptr && t->kind == TypeKind::Interface &&
              t->decl == iface->decl)) {
            fail_n(at, "lucb.check.type",
                   "`" + type_name(t) + "` does not implement `" +
                       (iface != nullptr ? type_name(iface) : "interface") + "`");
            return false;
        }
    }
    return true;
}

auto Checker::unify_into(Type* pat, Type* got, Node* generic, vector<Type*>& inf, Node* at)
    -> void {
    if (pat == nullptr || got == nullptr || got->kind == TypeKind::Error ||
        pat->kind == TypeKind::Error) {
        return;
    }
    if (pat->kind == TypeKind::Param) {
        int i = index_of_param(generic, pat);
        if (i < 0 || i >= static_cast<int>(inf.size())) {
            return;
        }
        if (got->kind == TypeKind::UntypedInt) {
            if (inf[static_cast<size_t>(i)] == nullptr) {
                inf[static_cast<size_t>(i)] = ty_i64;
            }
            return;
        }
        if (inf[static_cast<size_t>(i)] == nullptr) {
            inf[static_cast<size_t>(i)] = got;
        } else if (!type_eq(inf[static_cast<size_t>(i)], got) &&
                   !can_widen(got, inf[static_cast<size_t>(i)])) {
            if (can_widen(inf[static_cast<size_t>(i)], got)) {
                inf[static_cast<size_t>(i)] = got;
            } else {
                fail_n(at, "lucb.check.type",
                       "type parameter `" + string(pat->name) + "` inferred as both `" +
                           type_name(inf[static_cast<size_t>(i)]) + "` and `" + type_name(got) +
                           "`");
            }
        }
        return;
    }
    if (is_ptr(pat) && is_ptr(got)) {
        unify_into(pat->elem, got->elem, generic, inf, at);
        return;
    }
    if (is_span(pat) && (is_span(got) || is_array(got))) {
        unify_into(pat->elem, got->elem, generic, inf, at);
        return;
    }
    if (is_array(pat) && is_array(got) && pat->length == got->length) {
        unify_into(pat->elem, got->elem, generic, inf, at);
        return;
    }
    if (is_opt(pat) && is_opt(got)) {
        unify_into(pat->elem, got->elem, generic, inf, at);
        return;
    }
    if (is_opt(pat)) {
        unify_into(pat->elem, got, generic, inf, at);
        return;
    }
    if (is_fail(pat) && is_fail(got)) {
        unify_into(pat->elem, got->elem, generic, inf, at);
        return;
    }
    if (pat->kind == TypeKind::Struct && got->kind == TypeKind::Struct && pat->decl == got->decl &&
        pat->ntargs == got->ntargs && pat->args != nullptr && got->args != nullptr) {
        for (int i = 0; i < pat->ntargs; i++) {
            unify_into(pat->args[i], got->args[i], generic, inf, at);
        }
    }
}

auto Checker::finish_inferred(Node* generic, vector<Type*>& inf, Node* at) -> bool {
    int i = 0;
    for (Node* g = generic != nullptr ? generic->left : nullptr; g != nullptr; g = g->next) {
        if (g->kind != NodeKind::GenericParam) {
            continue;
        }
        if (i >= static_cast<int>(inf.size()) || inf[static_cast<size_t>(i)] == nullptr) {
            fail_n(at, "lucb.check.type",
                   "cannot infer type parameter `" + string(g->text) + "`; write it at the call");
            return false;
        }
        if (inf[static_cast<size_t>(i)]->kind == TypeKind::UntypedInt) {
            inf[static_cast<size_t>(i)] = ty_i64;
        }
        if (!satisfies_bounds(inf[static_cast<size_t>(i)], g, at)) {
            return false;
        }
        i++;
    }
    return true;
}

auto Checker::subst_type(Type* t, Node* generic, const vector<Type*>& args) -> Type* {
    if (t == nullptr) {
        return nullptr;
    }
    if (t->kind == TypeKind::Param) {
        int i = index_of_param(generic, t);
        if (i >= 0 && i < static_cast<int>(args.size())) {
            return args[static_cast<size_t>(i)];
        }
        return t;
    }
    if (t->kind == TypeKind::Pointer) {
        return intern_ptr(subst_type(t->elem, generic, args), t->is_const, t->is_volatile,
                          t->is_nullable);
    }
    if (t->kind == TypeKind::Span) {
        return intern_sp(subst_type(t->elem, generic, args), t->is_const);
    }
    if (t->kind == TypeKind::Array) {
        return intern_arr(subst_type(t->elem, generic, args), t->length);
    }
    if (t->kind == TypeKind::Optional) {
        return intern_opt(subst_type(t->elem, generic, args));
    }
    if (t->kind == TypeKind::Fallible) {
        return intern_fail(subst_type(t->elem, generic, args));
    }
    if (t->kind == TypeKind::Tuple && t->args != nullptr) {
        vector<Type*> na;
        for (int i = 0; i < t->ntargs; i++) {
            na.push_back(subst_type(t->args[i], generic, args));
        }
        return intern_tup(na.empty() ? nullptr : na.data(), t->ntargs);
    }
    if (t->kind == TypeKind::Func) {
        vector<Type*> na;
        for (int i = 0; i < t->ntargs; i++) {
            na.push_back(subst_type(t->args[i], generic, args));
        }
        return intern_func(na.empty() ? nullptr : na.data(), t->ntargs,
                           subst_type(t->elem, generic, args), t->is_nullable);
    }
    if (t->kind == TypeKind::Struct && t->ntargs > 0 && t->args != nullptr && t->decl != nullptr) {
        vector<Type*> na;
        for (int i = 0; i < t->ntargs; i++) {
            na.push_back(subst_type(t->args[i], generic, args));
        }
        return instantiate_struct(t->decl, na, t->decl);
    }
    return t;
}

auto Checker::instantiate_func(Node* fn, const vector<Type*>& args, Node* owner) -> Node* {
    if (fn == nullptr) {
        return nullptr;
    }
    if (is_identity_args(fn, args)) {
        return fn;
    }
    Inst* existing = find_inst(fn, args);
    if (existing != nullptr) {
        return existing->clone;
    }
    if (inst_depth > 32) {
        fail_n(fn, "lucb.check.type", "generic instantiation is too deep");
        return fn;
    }
    Node* clone = clone_node(fn);
    clone->left = nullptr;
    clone->text = keep(mangle_inst(fn->text, args));
    Inst in;
    in.generic = fn;
    in.clone = clone;
    in.args = args;
    insts.push_back(in);
    pending_clones.push_back(clone);
    inst_depth++;
    push_scope();
    int i = 0;
    for (Node* g = fn->left; g != nullptr; g = g->next) {
        if (g->kind != NodeKind::GenericParam) {
            continue;
        }
        Type* a = i < static_cast<int>(args.size()) ? args[static_cast<size_t>(i)] : t_error();
        bind(g->text, a, false, nullptr);
        i++;
    }
    check_func(clone, owner);
    pop_scope();
    inst_depth--;
    return clone;
}

auto Checker::instantiate_struct(Node* st, const vector<Type*>& args, Node* at) -> Type* {
    if (st == nullptr) {
        return t_error();
    }
    if (is_identity_args(st, args)) {
        return st->ty != nullptr ? st->ty : t_error();
    }
    Inst* existing = find_inst(st, args);
    if (existing != nullptr && existing->type != nullptr) {
        return existing->type;
    }
    if (inst_depth > 32) {
        fail_n(at != nullptr ? at : st, "lucb.check.type", "generic instantiation is too deep");
        return t_error();
    }
    Node* clone = clone_node(st);
    clone->left = nullptr;
    clone->text = keep(mangle_inst(st->text, args));
    Type* t = make_type(TypeKind::Struct, clone->text);
    t->decl = clone;
    t->packed = (clone->flags & FlagPacked) != 0;
    t->ntargs = static_cast<int>(args.size());
    if (!args.empty()) {
        t->args = static_cast<Type**>(arena->alloc(sizeof(Type*) * args.size(), alignof(Type*)));
        for (size_t i = 0; i < args.size(); i++) {
            t->args[i] = args[i];
        }
    }
    clone->ty = t;
    interned.push_back(t);
    Inst in;
    in.generic = st;
    in.clone = clone;
    in.type = t;
    in.args = args;
    insts.push_back(in);
    pending_clones.push_back(clone);
    inst_depth++;
    push_scope();
    int i = 0;
    for (Node* g = st->left; g != nullptr; g = g->next) {
        if (g->kind != NodeKind::GenericParam) {
            continue;
        }
        Type* a = i < static_cast<int>(args.size()) ? args[static_cast<size_t>(i)] : t_error();
        bind(g->text, a, false, nullptr);
        i++;
    }
    for (Node* m = clone->body; m != nullptr; m = m->next) {
        if (m->kind == NodeKind::Field) {
            m->ty = resolve_type(m->type);
        }
    }
    for (Node* m = clone->body; m != nullptr; m = m->next) {
        if (m->kind == NodeKind::Func) {
            resolve_sig(m);
            check_func(m, clone);
        }
    }
    pop_scope();
    inst_depth--;
    return t;
}

auto Checker::read_explicit_targs(Node* n, Node* generic, vector<Type*>& inf) -> Type* {
    if (n == nullptr || n->type == nullptr) {
        return nullptr;
    }
    int i = 0;
    int ng = count_generics(generic);
    for (Node* a = n->type; a != nullptr; a = a->next) {
        if (i >= ng) {
            fail_n(a, "lucb.check.type", "too many type arguments");
            break;
        }
        inf[static_cast<size_t>(i)] = resolve_type(a);
        i++;
    }
    if (i != ng) {
        fail_n(n, "lucb.check.type", "wrong number of type arguments");
    }
    return nullptr;
}

auto Checker::check_generic_call(Node* n, Node* fn, Node* recv) -> Type* {
    int ng = count_generics(fn);
    vector<Type*> inf(static_cast<size_t>(ng), nullptr);
    if (n->type != nullptr) {
        read_explicit_targs(n, fn, inf);
    } else {
        Node* p = fn->right;
        Node* a = n->body;
        while (p != nullptr && a != nullptr) {
            Type* at = check_expr(a->left, nullptr);
            unify_into(p->ty, at, fn, inf, a);
            p = p->next;
            a = a->next;
        }
    }
    if (!finish_inferred(fn, inf, n)) {
        return t_error();
    }
    Type* result = subst_type(fn->ty, fn, inf);
    if ((fn->flags & FlagFallible) != 0) {
        result = intern_fail(result);
    }
    if (checking_generic_template) {
        n->resolved = fn;
        Node* p = fn->right;
        Node* a = n->body;
        while (p != nullptr && a != nullptr) {
            check_expr(a->left, subst_type(p->ty, fn, inf));
            p = p->next;
            a = a->next;
        }
        return result;
    }
    Node* inst = instantiate_func(fn, inf, recv);
    return check_func_call(n, inst, recv);
}

auto Checker::check_generic_ctor(Node* n, Node* st) -> Type* {
    int ng = count_generics(st);
    vector<Type*> inf(static_cast<size_t>(ng), nullptr);
    if (n->type != nullptr) {
        read_explicit_targs(n, st, inf);
    } else {
        for (Node* a = n->body; a != nullptr; a = a->next) {
            if (a->left == nullptr || a->text.empty()) {
                continue;
            }
            Node* field = struct_member(st, a->text, NodeKind::Field);
            if (field == nullptr) {
                fail_n(a, "lucb.check.name", "no field `" + string(a->text) + "`");
                continue;
            }
            Type* at = check_expr(a->left, nullptr);
            unify_into(field->ty, at, st, inf, a);
        }
    }
    if (!finish_inferred(st, inf, n)) {
        return t_error();
    }
    if (checking_generic_template) {
        n->resolved = st;
        return subst_type(st->ty, st, inf);
    }
    Type* ty = instantiate_struct(st, inf, n);
    n->resolved = ty != nullptr ? ty->decl : st;
    if (n->body != nullptr && ty != nullptr && ty->decl != nullptr) {
        check_ctor(n, ty->decl);
    }
    return ty;
}

} // namespace lucb
