//==============================================================================================
//
//   check/intrinsics - The one-word core functions
//
//   DESCRIPTION:
//       `sizeof`, `alignof`, `offsetof`, `assert`, `trap`, `error`, `discard`, `hash`, `hex`,
//       `bin`, `pad`, `print`, `format`, and formatted strings: the names base.md §3.5
//       reserves for the language. They parse as ordinary calls; only their checked types and
//       semantics are special.
//
//==============================================================================================

#include "check/check.h"
#include "check/checker.h"

#include "support/literal.h"

namespace lucb {

auto Checker::type_from_expr_or_name(Node* a) -> Type* {
    if (a == nullptr) {
        return t_error();
    }
    if (a->kind == NodeKind::Type) {
        return resolve_type(a);
    }
    if (a->kind == NodeKind::Name) {
        Type* named = named_scalar(a->text);
        if (named != nullptr && lookup(a->text) == nullptr) {
            a->ty = named;
            return named;
        }
        Binding* b = lookup(a->text);
        if (b != nullptr && b->decl != nullptr &&
            (b->decl->kind == NodeKind::Struct || b->decl->kind == NodeKind::Enum ||
             b->decl->kind == NodeKind::Union || b->decl->kind == NodeKind::TypeAlias)) {
            a->ty = b->type;
            a->resolved = b->decl;
            return b->type;
        }
    }
    if (a->kind == NodeKind::Member && a->left != nullptr && a->left->kind == NodeKind::Name) {
        // `c.long` or `module.Type` names a type; anything else is a value to measure
        if (a->left->text == "c") {
            Type* ct = imported_c_type(a, keep("c." + string(a->text)));
            if (ct != nullptr) {
                a->ty = ct;
                return ct;
            }
        }
        Binding* b = lookup(a->left->text);
        if (b != nullptr && b->type != nullptr && b->type->kind == TypeKind::Module) {
            Node* p = pub_member(b->decl, a->text);
            if (p != nullptr && (p->kind == NodeKind::Struct || p->kind == NodeKind::Enum ||
                                 p->kind == NodeKind::Union || p->kind == NodeKind::TypeAlias)) {
                mark_import(b);
                Type* t = p->kind == NodeKind::TypeAlias ? resolve_alias(p) : decl_type(p);
                a->ty = t;
                a->resolved = p;
                return t;
            }
        }
    }
    return check_expr(a, nullptr);
}

auto Checker::check_sizeof(Node* n) -> Type* {
    if (count_args(n->body) != 1) {
        fail_n(n, "lucb.check.call", "`sizeof` takes one argument");
        return t_usize();
    }
    Type* t = type_from_expr_or_name(n->body->left);
    if (t->kind == TypeKind::Error) {
        return t_usize();
    }
    n->body->left->ty = t;
    return t_usize();
}

auto Checker::check_assert(Node* n) -> Type* {
    n->resolved = nullptr;
    int count = count_args(n->body);
    if (count < 1 || count > 2) {
        fail_n(n, "lucb.check.call", "`assert` takes a condition");
        return t_unit();
    }
    Type* c = check_expr(n->body->left, t_bool());
    if (!type_eq(c, t_bool())) {
        fail_n(n, "lucb.check.type", "`assert` needs a `bool`");
    }
    if (count == 2 && n->body->next != nullptr) {
        Type* m = check_expr(n->body->next->left, t_str());
        if (!type_eq(m, t_str())) {
            fail_n(n, "lucb.check.type", "`assert` message must be `str`");
        }
    }
    return t_unit();
}

auto Checker::check_offsetof(Node* n) -> Type* {
    int count = count_args(n->body);
    if (count != 2) {
        fail_n(n, "lucb.check.call", "`offsetof` takes a type and a field");
        return t_usize();
    }
    Type* t = type_from_expr_or_name(n->body->left);
    Node* field = n->body->next != nullptr ? n->body->next->left : nullptr;
    if (t->kind != TypeKind::Struct && !is_union(t)) {
        fail_n(n, "lucb.check.type", "`offsetof` needs a struct or union");
        return t_usize();
    }
    if (field == nullptr || field->kind != NodeKind::Name) {
        fail_n(n, "lucb.check.type", "`offsetof` needs a field name");
        return t_usize();
    }
    Node* f = struct_member(t->decl, field->text, NodeKind::Field);
    if (f == nullptr) {
        fail_n(n, "lucb.check.name", "no field `" + string(field->text) + "`");
    }
    n->body->left->ty = t;
    field->resolved = f;
    return t_usize();
}

auto Checker::check_alignof(Node* n) -> Type* {
    if (count_args(n->body) != 1) {
        fail_n(n, "lucb.check.call", "`alignof` takes one type");
        return t_usize();
    }
    Type* t = type_from_expr_or_name(n->body->left);
    n->body->left->ty = t;
    return t_usize();
}

auto Checker::is_display(Type* t) -> bool {
    if (t == nullptr) {
        return false;
    }
    return is_int(t) || is_float(t) || t->kind == TypeKind::Bool || t->kind == TypeKind::Str ||
           t->kind == TypeKind::Char || is_ptr(t) || t->kind == TypeKind::Fmt;
}

// `==` exists where every component supports it; unions and interface views never do (§7.4).
auto Checker::has_equality(Type* t) -> bool {
    if (t == nullptr) {
        return false;
    }
    if (t->kind == TypeKind::Union || t->kind == TypeKind::Interface || t->kind == TypeKind::Fmt) {
        return false;
    }
    if ((is_array(t) || is_opt(t)) && !is_ptr(t) && !is_func(t)) {
        return has_equality(t->elem);
    }
    if (is_tup(t)) {
        for (int i = 0; i < t->ntargs; i++) {
            if (!has_equality(t->args[i])) {
                return false;
            }
        }
        return true;
    }
    if ((t->kind == TypeKind::Struct || t->kind == TypeKind::Enum) && t->decl != nullptr) {
        for (Node* m = t->decl->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::Field && !has_equality(m->ty)) {
                return false;
            }
            for (Node* p = m->kind == NodeKind::EnumCase ? m->body : nullptr; p != nullptr; p = p->next) {
                if (!has_equality(p->ty)) {
                    return false;
                }
            }
        }
    }
    return true;
}

auto Checker::is_hashable(Type* t) -> bool {
    if (t == nullptr) {
        return false;
    }
    if (t->kind == TypeKind::Param && (t->bounds & BoundHashable) != 0) {
        return true;
    }
    if (is_int(t) || is_float(t) || t->kind == TypeKind::Bool || t->kind == TypeKind::Char ||
        t->kind == TypeKind::Str || is_ptr(t)) {
        return true;
    }
    if (is_array(t) || is_opt(t)) {
        return is_hashable(t->elem);
    }
    if (is_tup(t)) {
        for (int i = 0; i < t->ntargs; i++) {
            if (!is_hashable(t->args[i])) {
                return false;
            }
        }
        return t->ntargs > 0;
    }
    if (t->kind == TypeKind::Struct && t->decl != nullptr) {
        for (Node* m = t->decl->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::Field && !is_hashable(m->ty)) {
                return false;
            }
        }
        return true;
    }
    if (t->kind == TypeKind::Enum && t->decl != nullptr) {
        if (is_int_enum(t)) {
            return true;
        }
        for (Node* c = t->decl->body; c != nullptr; c = c->next) {
            if (c->kind != NodeKind::EnumCase) {
                continue;
            }
            for (Node* p = c->body; p != nullptr; p = p->next) {
                if (!is_hashable(p->ty)) {
                    return false;
                }
            }
        }
        return true;
    }
    return false;
}

auto Checker::check_hash(Node* n) -> Type* {
    n->resolved = nullptr;
    if (count_args(n->body) != 1) {
        fail_n(n, "lucb.check.call", "`hash` takes one argument");
        return ty_u64;
    }
    Type* a = check_expr(n->body->left, nullptr);
    if (a != nullptr && a->kind == TypeKind::UntypedInt) {
        a = coerce(n->body->left, a, t_i64());
        n->body->left->ty = a;
    }
    if (!is_hashable(a)) {
        fail_n(n, "lucb.check.type", "this value cannot be hashed");
    }
    return ty_u64;
}

auto Checker::check_hex(Node* n) -> Type* {
    n->resolved = nullptr;
    if (count_args(n->body) != 1) {
        fail_n(n, "lucb.check.call", "`hex` takes one argument");
        return ty_fmt;
    }
    Type* a = check_expr(n->body->left, nullptr);
    if (a != nullptr && a->kind == TypeKind::UntypedInt) {
        a = coerce(n->body->left, a, t_i64());
        n->body->left->ty = a;
    }
    if (!is_int(a) && !is_ptr(a) && (a == nullptr || a->kind != TypeKind::Char)) {
        fail_n(n, "lucb.check.type", "`hex` takes an integer or a pointer");
    }
    return ty_fmt;
}

auto Checker::check_bin(Node* n) -> Type* {
    n->resolved = nullptr;
    if (count_args(n->body) != 1) {
        fail_n(n, "lucb.check.call", "`bin` takes one argument");
        return ty_fmt;
    }
    Type* a = check_expr(n->body->left, nullptr);
    if (a != nullptr && a->kind == TypeKind::UntypedInt) {
        a = coerce(n->body->left, a, t_i64());
        n->body->left->ty = a;
    }
    if (!is_int(a) && (a == nullptr || a->kind != TypeKind::Char)) {
        fail_n(n, "lucb.check.type", "`bin` takes an integer");
    }
    return ty_fmt;
}

auto Checker::check_pad(Node* n) -> Type* {
    n->resolved = nullptr;
    if (count_args(n->body) != 2) {
        fail_n(n, "lucb.check.call", "`pad` takes a value and a width");
        return ty_fmt;
    }
    Type* a = check_expr(n->body->left, nullptr);
    if (a != nullptr && a->kind == TypeKind::UntypedInt) {
        a = coerce(n->body->left, a, t_i64());
        n->body->left->ty = a;
    }
    if (!is_display(a)) {
        fail_n(n, "lucb.check.type", "`pad` needs a displayable value");
    }
    Type* w = check_expr(n->body->next != nullptr ? n->body->next->left : nullptr, t_usize());
    if (w != nullptr && w->kind == TypeKind::UntypedInt) {
        w = coerce(n->body->next->left, w, t_usize());
        n->body->next->left->ty = w;
    }
    if (!is_int(w) && (w == nullptr || w->kind != TypeKind::Usize)) {
        fail_n(n, "lucb.check.type", "`pad` width must be `usize`");
    }
    return ty_fmt;
}

auto Checker::check_formatted(Node* n) -> Type* {
    for (Node* p = n->body; p != nullptr; p = p->next) {
        if (p->kind == NodeKind::FormatField) {
            Type* ft = check_expr(p->left, nullptr);
            if (ft != nullptr && ft->kind == TypeKind::UntypedInt) {
                ft = coerce(p->left, ft, t_i64());
                p->left->ty = ft;
            }
            if (is_atomic(ft)) {
                // a plain read of `@T` is a load (§15.1): the field displays as its `T`
                ft = ft->elem;
                p->left->ty = ft;
            }
            if (is_display(ft)) {
                continue;
            }
            if (displays_itself(ft)) {
                display_through_sink(p);
                continue;
            }
            fail_n(p, "lucb.check.type", "this value cannot be formatted");
        }
    }
    return ty_fmt;
}

// The `Display` protocol of the `luce` module (§14.4).
auto Checker::display_iface() -> Type* {
    Binding* b = lookup("luce");
    Node* d = b != nullptr && b->decl != nullptr ? pub_member(b->decl, "Display") : nullptr;
    return d != nullptr ? d->ty : nullptr;
}

auto Checker::displays_itself(Type* t) -> bool {
    return t != nullptr && t->kind == TypeKind::Struct && t->decl != nullptr &&
           struct_implements(t->decl, display_iface());
}

// A field whose value writes itself (§14.4) becomes the call `value.display(__sink)`, where
// `__sink` stands for the buffer the formatted string is being built in; the interpreter and
// the emitter supply that sink where they build the string.
auto Checker::display_through_sink(Node* field) -> void {
    Node* sink = arena->make<Node>();
    sink->kind = NodeKind::Name;
    sink->span = field->span;
    sink->text = keep("__sink");
    sink->flags = FlagFormatSink;
    Node* arg = arena->make<Node>();
    arg->kind = NodeKind::Param;
    arg->span = field->span;
    arg->left = sink;
    Node* call = syn_call(field->left, "display", field->span);
    call->body = arg;
    call->flags |= FlagFormatSink;
    field->left = call;
    field->flags |= FlagFormatSink;
    check_expr(call, nullptr);
}

auto Checker::check_print(Node* n) -> Type* {
    n->resolved = nullptr;
    int count = count_args(n->body);
    if (count != 1) {
        fail_n(n, "lucb.check.call", "`print` takes one argument");
        return t_unit();
    }
    Type* a = check_expr(n->body->left, nullptr);
    if (is_atomic(a)) {
        a = a->elem;
        n->body->left->ty = a;
    }
    if (a != nullptr && a->kind == TypeKind::UntypedInt) {
        a = coerce(n->body->left, a, t_i64());
        n->body->left->ty = a;
    }
    if (!is_int(a) && !is_float(a) && a->kind != TypeKind::Bool && a->kind != TypeKind::Str &&
        a->kind != TypeKind::Char && a->kind != TypeKind::Fmt) {
        fail_n(n, "lucb.check.type", "`print` takes a scalar, `str`, or a formatted string");
    }
    return t_unit();
}

auto Checker::check_format(Node* n) -> Type* {
    n->resolved = nullptr;
    if (count_args(n->body) != 2) {
        fail_n(n, "lucb.check.call", "`format` takes a buffer and a formatted string");
        return intern_fail(t_str());
    }
    Type* buf = check_expr(n->body->left, intern_sp(ty_u8, false));
    if (!is_span(buf) || buf->elem == nullptr || buf->elem->kind != TypeKind::U8) {
        fail_n(n, "lucb.check.type", "`format` needs a `u8[]` buffer");
    }
    Type* msg = check_expr(n->body->next != nullptr ? n->body->next->left : nullptr, ty_fmt);
    if (msg != nullptr && msg->kind != TypeKind::Fmt && msg->kind != TypeKind::Str) {
        fail_n(n, "lucb.check.type", "`format` needs a formatted string or `str`");
    }
    // the text views the buffer: a local array, or a span derived from one, makes the result
    // local (§6.6); a span parameter views the caller's memory
    Node* buffer = n->body->left;
    if (is_local(buffer) || (is_array(buffer->ty) && place_is_local(buffer))) {
        mark_local(n);
    }
    return intern_fail(t_str());
}

auto Checker::check_error(Node* n) -> Type* {
    if (!fallible_fn && !in_catch) {
        fail_n(n, "lucb.check.type", "`error` is only valid in a fallible function or catch");
    }
    int count = count_args(n->body);
    if (count != 2) {
        fail_n(n, "lucb.check.call", "`error` takes a code and a message");
    } else {
        Type* code = check_expr(n->body->left, ty_errcode);
        if (code != nullptr && code->kind != TypeKind::ErrorCode && !is_int(code) &&
            code->kind != TypeKind::UntypedInt) {
            fail_n(n->body, "lucb.check.type", "`error` code must be `ErrorCode`");
        }
        Node* message = n->body->next != nullptr ? n->body->next->left : nullptr;
        Type* msg = check_expr(message, t_str());
        if (!type_eq(msg, t_str())) {
            fail_n(n, "lucb.check.type", "`error` message must be `str`");
        }
        if (is_local(message)) {
            // §6.6: the message outlives the frame that raised the error
            fail_n(message, "lucb.check.escape", "an error message must not name a local");
        }
    }
    return t_never();
}

auto Checker::check_error_code_package(Node* n) -> Type* {
    if (!in_top_const) {
        fail_n(n, "lucb.check.type",
               "`ErrorCode.package` is only a top-level constant initialiser");
        return ty_errcode != nullptr ? ty_errcode : t_error();
    }
    if (count_args(n->body) != 1) {
        fail_n(n, "lucb.check.call", "`ErrorCode.package` takes one integer");
        return ty_errcode != nullptr ? ty_errcode : t_error();
    }
    Node* arg = n->body != nullptr ? n->body->left : nullptr;
    Type* at = check_expr(arg, ty_u32);
    (void)at;
    uint64_t v = 0;
    if (!const_u64(arg, &v)) {
        fail_n(n, "lucb.check.type", "`ErrorCode.package` needs a constant");
        return ty_errcode != nullptr ? ty_errcode : t_error();
    }
    if (v > 0xffffffffu) {
        fail_n(n, "lucb.check.number", "`ErrorCode.package` needs a `u32`");
        return ty_errcode != nullptr ? ty_errcode : t_error();
    }
    uint32_t code = static_cast<uint32_t>(v);
    for (size_t i = 0; i < package_codes.size(); i++) {
        if (package_codes[i] == code) {
            fail_n(n, "lucb.check.shadow", "duplicate `ErrorCode.package` value");
            break;
        }
    }
    package_codes.push_back(code);
    // the value carries the package's identity in its high half (§11.3)
    n->cached = (static_cast<uint64_t>(package_identity(package_name)) << 16) | code;
    n->flags |= FlagPackageCode;
    if (n->left != nullptr) {
        n->left->ty = intern_func(&ty_u32, 1, ty_errcode, false);
        n->left->resolved = nullptr;
    }
    return ty_errcode;
}

auto Checker::check_trap(Node* n) -> Type* {
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

auto Checker::check_discard(Node* n) -> Type* {
    n->resolved = nullptr;
    if (count_args(n->body) != 1) {
        fail_n(n, "lucb.check.call", "`discard` takes one argument");
        return t_unit();
    }
    Type* t = check_expr(n->body->left, nullptr);
    if (is_fail(t)) {
        fail_n(n, "lucb.check.type", "handle this failure with `try` or `catch`");
    }
    return t_unit();
}

} // namespace lucb
