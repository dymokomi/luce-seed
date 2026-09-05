#include "parse/parser.h"
#include "parse/parser_impl.h"

namespace lucb {

auto Parser::skip_docs() -> void {
        while (at(TokenKind::DocComment) || at(TokenKind::Newline)) {
            take();
        }
    }

auto Parser::find_match(int start, TokenKind open, TokenKind close) const -> int {
        int depth = 0;
        for (int i = start; i < n; i++) {
            if (tok[i].kind == open) {
                depth++;
            } else if (tok[i].kind == close) {
                depth--;
                if (depth == 0) {
                    return i;
                }
            }
        }
        return -1;
    }

auto Parser::is_lambda_ahead() const -> bool {
        if (!at(TokenKind::LParen)) {
            return false;
        }
        int close = find_match(pos, TokenKind::LParen, TokenKind::RParen);
        if (close < 0) {
            return false;
        }
        int after = close + 1;
        if (after >= n) {
            return false;
        }
        return tok[after].kind == TokenKind::FatArrow;
    }

auto Parser::is_array_suffix_ahead() const -> bool {
        if (!at(TokenKind::LBracket)) {
            return false;
        }
        if (peek(1).kind == TokenKind::RBracket) {
            return true;
        }
        return peek(1).kind == TokenKind::IntLit && peek(2).kind == TokenKind::RBracket;
    }

auto Parser::is_generic_call_ahead() const -> bool {
        if (!at(TokenKind::LBracket)) {
            return false;
        }
        int close = find_match(pos, TokenKind::LBracket, TokenKind::RBracket);
        if (close < 0 || close + 1 >= n || tok[close + 1].kind != TokenKind::LParen) {
            return false;
        }
        Token first = peek(1);
        if (first.kind == TokenKind::KwFunc || first.kind == TokenKind::LParen) {
            return true;
        }
        if (first.kind != TokenKind::Name) {
            return false;
        }
        if (is_core_type(first.text)) {
            return true;
        }
        if (!first.text.empty() && first.text[0] >= 'A' && first.text[0] <= 'Z') {
            return true;
        }
        return false;
    }

auto Parser::is_scalar_cast_ahead() const -> bool {
        if (!at(TokenKind::LParen)) {
            return false;
        }
        int i = pos + 1;
        while (peek_kind(i) == TokenKind::KwConst || peek_kind(i) == TokenKind::KwVolatile) {
            i++;
        }
        // `c.int`
        if (peek_kind(i) == TokenKind::Name && peek_text(i) == "c" &&
            peek_kind(i + 1) == TokenKind::Dot && peek_kind(i + 2) == TokenKind::Name &&
            peek_kind(i + 3) == TokenKind::RParen) {
            TokenKind after = peek_kind(i + 4);
            return expr_starts(after) || after == TokenKind::Minus || after == TokenKind::Star ||
                   after == TokenKind::Amp || after == TokenKind::LParen ||
                   after == TokenKind::Tilde || after == TokenKind::MinusPercent;
        }
        if (peek_kind(i) != TokenKind::Name) {
            return false;
        }
        Token name = tok[i];
        i++;
        int stars = 0;
        while (peek_kind(i) == TokenKind::Star || peek_kind(i) == TokenKind::StarQuestion) {
            stars++;
            i++;
        }
        if (peek_kind(i) != TokenKind::RParen) {
            return false;
        }
        TokenKind operand = peek_kind(i + 1);
        if (operand == TokenKind::Name || operand == TokenKind::IntLit ||
            operand == TokenKind::FloatLit || operand == TokenKind::CharLit ||
            operand == TokenKind::KwSelf || operand == TokenKind::KwTrue ||
            operand == TokenKind::KwFalse) {
            if (stars == 0 && !is_scalar_type(name.text)) {
                return false;
            }
            return true;
        }
        if (stars == 0 && !is_scalar_type(name.text)) {
            return false;
        }
        return operand == TokenKind::LParen || operand == TokenKind::Minus ||
               operand == TokenKind::MinusPercent || operand == TokenKind::Tilde ||
               operand == TokenKind::Star || operand == TokenKind::Amp;
    }

auto Parser::sync_line() -> void {
        while (!at(TokenKind::EndOfFile) && !at(TokenKind::Newline) && !at(TokenKind::Dedent)) {
            take();
        }
        eat(TokenKind::Newline);
    }

ParseResult parse(const Source& source, const vector<Token>& tokens, Arena& arena,
                  DiagnosticBag& diagnostics) {
    ParseResult result;
    if (tokens.empty()) {
        diagnostics.add("lucb.parse.internal", string(source.path()), Span{},
                        "no tokens");
        return result;
    }
    Parser p;
    p.source = &source;
    p.tok = tokens.data();
    p.n = static_cast<int>(tokens.size());
    p.pos = 0;
    p.arena = &arena;
    p.diag = &diagnostics;
    result.module = p.parse_module();
    result.ok = diagnostics.empty();
    return result;
}

} // namespace lucb
