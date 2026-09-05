#include "parse/parser_impl.h"

namespace lucb {

auto Parser::parse_expression() -> Node* {
        nest++;
        if (nest > k_max_nest) {
            fail("lucb.parse.limit", "expression nests too deeply");
            nest--;
            return make(NodeKind::Name, cur().span);
        }
        Node* n = parse_else_expr();
        if (eat(TokenKind::KwCatch)) {
            Node* c = make(NodeKind::Catch, n->span);
            c->left = n;
            if (at(TokenKind::Name)) {
                c->text = take().text;
            }
            expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
            c->body = parse_suite();
            n = c;
        }
        nest--;
        return n;
    }

auto Parser::finish_expression(Node* unary) -> Node* {
        Node* n = parse_binary_rest(unary, 1);
        n = parse_conditional_rest(n);
        n = parse_else_rest(n);
        if (eat(TokenKind::KwCatch)) {
            Node* c = make(NodeKind::Catch, n->span);
            c->left = n;
            if (at(TokenKind::Name)) {
                c->text = take().text;
            }
            expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
            c->body = parse_suite();
            return c;
        }
        return n;
    }

auto Parser::parse_else_expr() -> Node* {
        Node* n = parse_conditional();
        return parse_else_rest(n);
    }

auto Parser::parse_else_rest(Node* n) -> Node* {
        if (!at(TokenKind::KwElse)) {
            return n;
        }
        // `else` after a conditional's `if` is consumed there.
        take();
        Node* e = make(NodeKind::Else, n->span);
        e->left = n;
        e->right = parse_else_expr();
        return e;
    }

auto Parser::parse_conditional() -> Node* {
        Node* n = parse_binary(1);
        return parse_conditional_rest(n);
    }

auto Parser::parse_conditional_rest(Node* n) -> Node* {
        if (!eat(TokenKind::KwIf)) {
            return n;
        }
        Node* c = make(NodeKind::Conditional, n->span);
        c->left = n;
        c->type = parse_binary(1);
        expect(TokenKind::KwElse, "lucb.parse.expect", "expected `else`");
        c->right = parse_conditional();
        return c;
    }

auto Parser::parse_binary(int min_prec) -> Node* {
        Node* left = parse_unary();
        return parse_binary_rest(left, min_prec);
    }

auto Parser::parse_binary_rest(Node* left, int min_prec) -> Node* {
        bool used_cmp = false;
        bool used_range = false;
        while (true) {
            TokenKind op = cur().kind;
            int prec = prec_of(op);
            if (prec < min_prec || prec == 0) {
                break;
            }
            if (is_compare(op) && used_cmp) {
                fail("lucb.parse.chain", "comparison operators cannot be chained");
            }
            if (is_compare(op) && left != nullptr && left->kind == NodeKind::Unary &&
                left->op == TokenKind::KwNot) {
                fail("lucb.parse.precedence",
                     "ambiguous `not` before comparison; write `not (a == b)`");
            }
            if (is_range_op(op) && used_range) {
                fail("lucb.parse.chain", "range operators cannot be chained");
            }
            if (is_compare(op)) {
                used_cmp = true;
            }
            if (is_range_op(op)) {
                used_range = true;
            }
            take();
            Node* right = parse_binary(prec + 1);
            Node* b = make(NodeKind::Binary, left->span);
            b->op = op;
            b->left = left;
            b->right = right;
            left = b;
        }
        return left;
    }

auto Parser::parse_unary() -> Node* {
        Token start = cur();
        if (at(TokenKind::KwTry) || at(TokenKind::KwNot) || at(TokenKind::Plus) ||
            at(TokenKind::Minus) || at(TokenKind::MinusPercent) || at(TokenKind::Tilde) ||
            at(TokenKind::Star) || at(TokenKind::Amp)) {
            Token op = take();
            Node* n = make(NodeKind::Unary, start.span);
            n->op = op.kind;
            n->left = parse_unary();
            n->span = span_from(start);
            return n;
        }
        if (is_scalar_cast_ahead()) {
            take(); // (
            Node* n = make(NodeKind::Cast, start.span);
            n->type = parse_type();
            expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
            n->left = parse_unary();
            n->span = span_from(start);
            return n;
        }
        return parse_postfix();
    }

auto Parser::parse_postfix() -> Node* {
        Token start = cur();
        Node* value = parse_primary();
        while (true) {
            if (eat(TokenKind::Dot)) {
                Node* m = make(NodeKind::Member, start.span);
                m->left = value;
                if (!at(TokenKind::Name) && !at(TokenKind::KwNone) &&
                    !at(TokenKind::KwSpawn)) {
                    fail("lucb.parse.expect", "expected a member name");
                } else {
                    m->text = take().text;
                }
                m->span = span_from(start);
                value = m;
            } else if (at(TokenKind::LParen)) {
                Node* c = make(NodeKind::Call, start.span);
                c->left = value;
                c->body = parse_arg_list();
                c->span = span_from(start);
                value = c;
            } else if (at(TokenKind::LBracket) && is_generic_call_ahead()) {
                Node* c = make(NodeKind::Call, start.span);
                c->left = value;
                c->type = parse_type_args();
                c->body = parse_arg_list();
                c->span = span_from(start);
                value = c;
            } else if (at(TokenKind::LBracket) && peek(1).kind == TokenKind::RBracket &&
                       peek(2).kind == TokenKind::LParen) {
                Node* s = make(NodeKind::SpanMake, start.span);
                s->type = expr_as_type(value);
                take();
                take();
                s->body = parse_arg_list();
                s->span = span_from(start);
                value = s;
            } else if (eat(TokenKind::LBracket)) {
                value = parse_subscript(value, start);
            } else {
                break;
            }
        }
        return value;
    }

auto Parser::expr_as_type(Node* value) -> Node* {
        Node* t = make(NodeKind::Type, value->span);
        if (value->kind == NodeKind::Name) {
            t->text = value->text;
            return t;
        }
        if (value->kind == NodeKind::Member && value->left != nullptr &&
            value->left->kind == NodeKind::Name) {
            t->text = value->text;
            t->left = value->left;
            return t;
        }
        fail("lucb.parse.expect", "a span constructor names its element type");
        return t;
    }

auto Parser::parse_subscript(Node* value, Token start) -> Node* {
        if (eat(TokenKind::DotDotLt) || at(TokenKind::DotDot)) {
            // shouldn't happen, `[..<end]` starts with ..<
        }
        if (at(TokenKind::DotDotLt)) {
            take();
            Node* s = make(NodeKind::Slice, start.span);
            s->left = value;
            s->right = parse_expression();
            expect(TokenKind::RBracket, "lucb.parse.expect", "expected `]`");
            s->span = span_from(start);
            return s;
        }
        Node* first = parse_expression();
        if (first != nullptr && first->kind == NodeKind::Binary &&
            (first->op == TokenKind::DotDotLt || first->op == TokenKind::DotDot)) {
            Node* s = make(NodeKind::Slice, start.span);
            s->left = value;
            s->body = first->left;
            s->right = first->right;
            expect(TokenKind::RBracket, "lucb.parse.expect", "expected `]`");
            s->span = span_from(start);
            return s;
        }
        if (eat(TokenKind::DotDotLt)) {
            Node* s = make(NodeKind::Slice, start.span);
            s->left = value;
            s->body = first;
            s->right = parse_expression();
            expect(TokenKind::RBracket, "lucb.parse.expect", "expected `]`");
            s->span = span_from(start);
            return s;
        }
        if (eat(TokenKind::DotDot)) {
            Node* s = make(NodeKind::Slice, start.span);
            s->left = value;
            s->body = first;
            expect(TokenKind::RBracket, "lucb.parse.expect", "expected `]`");
            s->span = span_from(start);
            return s;
        }
        Node* idx = make(NodeKind::Index, start.span);
        idx->left = value;
        idx->body = first;
        expect(TokenKind::RBracket, "lucb.parse.expect", "expected `]`");
        idx->span = span_from(start);
        return idx;
    }

auto Parser::parse_arg_list() -> Node* {
        expect(TokenKind::LParen, "lucb.parse.expect", "expected `(`");
        Node* list = nullptr;
        if (!at(TokenKind::RParen)) {
            while (true) {
                Node* a = make(NodeKind::Param, cur().span);
                if (at(TokenKind::Name) && peek(1).kind == TokenKind::Eq) {
                    a->text = take().text;
                    take();
                }
                a->left = parse_expression();
                append_node(&list, a);
                if (!eat(TokenKind::Comma)) {
                    break;
                }
                if (at(TokenKind::RParen)) {
                    break;
                }
            }
        }
        expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
        return list;
    }

auto Parser::parse_type_args() -> Node* {
        expect(TokenKind::LBracket, "lucb.parse.expect", "expected `[`");
        Node* list = nullptr;
        if (!at(TokenKind::RBracket)) {
            while (true) {
                append_node(&list, parse_type());
                if (!eat(TokenKind::Comma)) {
                    break;
                }
                if (at(TokenKind::RBracket)) {
                    break;
                }
            }
        }
        expect(TokenKind::RBracket, "lucb.parse.expect", "expected `]`");
        return list;
    }

auto Parser::parse_primary() -> Node* {
        Token start = cur();
        if (at(TokenKind::FormatStart)) {
            return parse_formatted();
        }
        if (is_literal_kind(cur().kind)) {
            return parse_literal();
        }
        if (at(TokenKind::Name) || at(TokenKind::Underscore)) {
            return make_tok(NodeKind::Name, take());
        }
        if (eat(TokenKind::KwSelf)) {
            Node* n = make(NodeKind::Self, start.span);
            n->text = "self";
            return n;
        }
        if (eat(TokenKind::Dot)) {
            Node* n = make(NodeKind::CaseValue, start.span);
            if (at(TokenKind::Name) || at(TokenKind::KwNone)) {
                n->text = take().text;
            } else {
                fail("lucb.parse.expect", "expected a case name");
            }
            if (at(TokenKind::LParen)) {
                n->body = parse_arg_list();
            }
            n->span = span_from(start);
            return n;
        }
        if (at(TokenKind::LParen)) {
            if (is_lambda_ahead()) {
                return parse_lambda();
            }
            return parse_group_or_tuple();
        }
        if (at(TokenKind::LBracket)) {
            return parse_array_lit();
        }
        if (at(TokenKind::KwMatch)) {
            return parse_match(true);
        }
        if (at(TokenKind::KwNew)) {
            return parse_new_or_alloc(false);
        }
        if (at(TokenKind::KwAlloc)) {
            return parse_new_or_alloc(true);
        }
        fail("lucb.parse.expect", "expected an expression");
        return make(NodeKind::Name, start.span);
    }

auto Parser::parse_literal() -> Node* {
        Token t = take();
        Node* n = make(NodeKind::Literal, t.span);
        n->text = t.text;
        n->op = t.kind;
        return n;
    }

auto Parser::parse_formatted() -> Node* {
        Token start = take();
        Node* n = make(NodeKind::Formatted, start.span);
        Node* parts = nullptr;
        while (!at(TokenKind::FormatEnd) && !at(TokenKind::EndOfFile)) {
            if (at(TokenKind::FormatText)) {
                Node* p = make_tok(NodeKind::FormatText, take());
                append_node(&parts, p);
            } else if (eat(TokenKind::LBrace)) {
                Node* p = make(NodeKind::FormatField, peek(-1).span);
                p->left = parse_expression();
                expect(TokenKind::RBrace, "lucb.parse.expect", "expected `}`");
                append_node(&parts, p);
            } else {
                fail("lucb.parse.expect", "expected formatted string text or a field");
                break;
            }
        }
        expect(TokenKind::FormatEnd, "lucb.parse.expect", "expected the end of a formatted string");
        n->body = parts;
        n->span = span_from(start);
        return n;
    }

auto Parser::parse_lambda() -> Node* {
        Token start = cur();
        Node* n = make(NodeKind::Lambda, start.span);
        expect(TokenKind::LParen, "lucb.parse.expect", "expected `(`");
        Node* list = nullptr;
        if (!at(TokenKind::RParen)) {
            while (true) {
                Node* p = make(NodeKind::Param, cur().span);
                if (!at(TokenKind::Name)) {
                    fail("lucb.parse.expect", "expected a parameter name");
                    break;
                }
                p->text = take().text;
                if (eat(TokenKind::Colon)) {
                    p->type = parse_type();
                }
                append_node(&list, p);
                if (!eat(TokenKind::Comma)) {
                    break;
                }
                if (at(TokenKind::RParen)) {
                    break;
                }
            }
        }
        expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
        n->right = list;
        expect(TokenKind::FatArrow, "lucb.parse.expect", "expected `=>`");
        n->body = parse_expression();
        n->span = span_from(start);
        return n;
    }

auto Parser::parse_group_or_tuple() -> Node* {
        Token start = take();
        if (eat(TokenKind::RParen)) {
            return make(NodeKind::Unit, span_from(start));
        }
        Node* first = parse_expression();
        if (!eat(TokenKind::Comma)) {
            expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
            Node* g = make(NodeKind::Group, span_from(start));
            g->left = first;
            return g;
        }
        Node* t = make(NodeKind::Tuple, start.span);
        append_node(&t->body, first);
        if (at(TokenKind::RParen)) {
            fail("lucb.parse.expect", "a tuple requires at least two elements");
        }
        while (true) {
            append_node(&t->body, parse_expression());
            if (!eat(TokenKind::Comma)) {
                break;
            }
            if (at(TokenKind::RParen)) {
                break;
            }
        }
        expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
        t->span = span_from(start);
        return t;
    }

auto Parser::parse_array_lit() -> Node* {
        Token start = take();
        Node* n = make(NodeKind::ArrayLit, start.span);
        if (!at(TokenKind::RBracket)) {
            while (true) {
                append_node(&n->body, parse_expression());
                if (!eat(TokenKind::Comma)) {
                    break;
                }
                if (at(TokenKind::RBracket)) {
                    break;
                }
            }
        }
        expect(TokenKind::RBracket, "lucb.parse.expect", "expected `]`");
        n->span = span_from(start);
        return n;
    }

auto Parser::parse_new_or_alloc(bool is_alloc) -> Node* {
        Token start = take();
        Node* n = make(is_alloc ? NodeKind::Alloc : NodeKind::New, start.span);
        if (is_alloc && at(TokenKind::LParen)) {
            n->body = parse_arg_list();
        } else {
            n->type = parse_type();
            if (at(TokenKind::LParen)) {
                n->body = parse_arg_list();
            } else if (eat(TokenKind::Dot)) {
                Node* cse = make(NodeKind::CaseValue, cur().span);
                if (at(TokenKind::Name)) {
                    cse->text = take().text;
                }
                if (at(TokenKind::LParen)) {
                    cse->body = parse_arg_list();
                }
                n->body = cse;
            }
        }
        if (eat(TokenKind::KwIn)) {
            n->right = parse_expression();
        }
        n->span = span_from(start);
        return n;
    }

auto Parser::parse_type() -> Node* {
        Token start = cur();
        uint32_t flags = 0;
        if (eat(TokenKind::At)) {
            flags |= FlagAtomic;
        }
        while (at(TokenKind::KwConst) || at(TokenKind::KwVolatile)) {
            if (eat(TokenKind::KwConst)) {
                flags |= FlagConst;
            } else {
                eat(TokenKind::KwVolatile);
                flags |= FlagVolatile;
            }
        }
        Node* value = parse_primary_type();
        while (at(TokenKind::Star) || at(TokenKind::StarQuestion) || is_array_suffix_ahead()) {
            if (is_array_suffix_ahead()) {
                take(); // [
                Node* t = make(NodeKind::Type, start.span);
                t->left = value;
                if (eat(TokenKind::RBracket)) {
                    t->flags = FlagSpan | (flags & FlagConst);
                } else {
                    t->flags = FlagArray;
                    t->right = parse_expression();
                    expect(TokenKind::RBracket, "lucb.parse.expect", "expected `]`");
                }
                value = t;
                flags &= ~(FlagConst | FlagVolatile);
                continue;
            }
            bool nullable = at(TokenKind::StarQuestion);
            take();
            Node* t = make(NodeKind::Type, start.span);
            t->flags = FlagStar | (flags & (FlagConst | FlagVolatile));
            t->left = value;
            value = t;
            flags &= ~(FlagConst | FlagVolatile);
            if (nullable) {
                Node* opt = make(NodeKind::Type, start.span);
                opt->flags = FlagOptional;
                opt->left = value;
                value = opt;
            }
        }
        if (flags & (FlagConst | FlagVolatile)) {
            fail("lucb.parse.type", "`const` and `volatile` qualify a pointee: write `const T*`");
        }
        if (eat(TokenKind::Question)) {
            Node* t = make(NodeKind::Type, start.span);
            t->flags = FlagOptional;
            t->left = value;
            value = t;
        }
        if (eat(TokenKind::Bang)) {
            Node* t = make(NodeKind::Type, start.span);
            t->flags = FlagFallible;
            t->left = value;
            value = t;
        }
        if (flags & FlagAtomic) {
            Node* t = make(NodeKind::Type, start.span);
            t->flags = FlagAtomic;
            t->left = value;
            value = t;
        }
        value->span = span_from(start);
        return value;
    }

auto Parser::parse_primary_type() -> Node* {
        Token start = cur();
        if (eat(TokenKind::KwFunc)) {
            Node* t = make(NodeKind::Type, start.span);
            t->flags = FlagFuncType;
            expect(TokenKind::LParen, "lucb.parse.expect", "expected `(`");
            Node* params = nullptr;
            if (!at(TokenKind::RParen)) {
                while (true) {
                    append_node(&params, parse_type());
                    if (!eat(TokenKind::Comma)) {
                        break;
                    }
                    if (at(TokenKind::RParen)) {
                        break;
                    }
                }
            }
            expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
            expect(TokenKind::Arrow, "lucb.parse.expect", "expected `->`");
            t->body = params;
            t->left = parse_type();
            t->span = span_from(start);
            return t;
        }
        if (at_name("void")) {
            Token t = take();
            Node* n = make(NodeKind::Type, t.span);
            n->flags = FlagVoid;
            n->text = "void";
            return n;
        }
        if (eat(TokenKind::LParen)) {
            Node* first = parse_type();
            if (!eat(TokenKind::Comma)) {
                expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
                Node* wrap = make(NodeKind::Type, start.span);
                wrap->left = first;
                wrap->span = span_from(start);
                return wrap;
            }
            Node* t = make(NodeKind::Type, start.span);
            t->flags = FlagTupleType;
            append_node(&t->body, first);
            while (true) {
                append_node(&t->body, parse_type());
                if (!eat(TokenKind::Comma)) {
                    break;
                }
                if (at(TokenKind::RParen)) {
                    break;
                }
            }
            expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
            t->span = span_from(start);
            return t;
        }
        if (!at(TokenKind::Name)) {
            fail("lucb.parse.expect", "expected a type");
            return make(NodeKind::Type, start.span);
        }
        Token name = take();
        size_t path_start = name.span.start;
        size_t path_end = name.span.end;
        while (eat(TokenKind::Dot)) {
            if (!at(TokenKind::Name)) {
                fail("lucb.parse.expect", "expected a name after `.`");
                break;
            }
            path_end = take().span.end;
        }
        Node* t = make(NodeKind::Type, start.span);
        t->text = source->bytes().substr(path_start, path_end - path_start);
        if (at(TokenKind::LBracket) && !is_array_suffix_ahead()) {
            t->body = parse_type_args();
        }
        t->span = span_from(start);
        return t;
    }

} // namespace lucb
