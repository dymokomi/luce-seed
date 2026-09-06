//==============================================================================================
//
//   check/stmt - Statements and control flow
//
//   DESCRIPTION:
//       `let`/`var` with zero values and `---`, assignment and compound assignment, `if`/`if
//       let`, `while`/`while let`, `for` over ranges, arrays, and spans, labeled
//       `break`/`continue`, `return`, `defer`/`errdefer`, `recover`, and the definite-return
//       rule (base.md §6, §8).
//
//==============================================================================================

#include "check/checker.h"

namespace lucb {

// `for x in source: body` over an Iterable (§8.3) becomes, in its own block,
// `var __iterN = source.iterator()` and `while let x = __iterN.next(): body`: the iterator
// lives in a hidden local of its concrete type, and the interpreter and the emitter see
// only a loop they already know.
auto Checker::desugar_iterable_for(Node* n) -> void {
    if (n->left != nullptr) {
        fail_n(n, "lucb.check.type", "an Iterable yields one value per step; bind one name");
    }
    string_view hidden = keep("__iter" + std::to_string(++hidden_count));
    Node* start = arena->make<Node>();
    start->kind = NodeKind::Var;
    start->span = n->span;
    start->text = hidden;
    start->left = syn_call(n->right, "iterator", n->span);
    Node* advance = arena->make<Node>();
    advance->kind = NodeKind::Name;
    advance->span = n->span;
    advance->text = hidden;
    Node* step = arena->make<Node>();
    step->kind = NodeKind::Let;
    step->flags = FlagIfLet;
    step->span = n->span;
    step->text = n->text;
    step->type = n->type;
    step->left = syn_call(advance, "next", n->span);
    Node* loop = arena->make<Node>();
    loop->kind = NodeKind::While;
    loop->flags = FlagIfLet;
    loop->span = n->span;
    loop->label = n->label;
    loop->left = step;
    loop->body = n->body;
    start->next = loop;
    n->kind = NodeKind::Block;
    n->text = {};
    n->label = {};
    n->type = nullptr;
    n->left = nullptr;
    n->right = nullptr;
    n->body = start;
}

// The call `receiver.method()` as a fresh tree at `span`.
auto Checker::syn_call(Node* receiver, const char* method, Span span) -> Node* {
    Node* member = arena->make<Node>();
    member->kind = NodeKind::Member;
    member->span = span;
    member->text = keep(method);
    member->left = receiver;
    Node* call = arena->make<Node>();
    call->kind = NodeKind::Call;
    call->span = span;
    call->left = member;
    return call;
}

auto Checker::check_stmt(Node* n) -> void {
    if (n == nullptr) {
        return;
    }
    switch (n->kind) {
    case NodeKind::Block:
        push_scope();
        for (Node* s = n->body; s != nullptr; s = s->next) {
            check_stmt(s);
            if (s->next != nullptr && pruning_enabled() && always_returns(s)) {
                warn_n(s->next, "lucb.warn.dead", "unreachable code");
                s->next = nullptr;
            }
        }
        pop_scope();
        prune_block(n);
        break;
    case NodeKind::Let:
    case NodeKind::Var: {
        if (n->body != nullptr && n->text.empty()) {
            Type* init = n->left != nullptr ? check_expr(n->left, nullptr) : t_error();
            if (!is_tup(init)) {
                fail_n(n, "lucb.check.type", "tuple binding needs a tuple");
                break;
            }
            int nnames = 0;
            for (Node* nm = n->body; nm != nullptr; nm = nm->next) {
                nnames++;
            }
            if (nnames != init->ntargs) {
                fail_n(n, "lucb.check.type", "wrong number of tuple names");
            }
            int i = 0;
            for (Node* nm = n->body; nm != nullptr; nm = nm->next) {
                Type* et = i < init->ntargs ? init->args[i] : t_error();
                nm->ty = et;
                bind(nm->text, et, n->kind == NodeKind::Var, nm);
                i++;
            }
            n->ty = init;
            break;
        }
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
            if (init != nullptr && init->kind == TypeKind::Unit) {
                fail_n(n->left, "lucb.check.type", "this expression has no value");
            }
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
                fail_n(n, "lucb.check.type",
                       "initialiser has type " + type_name(init) + ", expected " + type_name(t));
            }
        } else if (t == nullptr) {
            fail_n(n, "lucb.check.type", "this binding needs a type or an initialiser");
            t = t_error();
        } else if (n->kind == NodeKind::Let) {
            fail_n(n, "lucb.check.type", "`let` needs an initialiser");
        } else if (!is_zeroable(t)) {
            fail_n(n, "lucb.check.type", "this type has no zero value; write an initialiser");
        }
        n->ty = t;
        if (t != nullptr && t->kind == TypeKind::Fmt) {
            fail_n(n, "lucb.check.type", "`fmt` cannot be stored");
        }
        if (is_fail(t)) {
            fail_n(n, "lucb.check.type", "`T!` cannot be stored");
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
        Type* dest = is_atomic(lt) ? lt->elem : lt;
        if (n->op == TokenKind::Eq) {
            Type* rt = check_expr(n->right, dest);
            if (!type_eq(dest, rt) && !can_widen(rt, dest) &&
                !can_ptr_convert(rt, dest, n->right)) {
                fail_n(n, "lucb.check.type", "assignment type mismatch");
            }
        } else {
            Type* rt = check_expr(n->right, dest);
            if (is_atomic(lt)) {
                if (n->op != TokenKind::PlusEq && n->op != TokenKind::MinusEq &&
                    n->op != TokenKind::AmpEq && n->op != TokenKind::PipeEq &&
                    n->op != TokenKind::CaretEq) {
                    fail_n(n, "lucb.check.type",
                           "an atomic compound assignment is `+=` `-=` `|=` `&=` or `^=`");
                }
                if (!is_int(dest) && dest->kind != TypeKind::Bool && !is_ptr(dest)) {
                    fail_n(n, "lucb.check.type", "compound assignment requires a number");
                }
            } else if (!is_int(lt) || (!is_int(rt) && rt->kind != TypeKind::UntypedInt)) {
                if (!(is_float(lt) && is_float(rt))) {
                    fail_n(n, "lucb.check.type", "compound assignment requires a number");
                }
            }
        }
        break;
    }
    case NodeKind::Asm:
        check_asm(n);
        break;
    case NodeKind::If: {
        if (n->flags & FlagIfLet) {
            Node* let = n->left;
            Type* ot = check_expr(let != nullptr ? let->left : nullptr, nullptr);
            if (!is_opt(ot) && !(is_ptr(ot) && ot->is_nullable)) {
                fail_n(n, "lucb.check.type", "`if let` needs an optional");
            }
            Type* payload =
                is_opt(ot) ? ot->elem : intern_ptr(ot->elem, ot->is_const, ot->is_volatile, false);
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
        if ((n->flags & FlagIfLet) == 0) {
            fold_constant_branch(n);
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
            Type* payload =
                is_opt(ot) ? ot->elem : intern_ptr(ot->elem, ot->is_const, ot->is_volatile, false);
            loop_labels.push_back(n->label);
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
            loop_labels.push_back(n->label);
            check_stmt(n->body);
            loop_labels.pop_back();
        }
        if ((n->flags & FlagIfLet) == 0) {
            fold_constant_branch(n);
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
            fail_n(n, "lucb.check.type",
                   "return type is " + type_name(t) + ", expected " + type_name(return_type));
        }
        if (n->left != nullptr && is_local(n->left) &&
            (is_ptr(return_type) || is_span(return_type) ||
             (return_type != nullptr && return_type->kind == TypeKind::Str))) {
            fail_n(n, "lucb.check.escape", "this pointer or view must not escape the function");
        }
        break;
    }
    case NodeKind::For: {
        Type* it =
            n->right != nullptr && n->right->kind == NodeKind::Binary &&
                    (n->right->op == TokenKind::DotDotLt || n->right->op == TokenKind::DotDotEq)
                ? nullptr
                : check_expr(n->right, nullptr);
        Type* elem = nullptr;
        if (n->right != nullptr && n->right->kind == NodeKind::Binary &&
            (n->right->op == TokenKind::DotDotLt || n->right->op == TokenKind::DotDotEq)) {
            Type* annotated = n->type != nullptr ? resolve_type(n->type) : nullptr;
            Type* hint = (annotated != nullptr && is_int(annotated)) ? annotated : nullptr;
            Type* a = check_expr(n->right->left, hint);
            Type* b = check_expr(n->right->right, hint);
            Type* u = hint != nullptr ? hint : unify_int(a, b);
            if (u == nullptr || !is_int(u)) {
                u = t_i64();
            }
            if (a != nullptr && a->kind == TypeKind::UntypedInt) {
                n->right->left->ty = coerce(n->right->left, a, u);
            }
            if (b != nullptr && b->kind == TypeKind::UntypedInt) {
                n->right->right->ty = coerce(n->right->right, b, u);
            }
            elem = u;
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
        } else if (it != nullptr && (it->kind == TypeKind::Struct || it->kind == TypeKind::Enum) &&
                   struct_member(it->decl, "iterator", NodeKind::Func) != nullptr) {
            desugar_iterable_for(n);
            check_stmt(n);
            break;
        } else {
            fail_n(n, "lucb.check.type", "`for` iterates a span, an array, a range, text, or an Iterable");
            elem = t_error();
        }
        if (n->type != nullptr) {
            Type* want = resolve_type(n->type);
            if (is_int(want) && (elem == nullptr || is_int(elem) ||
                                 (elem != nullptr && elem->kind == TypeKind::UntypedInt))) {
                if (n->right != nullptr && n->right->kind == NodeKind::Binary &&
                    (n->right->op == TokenKind::DotDotLt || n->right->op == TokenKind::DotDotEq)) {
                    if (n->right->left != nullptr) {
                        n->right->left->ty = coerce(n->right->left, n->right->left->ty, want);
                    }
                    if (n->right->right != nullptr) {
                        n->right->right->ty = coerce(n->right->right, n->right->right->ty, want);
                    }
                    n->right->ty = want;
                }
                elem = want;
            } else if (!type_eq(want, elem) && !can_widen(elem, want)) {
                fail_n(n, "lucb.check.type", "loop variable has the wrong type");
            } else {
                elem = want;
            }
        }
        n->ty = elem;
        loop_labels.push_back(n->label);
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
        if (n->left != nullptr && n->left->kind == NodeKind::Free) {
            check_free(n->left);
        } else {
            check_expr(n->left);
        }
        if (n->body != nullptr) {
            check_stmt(n->body);
        }
        break;
    case NodeKind::Errdefer:
        if (!fallible_fn) {
            fail_n(n, "lucb.check.type", "`errdefer` is only valid in a fallible function");
        }
        if (n->left != nullptr && n->left->kind == NodeKind::Free) {
            check_free(n->left);
        } else {
            check_expr(n->left);
        }
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

auto Checker::check_asm(Node* n) -> void {
    for (Node* op = n != nullptr ? n->left : nullptr; op != nullptr; op = op->next) {
        if (op->text == "options") {
            continue;
        }
        if (op->left != nullptr) {
            Type* t = check_expr(op->left, nullptr);
            if (t != nullptr && t->kind == TypeKind::UntypedInt) {
                t = coerce(op->left, t, t_i64());
                op->left->ty = t;
            }
        }
    }
}

// True if a `break` anywhere under `n` could leave the enclosing loop.
auto Checker::contains_break(Node* n) -> bool {
    if (n == nullptr) {
        return false;
    }
    if (n->kind == NodeKind::Break) {
        return true;
    }
    return contains_break(n->left) || contains_break(n->right) || contains_break(n->body) ||
           contains_break(n->type) || contains_break(n->next);
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
        return n->left != nullptr && n->left->ty != nullptr && n->left->ty->kind == TypeKind::Never;
    case NodeKind::Block: {
        for (Node* s = n->body; s != nullptr; s = s->next) {
            if (always_returns(s)) {
                return true;
            }
        }
        return false;
    }
    case NodeKind::If:
        return always_returns(n->body) && always_returns(n->right);
    case NodeKind::With:
        return always_returns(n->body);
    case NodeKind::While:
        // `while true:` leaves only through `return`, `error`, or a `break`.
        return n->left != nullptr && n->left->kind == NodeKind::Literal &&
               n->left->op == TokenKind::KwTrue && !contains_break(n->body);
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
