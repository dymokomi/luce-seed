#include "interp/interp_impl.h"

#include "support/literal.h"

namespace lucb {

auto Interp::exec(Node* n) -> void {
        if (n == nullptr || trapped || returning) {
            return;
        }
        switch (n->kind) {
        case NodeKind::Block:
            defers.emplace_back();
            for (Node* s = n->body; s != nullptr; s = s->next) {
                exec(s);
                if (trapped || returning || breaking || continuing) {
                    break;
                }
            }
            if (!defers.empty()) {
                vector<Deferred> d = defers.back();
                defers.pop_back();
                if (!trapped) {
                    bool failing = returning && ret.failed;
                    for (int i = static_cast<int>(d.size()) - 1; i >= 0; i--) {
                        if (d[static_cast<size_t>(i)].err_only && !failing) {
                            continue;
                        }
                        Node* dn = d[static_cast<size_t>(i)].n;
                        Value dv = eval(dn->left);
                        if (dv.failed && dn->body != nullptr) {
                            run_catch_handler(dn, dv);
                        }
                    }
                }
            }
            break;
        case NodeKind::Let:
        case NodeKind::Var: {
            Slot s;
            s.name = n->text;
            if (n->left != nullptr) {
                if (n->ty != nullptr && n->ty->kind == TypeKind::Span &&
                    n->left->kind == NodeKind::Name) {
                    Value* src = lvalue(n->left);
                    s.value.kind = TypeKind::Span;
                    s.value.type = n->ty;
                    if (src != nullptr) {
                        s.value.ptr = src->ptr != nullptr ? src->ptr : src->fields.data();
                        s.value.length = src->length != 0 ? src->length : src->fields.size();
                        s.value.str = src->str;
                    }
                } else {
                    s.value = eval(n->left);
                    if (n->ty != nullptr && n->ty->kind == TypeKind::Span &&
                        s.value.kind == TypeKind::Array) {
                        s.value.ptr = s.value.ptr;
                        s.value.kind = TypeKind::Span;
                        s.value.type = n->ty;
                    }
                    if (n->ty != nullptr) {
                        s.value.type = n->ty;
                        s.value.kind = n->ty->kind;
                    }
                }
            } else {
                s.value = zero_of(n->ty);
            }
            if (!frames.empty()) {
                frames.back().slots.push_back(s);
            }
            break;
        }
        case NodeKind::Assign: {
            Value* dst = lvalue(n->left);
            Value src = eval(n->right);
            if (dst == nullptr || trapped) {
                return;
            }
            if (n->op == TokenKind::Eq) {
                Type* dt = n->left != nullptr ? n->left->ty : dst->type;
                src.kind = dst->kind;
                src.type = dt != nullptr ? dt : dst->type;
                if (dt != nullptr) {
                    src.kind = dt->kind;
                }
                *dst = src;
                break;
            }
            TokenKind op = TokenKind::Plus;
            if (n->op == TokenKind::PlusEq) {
                op = TokenKind::Plus;
            } else if (n->op == TokenKind::MinusEq) {
                op = TokenKind::Minus;
            } else if (n->op == TokenKind::StarEq) {
                op = TokenKind::Star;
            } else if (n->op == TokenKind::SlashSlashEq) {
                op = TokenKind::SlashSlash;
            } else if (n->op == TokenKind::PercentEq) {
                op = TokenKind::Percent;
            } else if (n->op == TokenKind::PlusPercentEq) {
                op = TokenKind::PlusPercent;
            } else if (n->op == TokenKind::MinusPercentEq) {
                op = TokenKind::MinusPercent;
            } else if (n->op == TokenKind::StarPercentEq) {
                op = TokenKind::StarPercent;
            } else if (n->op == TokenKind::PlusPipeEq) {
                op = TokenKind::PlusPipe;
            } else if (n->op == TokenKind::MinusPipeEq) {
                op = TokenKind::MinusPipe;
            } else if (n->op == TokenKind::StarPipeEq) {
                op = TokenKind::StarPipe;
            } else if (n->op == TokenKind::AmpEq) {
                op = TokenKind::Amp;
            } else if (n->op == TokenKind::PipeEq) {
                op = TokenKind::Pipe;
            } else if (n->op == TokenKind::CaretEq) {
                op = TokenKind::Caret;
            } else if (n->op == TokenKind::LtLtEq) {
                op = TokenKind::LtLt;
            } else if (n->op == TokenKind::GtGtEq) {
                op = TokenKind::GtGt;
            } else {
                fail("unsupported assignment");
                return;
            }
            Value r = arith(n->left->ty, *dst, src, op);
            if (!trapped) {
                *dst = r;
            }
            break;
        }
        case NodeKind::If: {
            if (n->flags & FlagIfLet) {
                Node* let = n->left;
                Value v = eval(let != nullptr ? let->left : nullptr);
                if (trapped) {
                    return;
                }
                bool some = v.kind == TypeKind::Optional ? v.present : v.ptr != nullptr;
                if (some) {
                    Slot s;
                    s.name = let != nullptr ? let->text : string_view{};
                    s.value = v;
                    if (v.kind == TypeKind::Optional && v.type != nullptr && v.type->elem != nullptr) {
                        s.value.kind = v.type->elem->kind;
                        s.value.type = v.type->elem;
                    }
                    frames.back().slots.push_back(s);
                    exec(n->body);
                    frames.back().slots.pop_back();
                } else {
                    exec(n->right);
                }
            } else {
                Value c = eval(n->left);
                if (trapped) {
                    return;
                }
                if (c.b) {
                    exec(n->body);
                } else {
                    exec(n->right);
                }
            }
            break;
        }
        case NodeKind::While:
            while (!trapped && !returning && !breaking) {
                continuing = false;
                if (n->flags & FlagIfLet) {
                    Node* let = n->left;
                    Value v = eval(let != nullptr ? let->left : nullptr);
                    if (trapped) {
                        return;
                    }
                    bool some = v.kind == TypeKind::Optional ? v.present : v.ptr != nullptr;
                    if (!some) {
                        break;
                    }
                    Slot s;
                    s.name = let != nullptr ? let->text : string_view{};
                    s.value = v;
                    if (v.kind == TypeKind::Optional && v.type != nullptr && v.type->elem != nullptr) {
                        s.value.kind = v.type->elem->kind;
                        s.value.type = v.type->elem;
                    }
                    frames.back().slots.push_back(s);
                    exec(n->body);
                    frames.back().slots.pop_back();
                } else {
                    Value c = eval(n->left);
                    if (trapped || !c.b) {
                        break;
                    }
                    exec(n->body);
                }
                if (breaking) {
                    if (jump_label.empty() || jump_label == n->text) {
                        breaking = false;
                    }
                    break;
                }
                if (continuing) {
                    if (jump_label.empty() || jump_label == n->text) {
                        continuing = false;
                        continue;
                    }
                    break;
                }
            }
            break;
        case NodeKind::Return:
            ret = n->left != nullptr ? eval(n->left) : v_unit();
            returning = true;
            break;
        case NodeKind::Break:
            breaking = true;
            jump_label = n->text;
            break;
        case NodeKind::Continue:
            continuing = true;
            jump_label = n->text;
            break;
        case NodeKind::Defer:
        case NodeKind::Errdefer: {
            Deferred d;
            d.n = n;
            d.err_only = n->kind == NodeKind::Errdefer;
            if (!defers.empty()) {
                defers.back().push_back(d);
            } else {
                eval(n->left);
            }
            break;
        }
        case NodeKind::Recover:
            recover_val = n->left != nullptr ? eval(n->left) : v_unit();
            recovered = true;
            returning = true;
            break;
        case NodeKind::Match:
            eval_match(n);
            break;
        case NodeKind::For: {
            if (n->right != nullptr && n->right->kind == NodeKind::Binary &&
                (n->right->op == TokenKind::DotDotLt || n->right->op == TokenKind::DotDotEq)) {
                Value a = eval(n->right->left);
                Value b = eval(n->right->right);
                if (trapped) {
                    return;
                }
                int64_t start = as_s(a, a.type);
                int64_t end = as_s(b, b.type);
                bool closed = n->right->op == TokenKind::DotDotEq;
                for (int64_t i = start; !trapped && !returning && !breaking &&
                                        (closed ? i <= end : i < end);
                     i++) {
                    continuing = false;
                    Slot s;
                    s.name = n->text;
                    s.value = v_int(n->ty, static_cast<uint64_t>(i));
                    frames.back().slots.push_back(s);
                    exec(n->body);
                    frames.back().slots.pop_back();
                    if (breaking) {
                        if (jump_label.empty() || jump_label == n->text) {
                            breaking = false;
                        }
                        break;
                    }
                    if (continuing) {
                        if (jump_label.empty() || jump_label == n->text) {
                            continuing = false;
                            continue;
                        }
                        break;
                    }
                }
                break;
            }
            Value it = eval(n->right);
            if (trapped) {
                return;
            }
            size_t len = it.length != 0 ? it.length : it.fields.size();
            if (it.kind == TypeKind::Array && it.type != nullptr) {
                len = static_cast<size_t>(it.type->length);
            }
            if (it.kind == TypeKind::Str) {
                string d = decode_string(it.str);
                len = d.size();
                for (size_t i = 0; i < len && !trapped && !returning; i++) {
                    Slot s;
                    s.name = n->text;
                    s.value = v_int(n->ty, static_cast<unsigned char>(d[static_cast<size_t>(i)]));
                    if (n->ty != nullptr && n->ty->kind == TypeKind::Char) {
                        s.value.kind = TypeKind::Char;
                        s.value.u = static_cast<unsigned char>(d[i]);
                    }
                    frames.back().slots.push_back(s);
                    exec(n->body);
                    frames.back().slots.pop_back();
                }
                break;
            }
            for (size_t i = 0; i < len && !trapped && !returning; i++) {
                Slot s;
                s.name = n->text;
                Value* elems = it.ptr;
                Value elem;
                if (elems != nullptr) {
                    elem = elems[i];
                } else if (i < it.fields.size()) {
                    elem = it.fields[i];
                }
                if (n->flags & FlagByPtr) {
                    Value p;
                    p.kind = TypeKind::Pointer;
                    p.type = n->ty;
                    if (elems != nullptr) {
                        p.ptr = elems + static_cast<ptrdiff_t>(i);
                    } else if (i < it.fields.size()) {
                        p.ptr = &it.fields[i];
                    }
                    s.value = p;
                } else {
                    s.value = elem;
                }
                frames.back().slots.push_back(s);
                exec(n->body);
                frames.back().slots.pop_back();
            }
            break;
        }
        case NodeKind::ExprStmt:
            eval(n->left);
            break;
        case NodeKind::Free:
            eval(n->left);
            if (n->right != nullptr) {
                as_alloc(n->right);
            }
            break;
        case NodeKind::With: {
            Value saved = current_alloc;
            current_alloc = as_alloc(n->left);
            exec(n->body);
            current_alloc = saved;
            break;
        }
        default:
            fail("unsupported statement at runtime");
            break;
        }
    }

} // namespace lucb
