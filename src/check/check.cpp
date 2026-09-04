#include "check/check.h"

#include "check/type.h"
#include "support/literal.h"

namespace lucb {
namespace {

const uint32_t k_type_flags_unsupported =
    FlagStar | FlagSpan | FlagArray | FlagOptional | FlagFallible | FlagAtomic | FlagFuncType |
    FlagVoid | FlagTupleType | FlagConst | FlagVolatile;

struct Binding {
    string_view name;
    Type* type = nullptr;
    bool mut = false;
    Node* decl = nullptr;
    int depth = 0;
};

struct Checker {
    Arena* arena = nullptr;
    DiagnosticBag* diag = nullptr;
    string path;
    Type* ty_error = nullptr;
    Type* ty_never = nullptr;
    Type* ty_unit = nullptr;
    Type* ty_bool = nullptr;
    Type* ty_i64 = nullptr;
    Type* ty_str = nullptr;
    vector<Binding> scope;
    int depth = 0;
    Node* current_fn = nullptr;
    Node* current_struct = nullptr;
    Type* return_type = nullptr;

    Type* t_error() { return ty_error; }
    Type* t_never() { return ty_never; }
    Type* t_unit() { return ty_unit; }
    Type* t_bool() { return ty_bool; }
    Type* t_i64() { return ty_i64; }
    Type* t_str() { return ty_str; }

    Type* make_type(TypeKind kind, string_view name) {
        Type* t = arena->make<Type>();
        t->kind = kind;
        t->name = name;
        return t;
    }

    void fail(Span span, const char* code, const string& message) {
        diag->add(code, path, span, message);
    }

    void fail_n(Node* n, const char* code, const string& message) {
        fail(n != nullptr ? n->span : Span{}, code, message);
    }

    void push_scope() { depth++; }

    void pop_scope() {
        while (!scope.empty() && scope.back().depth == depth) {
            scope.pop_back();
        }
        depth--;
    }

    Binding* lookup(string_view name) {
        for (int i = static_cast<int>(scope.size()) - 1; i >= 0; i--) {
            if (scope[static_cast<size_t>(i)].name == name) {
                return &scope[static_cast<size_t>(i)];
            }
        }
        return nullptr;
    }

    bool bind(string_view name, Type* type, bool mut, Node* decl) {
        if (lookup(name) != nullptr) {
            fail_n(decl, "lucb.check.shadow", "this name is already in scope");
            return false;
        }
        Binding b;
        b.name = name;
        b.type = type;
        b.mut = mut;
        b.decl = decl;
        b.depth = depth;
        scope.push_back(b);
        return true;
    }

    bool is_core_name(string_view name) {
        return name == "print" || name == "assert" || name == "discard" || name == "error" ||
               name == "trap" || name == "hash" || name == "format" || name == "location" ||
               name == "sizeof" || name == "alignof" || name == "offsetof" || name == "hex" ||
               name == "i64" || name == "bool" || name == "unit" || name == "str";
    }

    Type* resolve_type(Node* n) {
        if (n == nullptr) {
            return t_unit();
        }
        if (n->ty != nullptr) {
            return n->ty;
        }
        if ((n->flags & k_type_flags_unsupported) != 0) {
            fail_n(n, "lucb.check.unsupported", "this type is not in the scalar core yet");
            n->ty = t_error();
            return n->ty;
        }
        if (n->left != nullptr && n->text.empty() && n->flags == 0) {
            n->ty = resolve_type(n->left);
            return n->ty;
        }
        if (n->text == "i64") {
            n->ty = t_i64();
            return n->ty;
        }
        if (n->text == "bool") {
            n->ty = t_bool();
            return n->ty;
        }
        if (n->text == "unit") {
            n->ty = t_unit();
            return n->ty;
        }
        if (n->text == "str") {
            n->ty = t_str();
            return n->ty;
        }
        Binding* b = lookup(n->text);
        if (b != nullptr && b->type != nullptr && b->type->kind == TypeKind::Struct) {
            n->ty = b->type;
            n->resolved = b->decl;
            return n->ty;
        }
        fail_n(n, "lucb.check.type", "unknown type `" + string(n->text) + "`");
        n->ty = t_error();
        return n->ty;
    }

    Node* struct_member(Node* st, string_view name, NodeKind kind) {
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

    Type* check_expr(Node* n) {
        if (n == nullptr) {
            return t_error();
        }
        if (n->ty != nullptr && n->kind != NodeKind::Name && n->kind != NodeKind::Call &&
            n->kind != NodeKind::Member) {
            return n->ty;
        }
        Type* t = t_error();
        switch (n->kind) {
        case NodeKind::Literal:
            t = check_literal(n);
            break;
        case NodeKind::Name:
            t = check_name(n);
            break;
        case NodeKind::Self:
            t = check_self(n);
            break;
        case NodeKind::Unary:
            t = check_unary(n);
            break;
        case NodeKind::Binary:
            t = check_binary(n);
            break;
        case NodeKind::Call:
            t = check_call(n);
            break;
        case NodeKind::Member:
            t = check_member(n, false);
            break;
        case NodeKind::Group:
            t = check_expr(n->left);
            break;
        case NodeKind::Unit:
            t = t_unit();
            break;
        case NodeKind::Conditional: {
            Type* tv = check_expr(n->left);
            Type* tc = check_expr(n->type);
            Type* ta = check_expr(n->right);
            if (!type_eq(tc, t_bool())) {
                fail_n(n, "lucb.check.type", "a condition must be `bool`");
            }
            if (!type_eq(tv, ta)) {
                fail_n(n, "lucb.check.type", "conditional branches must have the same type");
            }
            t = tv;
            break;
        }
        default:
            fail_n(n, "lucb.check.unsupported", "this expression is not in the scalar core yet");
            t = t_error();
            break;
        }
        n->ty = t;
        return t;
    }

    Type* check_literal(Node* n) {
        if (n->op == TokenKind::KwTrue || n->op == TokenKind::KwFalse) {
            return t_bool();
        }
        if (n->op == TokenKind::StringLit) {
            return t_str();
        }
        if (n->op == TokenKind::IntLit) {
            int64_t value = 0;
            if (!parse_i64_literal(n->text, &value)) {
                fail_n(n, "lucb.check.number", "integer literal does not fit in `i64`");
            }
            if (n->text.size() >= 2) {
                char last = n->text[n->text.size() - 1];
                if (last >= 'a' && last <= 'z' && n->text.find("i64") == string_view::npos &&
                    n->text.find("u64") == string_view::npos) {
                    // allow unsuffixed; reject u8/i32 for M3
                    size_t k = n->text.size();
                    while (k > 0 && n->text[k - 1] >= 'a' && n->text[k - 1] <= 'z') {
                        k--;
                    }
                    while (k > 0 && n->text[k - 1] >= '0' && n->text[k - 1] <= '9') {
                        k--;
                    }
                    if (k < n->text.size() && n->text.substr(k) != "i64") {
                        fail_n(n, "lucb.check.unsupported",
                               "only unsuffixed and `i64` integer literals are in the scalar core");
                    }
                }
            }
            return t_i64();
        }
        fail_n(n, "lucb.check.unsupported", "this literal is not in the scalar core yet");
        return t_error();
    }

    Type* check_name(Node* n) {
        Binding* b = lookup(n->text);
        if (b == nullptr) {
            fail_n(n, "lucb.check.name", "unknown name `" + string(n->text) + "`");
            return t_error();
        }
        n->resolved = b->decl;
        return b->type;
    }

    Type* check_self(Node* n) {
        Binding* b = lookup("self");
        if (b == nullptr) {
            fail_n(n, "lucb.check.self", "`self` is only valid in a method");
            return t_error();
        }
        n->resolved = b->decl;
        return b->type;
    }

    Type* check_unary(Node* n) {
        Type* inner = check_expr(n->left);
        if (n->op == TokenKind::KwNot) {
            if (!type_eq(inner, t_bool())) {
                fail_n(n, "lucb.check.type", "`not` requires `bool`");
            }
            return t_bool();
        }
        if (n->op == TokenKind::Minus || n->op == TokenKind::Plus) {
            if (!type_eq(inner, t_i64())) {
                fail_n(n, "lucb.check.type", "unary `+`/`-` requires `i64`");
            }
            return t_i64();
        }
        fail_n(n, "lucb.check.unsupported", "this operator is not in the scalar core yet");
        return t_error();
    }

    Type* check_binary(Node* n) {
        Type* L = check_expr(n->left);
        Type* R = check_expr(n->right);
        TokenKind op = n->op;
        if (op == TokenKind::KwAnd || op == TokenKind::KwOr) {
            if (!type_eq(L, t_bool()) || !type_eq(R, t_bool())) {
                fail_n(n, "lucb.check.type", "`and`/`or` require `bool`");
            }
            return t_bool();
        }
        if (op == TokenKind::EqEq || op == TokenKind::NotEq) {
            if (!type_eq(L, R)) {
                fail_n(n, "lucb.check.type", "operands of `==` must have the same type");
            }
            return t_bool();
        }
        if (op == TokenKind::Lt || op == TokenKind::LtEq || op == TokenKind::Gt ||
            op == TokenKind::GtEq) {
            if (!type_eq(L, t_i64()) || !type_eq(R, t_i64())) {
                fail_n(n, "lucb.check.type", "ordered comparison requires `i64`");
            }
            return t_bool();
        }
        if (op == TokenKind::Plus || op == TokenKind::Minus || op == TokenKind::Star ||
            op == TokenKind::SlashSlash || op == TokenKind::Percent) {
            if (!type_eq(L, t_i64()) || !type_eq(R, t_i64())) {
                fail_n(n, "lucb.check.type", "arithmetic requires `i64`");
            }
            return t_i64();
        }
        fail_n(n, "lucb.check.unsupported", "this operator is not in the scalar core yet");
        return t_error();
    }

    int count_args(Node* args) {
        int n = 0;
        for (Node* a = args; a != nullptr; a = a->next) {
            n++;
        }
        return n;
    }

    Type* check_call(Node* n) {
        Node* callee = n->left;
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "print") {
            return check_print(n);
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "trap") {
            return check_trap(n);
        }
        if (callee != nullptr && callee->kind == NodeKind::Member) {
            return check_method_call(n);
        }
        if (callee != nullptr && callee->kind == NodeKind::Name) {
            Binding* b = lookup(callee->text);
            if (b == nullptr) {
                fail_n(n, "lucb.check.name", "unknown name `" + string(callee->text) + "`");
                return t_error();
            }
            callee->resolved = b->decl;
            n->resolved = b->decl;
            if (b->decl != nullptr && b->decl->kind == NodeKind::Struct) {
                return check_ctor(n, b->decl);
            }
            if (b->decl != nullptr && b->decl->kind == NodeKind::Func) {
                return check_func_call(n, b->decl, nullptr);
            }
            fail_n(n, "lucb.check.type", "`" + string(callee->text) + "` is not callable");
            return t_error();
        }
        fail_n(n, "lucb.check.unsupported", "this call is not in the scalar core yet");
        return t_error();
    }

    Type* check_print(Node* n) {
        n->resolved = nullptr;
        int count = count_args(n->body);
        if (count != 1) {
            fail_n(n, "lucb.check.call", "`print` takes one argument");
            return t_unit();
        }
        Type* a = check_expr(n->body->left);
        if (!type_eq(a, t_i64()) && !type_eq(a, t_bool()) && !type_eq(a, t_str())) {
            fail_n(n, "lucb.check.type", "`print` in the scalar core takes i64, bool, or str");
        }
        return t_unit();
    }

    Type* check_trap(Node* n) {
        int count = count_args(n->body);
        if (count != 1) {
            fail_n(n, "lucb.check.call", "`trap` takes one argument");
        } else {
            Type* a = check_expr(n->body->left);
            if (!type_eq(a, t_str())) {
                fail_n(n, "lucb.check.type", "`trap` takes a string");
            }
        }
        return t_never();
    }

    Type* check_ctor(Node* n, Node* st) {
        Type* ty = st->ty;
        // Mark provided fields.
        for (Node* a = n->body; a != nullptr; a = a->next) {
            if (a->left == nullptr) {
                continue;
            }
            Type* at = check_expr(a->left);
            Node* field = nullptr;
            if (!a->text.empty()) {
                field = struct_member(st, a->text, NodeKind::Field);
                if (field == nullptr) {
                    fail_n(a, "lucb.check.name", "no field `" + string(a->text) + "`");
                    continue;
                }
            } else {
                fail_n(a, "lucb.check.unsupported",
                       "positional struct construction is not in the scalar core; use names");
                continue;
            }
            a->resolved = field;
            if (!type_eq(at, field->ty)) {
                fail_n(a, "lucb.check.type",
                       "field `" + string(field->text) + "` has type " + type_name(field->ty));
            }
        }
        return ty;
    }

    Type* check_func_call(Node* n, Node* fn, Node* recv) {
        n->resolved = fn;
        Node* params = fn->right;
        Node* args = n->body;
        // Skip implicit self: methods don't list self.
        int nparams = count_args(params);
        int nargs = count_args(args);
        if (recv != nullptr) {
            // method: args must match params
        }
        (void)recv;
        if (nparams != nargs) {
            fail_n(n, "lucb.check.call", "wrong number of arguments");
        }
        Node* p = params;
        Node* a = args;
        while (p != nullptr && a != nullptr) {
            if (!a->text.empty() && a->text != p->text) {
                fail_n(a, "lucb.check.call", "argument name does not match parameter `" +
                                                 string(p->text) + "`");
            }
            Type* at = check_expr(a->left);
            if (!type_eq(at, p->ty)) {
                fail_n(a, "lucb.check.type",
                       "parameter `" + string(p->text) + "` has type " + type_name(p->ty));
            }
            a->resolved = p;
            p = p->next;
            a = a->next;
        }
        Type* result = fn->ty;
        if (result == nullptr) {
            result = t_unit();
        }
        return result;
    }

    Type* check_method_call(Node* n) {
        Node* mem = n->left;
        Node* obj = mem->left;
        // Static: Point.origin() — obj is a type name.
        if (obj != nullptr && obj->kind == NodeKind::Name) {
            Binding* b = lookup(obj->text);
            if (b != nullptr && b->decl != nullptr && b->decl->kind == NodeKind::Struct) {
                Node* method = struct_member(b->decl, mem->text, NodeKind::Func);
                if (method == nullptr) {
                    fail_n(n, "lucb.check.name", "no method `" + string(mem->text) + "`");
                    return t_error();
                }
                if ((method->flags & FlagStatic) == 0) {
                    fail_n(n, "lucb.check.call", "this method needs a receiver");
                }
                mem->resolved = method;
                obj->resolved = b->decl;
                return check_func_call(n, method, nullptr);
            }
        }
        Type* ot = check_expr(obj);
        if (ot == nullptr || ot->kind != TypeKind::Struct || ot->decl == nullptr) {
            fail_n(n, "lucb.check.type", "methods are called on structs");
            return t_error();
        }
        Node* method = struct_member(ot->decl, mem->text, NodeKind::Func);
        if (method == nullptr) {
            fail_n(n, "lucb.check.name", "no method `" + string(mem->text) + "`");
            return t_error();
        }
        if ((method->flags & FlagStatic) != 0) {
            fail_n(n, "lucb.check.call", "a static method is called on the type");
        }
        if ((method->flags & FlagMutating) != 0 && !is_mut_place(obj)) {
            fail_n(n, "lucb.check.mut", "a mutating method needs a `var` receiver");
        }
        mem->resolved = method;
        return check_func_call(n, method, obj);
    }

    Type* check_member(Node* n, bool as_call) {
        (void)as_call;
        Type* ot = check_expr(n->left);
        if (ot == nullptr || ot->kind != TypeKind::Struct || ot->decl == nullptr) {
            fail_n(n, "lucb.check.type", "field access needs a struct");
            return t_error();
        }
        Node* field = struct_member(ot->decl, n->text, NodeKind::Field);
        if (field == nullptr) {
            fail_n(n, "lucb.check.name", "no field `" + string(n->text) + "`");
            return t_error();
        }
        n->resolved = field;
        return field->ty;
    }

    bool is_mut_place(Node* n) {
        if (n == nullptr) {
            return false;
        }
        if (n->kind == NodeKind::Name) {
            Binding* b = lookup(n->text);
            return b != nullptr && b->mut;
        }
        if (n->kind == NodeKind::Self) {
            Binding* b = lookup("self");
            return b != nullptr && b->mut;
        }
        if (n->kind == NodeKind::Member) {
            return is_mut_place(n->left);
        }
        return false;
    }

    void check_stmt(Node* n) {
        if (n == nullptr) {
            return;
        }
        switch (n->kind) {
        case NodeKind::Block:
            push_scope();
            for (Node* s = n->body; s != nullptr; s = s->next) {
                check_stmt(s);
            }
            pop_scope();
            break;
        case NodeKind::Let:
        case NodeKind::Var: {
            Type* t = nullptr;
            if (n->type != nullptr) {
                t = resolve_type(n->type);
            }
            if (n->left != nullptr) {
                Type* init = check_expr(n->left);
                if (t == nullptr) {
                    t = init;
                } else if (!type_eq(t, init)) {
                    fail_n(n, "lucb.check.type", "initialiser has type " + type_name(init) +
                                                     ", expected " + type_name(t));
                }
            } else if (t == nullptr) {
                fail_n(n, "lucb.check.type", "this binding needs a type or an initialiser");
                t = t_error();
            } else if (!is_zeroable(t)) {
                fail_n(n, "lucb.check.type", "this type has no zero value; write an initialiser");
            }
            n->ty = t;
            bind(n->text, t, n->kind == NodeKind::Var, n);
            break;
        }
        case NodeKind::Assign: {
            Type* lt = check_expr(n->left);
            Type* rt = check_expr(n->right);
            if (!is_mut_place(n->left)) {
                fail_n(n, "lucb.check.mut", "this place is not assignable");
            }
            if (n->op == TokenKind::Eq) {
                if (!type_eq(lt, rt)) {
                    fail_n(n, "lucb.check.type", "assignment type mismatch");
                }
            } else {
                if (!type_eq(lt, t_i64()) || !type_eq(rt, t_i64())) {
                    fail_n(n, "lucb.check.type", "compound assignment requires `i64`");
                }
            }
            break;
        }
        case NodeKind::If: {
            Type* c = check_expr(n->left);
            if (n->left != nullptr && n->left->kind == NodeKind::Let) {
                fail_n(n, "lucb.check.unsupported", "`if let` is not in the scalar core yet");
            } else if (!type_eq(c, t_bool())) {
                fail_n(n, "lucb.check.type", "a condition must be `bool`");
            }
            check_stmt(n->body);
            if (n->right != nullptr) {
                check_stmt(n->right);
            }
            break;
        }
        case NodeKind::While: {
            Type* c = check_expr(n->left);
            if (!type_eq(c, t_bool())) {
                fail_n(n, "lucb.check.type", "a condition must be `bool`");
            }
            check_stmt(n->body);
            break;
        }
        case NodeKind::Return: {
            Type* t = t_unit();
            if (n->left != nullptr) {
                t = check_expr(n->left);
            }
            if (return_type != nullptr && !type_eq(t, return_type)) {
                fail_n(n, "lucb.check.type", "return type is " + type_name(t) + ", expected " +
                                                 type_name(return_type));
            }
            break;
        }
        case NodeKind::ExprStmt:
            check_expr(n->left);
            break;
        default:
            fail_n(n, "lucb.check.unsupported", "this statement is not in the scalar core yet");
            break;
        }
    }

    bool always_returns(Node* n) {
        if (n == nullptr) {
            return false;
        }
        switch (n->kind) {
        case NodeKind::Return:
            return true;
        case NodeKind::Block: {
            bool r = false;
            for (Node* s = n->body; s != nullptr; s = s->next) {
                r = always_returns(s);
            }
            return r;
        }
        case NodeKind::If:
            return always_returns(n->body) && always_returns(n->right);
        default:
            return false;
        }
    }

    void check_params(Node* fn) {
        for (Node* p = fn->right; p != nullptr; p = p->next) {
            if (p->text == "self") {
                fail_n(p, "lucb.check.self",
                       "do not write `self` as a parameter; methods take it implicitly");
            }
            p->ty = resolve_type(p->type);
            bind(p->text, p->ty, false, p);
        }
    }

    void check_func(Node* fn, Node* owner) {
        if (fn->left != nullptr) {
            fail_n(fn, "lucb.check.unsupported", "generics are not in the scalar core yet");
        }
        Type* result = t_unit();
        if (fn->type != nullptr) {
            result = resolve_type(fn->type);
        }
        fn->ty = result;
        Node* saved_fn = current_fn;
        Node* saved_st = current_struct;
        Type* saved_ret = return_type;
        current_fn = fn;
        current_struct = owner;
        return_type = result;
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
    }

    void check_struct(Node* st) {
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
    }

    void collect_module(Node* mod) {
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Struct) {
                if (lookup(d->text) != nullptr) {
                    fail_n(d, "lucb.check.shadow", "this name is already in scope");
                    continue;
                }
                Type* t = make_type(TypeKind::Struct, d->text);
                t->decl = d;
                d->ty = t;
                bind(d->text, t, false, d);
            } else if (d->kind == NodeKind::Func) {
                if (is_core_name(d->text)) {
                    fail_n(d, "lucb.check.shadow", "this name belongs to the language");
                }
                if (lookup(d->text) != nullptr) {
                    fail_n(d, "lucb.check.shadow", "this name is already in scope");
                    continue;
                }
                Type* result = t_unit();
                if (d->type != nullptr) {
                    // result types that name structs need structs already bound;
                    // resolve in a second pass. Bind as a func with placeholder.
                }
                bind(d->text, result, false, d);
            } else {
                fail_n(d, "lucb.check.unsupported",
                       "this declaration is not in the scalar core yet");
            }
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Struct) {
                for (Node* m = d->body; m != nullptr; m = m->next) {
                    if (m->kind == NodeKind::Field) {
                        m->ty = resolve_type(m->type);
                    }
                }
            }
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Func) {
                resolve_sig(d);
                Binding* b = lookup(d->text);
                if (b != nullptr && b->decl == d) {
                    b->type = d->ty;
                }
            } else if (d->kind == NodeKind::Struct) {
                for (Node* m = d->body; m != nullptr; m = m->next) {
                    if (m->kind == NodeKind::Func) {
                        resolve_sig(m);
                    }
                }
            }
        }
    }

    void resolve_sig(Node* fn) {
        Type* result = t_unit();
        if (fn->type != nullptr) {
            result = resolve_type(fn->type);
        }
        fn->ty = result;
        for (Node* p = fn->right; p != nullptr; p = p->next) {
            p->ty = resolve_type(p->type);
        }
    }

    void check_module(Node* mod) {
        push_scope();
        collect_module(mod);
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Struct) {
                check_struct(d);
            }
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Func) {
                check_func(d, nullptr);
            }
        }
        pop_scope();
    }
};

} // namespace

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
    c.ty_i64 = c.make_type(TypeKind::I64, "i64");
    c.ty_str = c.make_type(TypeKind::Str, "str");
    c.check_module(module);
    return diagnostics.empty();
}

} // namespace lucb
