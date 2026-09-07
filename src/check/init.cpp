//==============================================================================================
//
//   check/init - The rules of a custom `init` (base.md §10.1)
//
//   DESCRIPTION:
//       A custom `init` returns `unit` or `!`, assigns every field exactly once, and neither
//       reads a field nor calls a method before every field is assigned. The body is walked
//       in order with the set of fields definitely assigned so far: an assignment to a field
//       already in the set may run twice; any other use of `self` while the set is short is
//       a read too early; a loop's body assigns no field, since it may run twice or not at
//       all; the branches of an `if` or `match` each start from the same set and contribute
//       what all of them assign.
//
//==============================================================================================

#include "check/checker.h"

namespace lucb {

namespace {

// The struct's fields in declaration order; the set is a bit per field, sixty-four at most.
int field_number(Node* owner, string_view name) {
    int i = 0;
    for (Node* m = owner->body; m != nullptr; m = m->next) {
        if (m->kind != NodeKind::Field) {
            continue;
        }
        if (m->text == name) {
            return i;
        }
        i += 1;
    }
    return -1;
}

uint64_t all_fields(Node* owner) {
    uint64_t set = 0;
    int i = 0;
    for (Node* m = owner->body; m != nullptr; m = m->next) {
        if (m->kind == NodeKind::Field) {
            set |= i < 64 ? (1ull << i) : 0;
            i += 1;
        }
    }
    return set;
}

// `self.name` as a store target: the field's number, or -1.
int assigned_field(Node* place, Node* owner) {
    if (place == nullptr || place->kind != NodeKind::Member || place->left == nullptr ||
        place->left->kind != NodeKind::Self) {
        return -1;
    }
    return field_number(owner, place->text);
}

bool mentions_self(Node* n) {
    if (n == nullptr) {
        return false;
    }
    if (n->kind == NodeKind::Self) {
        return true;
    }
    return mentions_self(n->left) || mentions_self(n->right) || mentions_self(n->body) ||
           mentions_self(n->type) || mentions_self(n->next);
}

} // namespace

// The walk: `assigned` is the set definitely assigned on entry and, on return, on exit.
struct InitWalk {
    Checker& checker;
    Node* owner;
    uint64_t every;

    void read_too_early(Node* at) {
        checker.fail_n(at, "lucb.check.init",
                       "`init` cannot read a field or call a method before every field is assigned");
    }

    // An expression evaluated while fields are missing must not touch `self`.
    void expression(Node* e, uint64_t assigned) {
        if (assigned != every && mentions_self(e)) {
            read_too_early(e);
        }
    }

    void statement(Node* s, uint64_t& assigned, bool in_loop) {
        if (s == nullptr) {
            return;
        }
        switch (s->kind) {
        case NodeKind::Block:
            for (Node* item = s->body; item != nullptr; item = item->next) {
                statement(item, assigned, in_loop);
            }
            return;
        case NodeKind::Assign: {
            int field = assigned_field(s->left, owner);
            if (field < 0 || s->op != TokenKind::Eq) {
                expression(s->left, assigned);
                expression(s->right, assigned);
                return;
            }
            expression(s->right, assigned);
            uint64_t bit = field < 64 ? (1ull << field) : 0;
            if ((assigned & bit) != 0 || in_loop) {
                checker.fail_n(s, "lucb.check.init",
                               "field `" + string(s->left->text) + "` may be assigned twice in `init`");
            }
            assigned |= bit;
            return;
        }
        case NodeKind::If: {
            expression(s->left, assigned);
            uint64_t then_set = assigned;
            statement(s->body, then_set, in_loop);
            uint64_t else_set = assigned;
            statement(s->right, else_set, in_loop);
            bool then_leaves = checker.always_returns(s->body);
            bool else_leaves = s->right != nullptr && checker.always_returns(s->right);
            if (then_leaves && !else_leaves) {
                assigned = else_set;
            } else if (else_leaves && !then_leaves) {
                assigned = then_set;
            } else if (!then_leaves) {
                assigned = then_set & else_set;
            }
            return;
        }
        case NodeKind::Match: {
            expression(s->left, assigned);
            uint64_t joined = every;
            bool any = false;
            for (Node* arm = s->body; arm != nullptr; arm = arm->next) {
                expression(arm->type, assigned);
                uint64_t arm_set = assigned;
                statement(arm->body, arm_set, in_loop);
                if (!checker.always_returns(arm->body)) {
                    joined &= arm_set;
                    any = true;
                }
            }
            assigned = any ? joined : assigned;
            return;
        }
        case NodeKind::While:
        case NodeKind::For:
        case NodeKind::With: {
            expression(s->left, assigned);
            expression(s->right, assigned);
            uint64_t body_set = assigned;
            statement(s->body, body_set, true);
            return;
        }
        case NodeKind::Let:
        case NodeKind::Var:
        case NodeKind::Return:
        case NodeKind::Recover:
        case NodeKind::ExprStmt:
        case NodeKind::Free:
            expression(s->left, assigned);
            expression(s->right, assigned);
            return;
        case NodeKind::Defer:
        case NodeKind::Errdefer:
            // runs at the end, when every field is assigned or the init has failed
            return;
        default:
            return;
        }
    }
};

auto Checker::check_init(Node* fn, Node* owner) -> void {
    if (owner->kind != NodeKind::Struct) {
        return;
    }
    if (!type_eq(fn->ty, t_unit())) {
        fail_n(fn, "lucb.check.init", "`init` returns `unit` or `!`");
    }
    InitWalk walk{*this, owner, all_fields(owner)};
    uint64_t assigned = 0;
    walk.statement(fn->body, assigned, false);
    if (assigned != walk.every && !always_returns(fn->body)) {
        int i = 0;
        for (Node* m = owner->body; m != nullptr; m = m->next) {
            if (m->kind != NodeKind::Field) {
                continue;
            }
            if (i < 64 && (assigned & (1ull << i)) == 0) {
                fail_n(fn, "lucb.check.init", "field `" + string(m->text) + "` is not assigned by `init`");
            }
            i += 1;
        }
    }
}

} // namespace lucb
