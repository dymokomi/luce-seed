//==============================================================================================
//
//   parse/stmt - Statements
//
//   DESCRIPTION:
//       Bindings, assignment, `if`/`while`/`for`/`match`, labels, `defer`/`errdefer`,
//       `return`, `free`, `with`, `asm` suites, and the suite layout rules (base.md §8, §21).
//
//==============================================================================================

#include "parse/parser_impl.h"

namespace lucb {

auto Parser::parse_suite() -> Node* {
    Token start = cur();
    if (eat(TokenKind::Newline)) {
        expect(TokenKind::Indent, "lucb.parse.expect", "expected an indented block");
        Node* block = make(NodeKind::Block, start.span);
        while (!at(TokenKind::Dedent) && !at(TokenKind::EndOfFile)) {
            skip_docs();
            if (at(TokenKind::Dedent) || at(TokenKind::EndOfFile)) {
                break;
            }
            int here = pos;
            Node* s = parse_statement();
            if (s != nullptr) {
                append_node(&block->body, s);
            }
            if (pos == here) {
                take();
            }
        }
        expect(TokenKind::Dedent, "lucb.parse.expect", "expected a dedent");
        block->span = span_from(start);
        return block;
    }
    if (at(TokenKind::KwIf) || at(TokenKind::KwWhile) || at(TokenKind::KwFor) ||
        at(TokenKind::KwMatch) || at(TokenKind::KwWith) || at(TokenKind::KwAsm)) {
        fail("lucb.parse.suite", "a same-line suite cannot contain a compound statement");
        Node* block = make(NodeKind::Block, start.span);
        append_node(&block->body, parse_statement());
        return block;
    }
    Node* block = make(NodeKind::Block, start.span);
    Node* s = parse_simple_stmt();
    append_node(&block->body, s);
    return block;
}

auto Parser::parse_statement() -> Node* {
    if (at(TokenKind::KwIf)) {
        return parse_if();
    }
    if (at(TokenKind::KwWhile)) {
        return parse_while();
    }
    if (at(TokenKind::KwFor)) {
        return parse_for();
    }
    if (at(TokenKind::KwMatch)) {
        return parse_match(false);
    }
    if (at(TokenKind::KwWith)) {
        return parse_with();
    }
    if (at(TokenKind::KwAsm)) {
        return parse_asm();
    }
    if (at(TokenKind::Name) && peek(1).kind == TokenKind::Colon &&
        (peek(2).kind == TokenKind::KwWhile || peek(2).kind == TokenKind::KwFor)) {
        Token label = take();
        take(); // colon
        Node* loop = nullptr;
        if (at(TokenKind::KwWhile)) {
            loop = parse_while();
        } else {
            loop = parse_for();
        }
        if (loop != nullptr) {
            loop->label = label.text;
        }
        return loop;
    }
    return parse_simple_stmt();
}

auto Parser::parse_free() -> Node* {
    Token start = take();
    Node* n = make(NodeKind::Free, start.span);
    expect(TokenKind::LParen, "lucb.parse.expect", "expected `(`");
    n->left = parse_expression();
    expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
    if (eat(TokenKind::KwIn)) {
        n->right = parse_expression();
    }
    n->span = span_from(start);
    return n;
}

auto Parser::parse_simple_stmt() -> Node* {
    Token start = cur();
    if (at(TokenKind::KwLet) || at(TokenKind::KwVar)) {
        return parse_binding();
    }
    if (at(TokenKind::KwReturn)) {
        take();
        Node* n = make(NodeKind::Return, start.span);
        if (!at(TokenKind::Newline) && !at(TokenKind::Dedent) && !at(TokenKind::EndOfFile)) {
            n->left = parse_expression();
        }
        expect_stmt_newline(n->left);
        n->span = span_from(start);
        return n;
    }
    if (at(TokenKind::KwBreak) || at(TokenKind::KwContinue)) {
        Token t = take();
        Node* n =
            make(t.kind == TokenKind::KwBreak ? NodeKind::Break : NodeKind::Continue, start.span);
        if (at(TokenKind::Name)) {
            n->text = take().text;
        }
        expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
        return n;
    }
    if (at(TokenKind::KwDefer) || at(TokenKind::KwErrdefer)) {
        Token t = take();
        Node* n =
            make(t.kind == TokenKind::KwDefer ? NodeKind::Defer : NodeKind::Errdefer, start.span);
        if (at(TokenKind::KwFree)) {
            n->left = parse_free();
        } else {
            n->left = parse_expression();
        }
        if (n->left != nullptr && n->left->kind == NodeKind::Catch) {
            // `defer call catch failure:` — the handler suite ended the line.
            n->span = span_from(start);
            return n;
        }
        if (eat(TokenKind::KwCatch)) {
            if (at(TokenKind::Name)) {
                n->text = take().text;
            }
            expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
            n->body = parse_suite();
            n->span = span_from(start);
            return n;
        }
        expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
        return n;
    }
    if (at(TokenKind::KwRecover)) {
        take();
        Node* n = make(NodeKind::Recover, start.span);
        n->left = parse_expression();
        expect_stmt_newline(n->left);
        n->span = span_from(start);
        return n;
    }
    if (at(TokenKind::KwFree)) {
        Node* n = parse_free();
        expect_stmt_newline(n != nullptr ? n->left : nullptr);
        return n;
    }
    if (at(TokenKind::KwGoto)) {
        fail("lucb.parse.reserved", "goto is reserved and not implemented");
        sync_line();
        return make(NodeKind::ExprStmt, start.span);
    }

    Node* left = parse_unary();
    if (left != nullptr && is_assign_op(cur().kind)) {
        Node* n = make(NodeKind::Assign, start.span);
        n->op = cur().kind;
        take();
        n->left = left;
        n->right = parse_expression();
        expect_stmt_newline(n->right);
        n->span = span_from(start);
        return n;
    }
    Node* expr = finish_expression(left);
    Node* n = make(NodeKind::ExprStmt, start.span);
    n->left = expr;
    expect_stmt_newline(expr);
    n->span = span_from(start);
    return n;
}

auto Parser::parse_binding() -> Node* {
    Token start = cur();
    bool is_let = at(TokenKind::KwLet);
    take();
    Node* n = make(is_let ? NodeKind::Let : NodeKind::Var, start.span);
    if (eat(TokenKind::LParen)) {
        // tuple binding: reuse body as name list
        Node* names = nullptr;
        do {
            if (!at(TokenKind::Name)) {
                fail("lucb.parse.expect", "expected a name");
                break;
            }
            append_node(&names, make_tok(NodeKind::Name, take()));
        } while (eat(TokenKind::Comma));
        expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
        n->body = names;
    } else if (at(TokenKind::Name)) {
        n->text = take().text;
    } else {
        fail("lucb.parse.expect", "expected a name");
    }
    if (eat(TokenKind::Colon)) {
        n->type = parse_type();
    }
    if (eat(TokenKind::Eq)) {
        if (!is_let && eat(TokenKind::DashDashDash)) {
            n->flags |= FlagUninit;
        } else {
            n->left = parse_expression();
        }
    } else if (is_let) {
        fail("lucb.parse.expect", "a let binding requires an initialiser");
    }
    expect_stmt_newline(n->left);
    n->span = span_from(start);
    return n;
}

auto Parser::parse_condition() -> Node* {
    if (eat(TokenKind::KwLet)) {
        Node* n = make(NodeKind::Let, peek(-1).span);
        n->flags = FlagIfLet;
        if (!at(TokenKind::Name)) {
            fail("lucb.parse.expect", "expected a name");
        } else {
            n->text = take().text;
        }
        expect(TokenKind::Eq, "lucb.parse.expect", "expected `=`");
        n->left = parse_expression();
        return n;
    }
    return parse_expression();
}

auto Parser::parse_if() -> Node* {
    Token start = take();
    Node* n = make(NodeKind::If, start.span);
    n->left = parse_condition();
    if (n->left != nullptr && n->left->kind == NodeKind::Let) {
        n->flags |= FlagIfLet;
    }
    expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
    n->body = parse_suite();
    Node* tail = n;
    while (at(TokenKind::KwElif)) {
        Token e = take();
        Node* elif = make(NodeKind::If, e.span);
        elif->left = parse_condition();
        if (elif->left != nullptr && elif->left->kind == NodeKind::Let) {
            elif->flags |= FlagIfLet; // `elif let x = ...:`
        }
        expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
        elif->body = parse_suite();
        tail->right = elif;
        tail = elif;
    }
    if (eat(TokenKind::KwElse)) {
        expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
        tail->right = parse_suite();
    }
    n->span = span_from(start);
    return n;
}

auto Parser::parse_while() -> Node* {
    Token start = take();
    Node* n = make(NodeKind::While, start.span);
    n->left = parse_condition();
    if (n->left != nullptr && n->left->kind == NodeKind::Let) {
        n->flags |= FlagIfLet;
    }
    expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
    n->body = parse_suite();
    n->span = span_from(start);
    return n;
}

auto Parser::parse_for() -> Node* {
    Token start = take();
    Node* n = make(NodeKind::For, start.span);
    if (eat(TokenKind::LParen)) {
        if (at(TokenKind::Name)) {
            n->text = take().text;
        }
        expect(TokenKind::Comma, "lucb.parse.expect", "expected `,`");
        if (at(TokenKind::Name)) {
            n->left = make_tok(NodeKind::Name, take());
        }
        expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
    } else {
        if (!at(TokenKind::Name) && !at(TokenKind::Underscore)) {
            fail("lucb.parse.expect", "expected a loop variable");
        } else {
            n->text = take().text;
        }
        if (eat(TokenKind::Colon)) {
            n->type = parse_type();
        }
    }
    expect(TokenKind::KwIn, "lucb.parse.expect", "expected `in`");
    if (eat(TokenKind::Amp)) {
        n->flags |= FlagByPtr;
    }
    n->right = parse_expression();
    expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
    n->body = parse_suite();
    n->span = span_from(start);
    return n;
}

auto Parser::parse_with() -> Node* {
    Token start = take();
    Node* n = make(NodeKind::With, start.span);
    n->left = parse_expression();
    expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
    n->body = parse_suite();
    n->span = span_from(start);
    return n;
}

auto Parser::parse_match(bool as_expr) -> Node* {
    Token start = take();
    Node* n = make(as_expr ? NodeKind::MatchExpr : NodeKind::Match, start.span);
    n->left = parse_expression();
    expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
    expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
    expect(TokenKind::Indent, "lucb.parse.expect", "expected an indented body");
    Node* arms = nullptr;
    while (!at(TokenKind::Dedent) && !at(TokenKind::EndOfFile)) {
        skip_docs();
        if (at(TokenKind::Dedent)) {
            break;
        }
        int here = pos;
        Node* arm = make(NodeKind::MatchArm, cur().span);
        Node* pats = nullptr;
        append_node(&pats, parse_pattern());
        while (eat(TokenKind::Comma)) {
            append_node(&pats, parse_pattern());
        }
        arm->left = pats;
        if (eat(TokenKind::KwIf)) {
            arm->type = parse_expression();
        }
        if (as_expr) {
            expect(TokenKind::FatArrow, "lucb.parse.expect", "expected `=>`");
            arm->body = parse_expression();
            expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
        } else {
            expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
            arm->body = parse_suite();
        }
        append_node(&arms, arm);
        if (pos == here) {
            take();
        }
    }
    expect(TokenKind::Dedent, "lucb.parse.expect", "expected a dedent");
    n->body = arms;
    n->span = span_from(start);
    return n;
}

auto Parser::parse_pattern() -> Node* {
    Token start = cur();
    Node* n = make(NodeKind::Pattern, start.span);
    if (eat(TokenKind::Underscore)) {
        n->text = "_";
        n->span = start.span;
        return n;
    }
    if (eat(TokenKind::Dot)) {
        if (at_ident() || at_name("some")) {
            n->text = take().text;
        } else {
            fail("lucb.parse.expect", "expected a case name");
        }
        if (eat(TokenKind::LParen)) {
            Node* binds = nullptr;
            if (!at(TokenKind::RParen)) {
                while (true) {
                    Node* b = make(NodeKind::Name, cur().span);
                    if (eat(TokenKind::Underscore)) {
                        b->text = "_";
                    } else if (at(TokenKind::Name)) {
                        b->text = take().text;
                    } else {
                        fail("lucb.parse.expect", "expected a payload name");
                        break;
                    }
                    append_node(&binds, b);
                    if (!eat(TokenKind::Comma)) {
                        break;
                    }
                }
            }
            expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
            n->body = binds;
        }
        n->span = span_from(start);
        return n;
    }
    bool neg = eat(TokenKind::Minus);
    if (is_literal_kind(cur().kind) || at(TokenKind::IntLit) || at(TokenKind::FloatLit) ||
        at(TokenKind::CharLit) || at(TokenKind::StringLit)) {
        n->left = parse_literal();
        if (neg) {
            Node* u = make(NodeKind::Unary, start.span);
            u->op = TokenKind::Minus;
            u->left = n->left;
            n->left = u;
        }
        if (at(TokenKind::DotDotLt) || at(TokenKind::DotDotEq)) {
            n->op = cur().kind;
            take();
            n->right = parse_literal();
        }
        n->span = span_from(start);
        return n;
    }
    if (at(TokenKind::Name)) {
        n->text = take().text;
        n->span = span_from(start);
        return n;
    }
    fail("lucb.parse.expect", "expected a pattern");
    return n;
}

} // namespace lucb
