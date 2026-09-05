#include "check/checker.h"

namespace lucb {

auto Checker::check_stmt(Node* n) -> void {
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
            if (n->flags & FlagUninit) {
                if (t == nullptr) {
                    fail_n(n, "lucb.check.type", "`---` needs a written type");
                    t = t_error();
                }
            } else if (n->left != nullptr) {
                Type* init = check_expr(n->left, t);
                if (is_fail(init) && (t == nullptr || !is_fail(t))) {
                    fail_n(n, "lucb.check.type", "handle this failure with `try` or `catch`");
                }
                if (t == nullptr && init != nullptr && init->kind == TypeKind::Fmt) {
                    fail_n(n, "lucb.check.type", "`fmt` cannot be stored");
                }
                if (t == nullptr) {
                    if (init != nullptr && init->kind == TypeKind::UntypedInt) {
                        init = coerce(n->left, init, t_i64());
                        n->left->ty = init;
                    }
                    t = init;
                } else if (!type_eq(t, init) && !can_widen(init, t)) {
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
            if (t != nullptr && t->kind == TypeKind::Fmt) {
                fail_n(n, "lucb.check.type", "`fmt` cannot be stored");
            }
            bind(n->text, t, n->kind == NodeKind::Var, n);
            if (n->left != nullptr && is_local(n->left)) {
                set_from_local(n->text, true);
            }
            break;
        }
        case NodeKind::Assign: {
            Type* lt = check_expr(n->left, nullptr);
            if (!is_mut_place(n->left)) {
                fail_n(n, "lucb.check.mut", "this place is not assignable");
            }
            if (n->op == TokenKind::Eq) {
                Type* rt = check_expr(n->right, lt);
                if (!type_eq(lt, rt) && !can_widen(rt, lt) && !can_ptr_convert(rt, lt, n->right)) {
                    fail_n(n, "lucb.check.type", "assignment type mismatch");
                }
            } else {
                Type* rt = check_expr(n->right, lt);
                if (!is_int(lt) || (!is_int(rt) && rt->kind != TypeKind::UntypedInt)) {
                    if (!(is_float(lt) && is_float(rt))) {
                        fail_n(n, "lucb.check.type", "compound assignment requires a number");
                    }
                }
            }
            break;
        }
        case NodeKind::If: {
            if (n->flags & FlagIfLet) {
                Node* let = n->left;
                Type* ot = check_expr(let != nullptr ? let->left : nullptr, nullptr);
                if (!is_opt(ot) && !(is_ptr(ot) && ot->is_nullable)) {
                    fail_n(n, "lucb.check.type", "`if let` needs an optional");
                }
                Type* payload = is_opt(ot) ? ot->elem
                                           : intern_ptr(ot->elem, ot->is_const, ot->is_volatile, false);
                push_scope();
                if (let != nullptr) {
                    bind(let->text, payload, false, let);
                }
                check_stmt(n->body);
                pop_scope();
            } else {
                Type* c = check_expr(n->left, t_bool());
                if (!type_eq(c, t_bool())) {
                    fail_n(n, "lucb.check.type", "a condition must be `bool`");
                }
                check_stmt(n->body);
            }
            if (n->right != nullptr) {
                check_stmt(n->right);
            }
            break;
        }
        case NodeKind::While: {
            if (n->flags & FlagIfLet) {
                Node* let = n->left;
                Type* ot = check_expr(let != nullptr ? let->left : nullptr, nullptr);
                if (!is_opt(ot) && !(is_ptr(ot) && ot->is_nullable)) {
                    fail_n(n, "lucb.check.type", "`while let` needs an optional");
                }
                Type* payload = is_opt(ot) ? ot->elem
                                           : intern_ptr(ot->elem, ot->is_const, ot->is_volatile, false);
                loop_labels.push_back(n->text);
                push_scope();
                if (let != nullptr) {
                    bind(let->text, payload, false, let);
                }
                check_stmt(n->body);
                pop_scope();
                loop_labels.pop_back();
            } else {
                Type* c = check_expr(n->left, t_bool());
                if (!type_eq(c, t_bool())) {
                    fail_n(n, "lucb.check.type", "a condition must be `bool`");
                }
                loop_labels.push_back(n->text);
                check_stmt(n->body);
                loop_labels.pop_back();
            }
            break;
        }
        case NodeKind::Return: {
            Type* t = t_unit();
            if (n->left != nullptr) {
                t = check_expr(n->left, return_type);
            }
            if (return_type != nullptr && !type_eq(t, return_type) && !can_widen(t, return_type) &&
                !can_ptr_convert(t, return_type, n->left)) {
                fail_n(n, "lucb.check.type", "return type is " + type_name(t) + ", expected " +
                                                 type_name(return_type));
            }
            if (n->left != nullptr && is_local(n->left) &&
                (is_ptr(return_type) || is_span(return_type) ||
                 (return_type != nullptr && return_type->kind == TypeKind::Str))) {
                fail_n(n, "lucb.check.escape", "this pointer or view must not escape the function");
            }
            break;
        }
        case NodeKind::For: {
            Type* it = n->right != nullptr && n->right->kind == NodeKind::Binary &&
                               (n->right->op == TokenKind::DotDotLt ||
                                n->right->op == TokenKind::DotDotEq)
                           ? nullptr
                           : check_expr(n->right, nullptr);
            Type* elem = nullptr;
            if (n->right != nullptr && n->right->kind == NodeKind::Binary &&
                (n->right->op == TokenKind::DotDotLt || n->right->op == TokenKind::DotDotEq)) {
                Type* a = check_expr(n->right->left, nullptr);
                Type* b = check_expr(n->right->right, nullptr);
                Type* u = unify_int(a, b);
                if (u == nullptr || (!is_int(u) && a->kind == TypeKind::UntypedInt &&
                                     b->kind == TypeKind::UntypedInt)) {
                    u = t_usize();
                    if (a->kind == TypeKind::UntypedInt) {
                        n->right->left->ty = coerce(n->right->left, a, u);
                    }
                    if (b->kind == TypeKind::UntypedInt) {
                        n->right->right->ty = coerce(n->right->right, b, u);
                    }
                }
                if (a->kind == TypeKind::UntypedInt) {
                    n->right->left->ty = coerce(n->right->left, a, u != nullptr && is_int(u) ? u : t_usize());
                }
                if (b->kind == TypeKind::UntypedInt) {
                    n->right->right->ty = coerce(n->right->right, b, u != nullptr && is_int(u) ? u : t_usize());
                }
                elem = (u != nullptr && is_int(u)) ? u : t_usize();
                n->right->ty = elem;
            } else if (n->flags & FlagByPtr) {
                if (is_array(it)) {
                    elem = intern_ptr(it->elem, !is_mut_place(n->right), false, false);
                } else if (is_span(it)) {
                    elem = intern_ptr(it->elem, it->is_const, false, false);
                } else {
                    fail_n(n, "lucb.check.type", "`for` over `&` needs an array or span");
                }
            } else if (is_array(it) || is_span(it)) {
                elem = it->elem;
            } else if (it != nullptr && it->kind == TypeKind::Str) {
                elem = ty_char;
            } else {
                fail_n(n, "lucb.check.type", "`for` needs an array, span, or `str`");
                elem = t_error();
            }
            if (n->type != nullptr) {
                Type* want = resolve_type(n->type);
                if (!type_eq(want, elem) && !can_widen(elem, want)) {
                    fail_n(n, "lucb.check.type", "loop variable has the wrong type");
                }
                elem = want;
            }
            n->ty = elem;
            loop_labels.push_back(n->text);
            push_scope();
            bind(n->text, elem, false, n);
            check_stmt(n->body);
            pop_scope();
            loop_labels.pop_back();
            break;
        }
        case NodeKind::Break:
        case NodeKind::Continue: {
            if (loop_labels.empty()) {
                fail_n(n, "lucb.check.type", "`break`/`continue` needs a loop");
            } else if (!n->text.empty()) {
                bool found = false;
                for (size_t i = 0; i < loop_labels.size(); i++) {
                    if (loop_labels[i] == n->text) {
                        found = true;
                    }
                }
                if (!found) {
                    fail_n(n, "lucb.check.name", "unknown loop label `" + string(n->text) + "`");
                }
            }
            break;
        }
        case NodeKind::Defer:
            check_expr(n->left);
            if (n->body != nullptr) {
                check_stmt(n->body);
            }
            break;
        case NodeKind::Errdefer:
            if (!fallible_fn) {
                fail_n(n, "lucb.check.type", "`errdefer` is only valid in a fallible function");
            }
            check_expr(n->left);
            break;
        case NodeKind::Recover:
            if (!in_catch) {
                fail_n(n, "lucb.check.type", "`recover` is only valid in a `catch` handler");
            }
            check_expr(n->left, catch_type != nullptr ? catch_type : return_type);
            break;
        case NodeKind::Match:
            check_match(n, nullptr);
            break;
        case NodeKind::Free:
            check_free(n);
            break;
        case NodeKind::With:
            check_with(n);
            break;
        case NodeKind::ExprStmt: {
            Type* t = check_expr(n->left);
            if (is_fail(t)) {
                fail_n(n, "lucb.check.type", "handle this failure with `try` or `catch`");
            }
            break;
        }
        default:
            fail_n(n, "lucb.check.unsupported", "this statement is not in the scalar core yet");
            break;
        }
    }

auto Checker::always_returns(Node* n) -> bool {
        if (n == nullptr) {
            return false;
        }
        switch (n->kind) {
        case NodeKind::Return:
            return true;
        case NodeKind::Recover:
            return true;
        case NodeKind::ExprStmt:
            if (n->left != nullptr && n->left->kind == NodeKind::Call && n->left->left != nullptr &&
                n->left->left->kind == NodeKind::Name &&
                (n->left->left->text == "trap" || n->left->left->text == "error")) {
                return true;
            }
            return n->left != nullptr && n->left->ty != nullptr &&
                   n->left->ty->kind == TypeKind::Never;
        case NodeKind::Block: {
            bool r = false;
            for (Node* s = n->body; s != nullptr; s = s->next) {
                r = always_returns(s);
            }
            return r;
        }
        case NodeKind::If:
            return always_returns(n->body) && always_returns(n->right);
        case NodeKind::With:
            return always_returns(n->body);
        case NodeKind::Match: {
            if (n->body == nullptr) {
                return false;
            }
            for (Node* arm = n->body; arm != nullptr; arm = arm->next) {
                if (!always_returns(arm->body)) {
                    return false;
                }
            }
            return true;
        }
        default:
            return false;
        }
    }

} // namespace lucb
