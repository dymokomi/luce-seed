//==============================================================================================
//
//   check/handled - A caught failure's message stays in its handler
//
//   DESCRIPTION:
//       The message of a failure a `catch` handles lives in storage the thread keeps until
//       that handler finishes (base.md §11.3). A view of it may be read, passed down and
//       raised again, but not returned, recovered or stored where it outlives the handler:
//       a message kept is copied first. The same rule as luce-base's check/handled.
//
//==============================================================================================

#include "check/checker.h"

namespace lucb {

namespace {

const char* const k_copy_hint =
    "; copy it to keep it: `strings.copy(failure.message)` or an Owned str";

// Whether `n`, a place, is `self` or a field or element of it.
auto rooted_at_self(const Node* n) -> bool {
    while (n != nullptr && (n->kind == NodeKind::Member || n->kind == NodeKind::Index || n->kind == NodeKind::Group)) {
        n = n->left;
    }
    return n != nullptr && n->kind == NodeKind::Self;
}

// Whether the statements from `n` on assign to `self` or a part of it: the seed's reading of
// a method that changes its receiver, which luce-base infers in full (§9.5).
auto assigns_self(const Node* n) -> bool {
    for (; n != nullptr; n = n->next) {
        if (n->kind == NodeKind::Assign && rooted_at_self(n->left)) {
            return true;
        }
        if (assigns_self(n->body) || (n->kind != NodeKind::Lambda && (assigns_self(n->right) || assigns_self(n->left)))) {
            return true;
        }
    }
    return false;
}

// Whether a method parameter takes its generic type's element itself, `T` or `T?`, which the
// method may keep; a `const T[]` is read and copied from, as `assign` does.
auto is_element_parameter(const Type* t) -> bool {
    if (t != nullptr && t->kind == TypeKind::Optional) {
        t = t->elem;
    }
    return t != nullptr && t->kind == TypeKind::Param;
}

} // namespace

// The depth of the handler whose failure's message `n` views, 0 when it views none: the
// failure, its message, a field, slice or address of either, a name bound to one, and a
// tuple, array or construction holding one. Like §6.6's rule, it stops at calls.
auto Checker::handled_view(Node* n) -> int {
    if (n == nullptr) {
        return 0;
    }
    switch (n->kind) {
    case NodeKind::Name: {
        Binding* b = lookup(n->text);
        return b != nullptr ? b->handled : 0;
    }
    case NodeKind::Group:
    case NodeKind::Cast:
    case NodeKind::Propagate:
        return handled_view(n->left);
    case NodeKind::Catch: {
        // the handled value, or what its handler recovers of an enclosing handler's
        int deepest = handled_view(n->left);
        for (const auto& [at, depth] : recovered_views) {
            if (at == n && depth > deepest) {
                deepest = depth;
            }
        }
        return deepest;
    }
    case NodeKind::Member:
    case NodeKind::Index:
    case NodeKind::Slice:
        if (n->left == nullptr || is_ptr(n->left->ty)) {
            return 0; // what a pointer reaches is not the pointer
        }
        return handled_view(n->left);
    case NodeKind::Unary:
        return n->op == TokenKind::Amp ? handled_view(n->left) : 0;
    case NodeKind::Conditional:
    case NodeKind::Else:
        return std::max(handled_view(n->left), handled_view(n->right));
    case NodeKind::Tuple:
    case NodeKind::ArrayLit: {
        int deepest = 0;
        for (Node* m = n->body; m != nullptr; m = m->next) {
            deepest = std::max(deepest, handled_view(m));
        }
        return deepest;
    }
    case NodeKind::Call: {
        // a construction or an enum case holds its arguments; a call returns its own
        Node* r = n->resolved;
        const bool holds = r != nullptr && (r->kind == NodeKind::Struct || r->kind == NodeKind::EnumCase ||
                                            (r->kind == NodeKind::Func && r->text == "init"));
        if (!holds) {
            return 0;
        }
        int deepest = 0;
        for (Node* a = n->body; a != nullptr; a = a->next) {
            deepest = std::max(deepest, handled_view(a->left));
        }
        return deepest;
    }
    default:
        return 0;
    }
}

// Record that the binding `name` views the message of the handler at `depth`. Not a use of
// the name, so not `lookup`.
auto Checker::set_handled(string_view name, int depth) -> void {
    auto it = scope_index.find(name);
    if (it == scope_index.end()) {
        return;
    }
    Binding& b = scope[static_cast<size_t>(it->second)];
    b.handled = std::max(b.handled, depth);
}

// The local whose own storage `place` is part of: the variable itself, or one of its fields
// or elements; null for a global, `self`, or what a pointer or view reaches.
auto Checker::handled_root(Node* place) -> Binding* {
    if (place == nullptr) {
        return nullptr;
    }
    switch (place->kind) {
    case NodeKind::Name: {
        Binding* b = lookup(place->text);
        if (b == nullptr || b->depth == 0 || b->decl == nullptr || b->decl->kind == NodeKind::Global ||
            b->decl->kind == NodeKind::Const) {
            return nullptr;
        }
        return b;
    }
    case NodeKind::Group:
        return handled_root(place->left);
    case NodeKind::Member:
    case NodeKind::Index: {
        Type* ot = place->left != nullptr ? place->left->ty : nullptr;
        if (ot == nullptr || is_ptr(ot) || is_span(ot) || ot->kind == TypeKind::Str ||
            ot->kind == TypeKind::Module) {
            return nullptr;
        }
        return handled_root(place->left);
    }
    default:
        return nullptr;
    }
}

// `recover value`: a view of the handler's own message may not leave it; one of an
// enclosing handler's message makes the `catch` expression's value view it too.
auto Checker::handled_recovered(Node* value, Type* want) -> void {
    const int depth = handled_view(value);
    if (depth == 0 || !holds_view(want)) {
        return;
    }
    if (depth < catch_depth) {
        recovered_views.emplace_back(catch_node, depth);
        return;
    }
    handled_leaves(value, want, "recovered");
}

// `return value` and `recover value`: both leave the handler, so a view of its message may
// not go with them.
auto Checker::handled_leaves(Node* value, Type* want, const char* leaving) -> void {
    if (handled_view(value) > 0 && holds_view(want)) {
        fail_n(value, "lucb.check.escape",
               string("a caught failure's message lives until its handler finishes and must not be ") + leaving + k_copy_hint);
    }
}

// `place = value`: a view of a handled message is stored only in a local declared in its
// handler, which then views it too.
auto Checker::handled_stored(Node* place, Node* value, Type* want) -> void {
    const int depth = handled_view(value);
    if (depth == 0 || !holds_view(want)) {
        return;
    }
    Binding* root = handled_root(place);
    if (root != nullptr && root->depth > depth) {
        root->handled = std::max(root->handled, depth);
        return;
    }
    fail_n(value, "lucb.check.escape",
           string("a caught failure's message lives until its handler finishes and must not be stored where it outlives the handler") + k_copy_hint);
}

// `container.method(value)`: a generic type's method that changes its receiver and takes its
// element, `T`, keeps the value, as `List[str].push` does; a view of a handled message may go only into a container
// declared in its handler.
auto Checker::handled_kept(Node* method, Node* receiver, Node* argument, int index) -> void {
    if (receiver == nullptr || argument == nullptr) {
        return;
    }
    const int depth = handled_view(argument->left);
    if (depth == 0 || !holds_view(argument->left->ty)) {
        return;
    }
    Type* rt = receiver->ty;
    Type* owner = is_ptr(rt) && rt->elem != nullptr ? rt->elem : rt;
    if (owner == nullptr || owner->kind != TypeKind::Struct || owner->decl == nullptr) {
        return;
    }
    Node* origin = generic_origin(owner->decl);
    if (origin == owner->decl) {
        return;
    }
    Node* declared = struct_member(origin, method->text, NodeKind::Func);
    if (declared == nullptr || ((declared->flags & FlagMutating) == 0 && !assigns_self(declared->body))) {
        return;
    }
    Node* p = declared != nullptr ? declared->right : nullptr;
    for (int i = 0; i < index && p != nullptr; i++) {
        p = p->next;
    }
    if (p == nullptr || !is_element_parameter(p->ty)) {
        return;
    }
    if (!is_ptr(rt)) {
        Binding* root = handled_root(receiver);
        if (root != nullptr && root->depth > depth) {
            root->handled = std::max(root->handled, depth);
            return;
        }
    }
    fail_n(argument->left, "lucb.check.escape",
           string("a caught failure's message lives until its handler finishes and must not be kept in a container that outlives the handler") + k_copy_hint);
}

} // namespace lucb
