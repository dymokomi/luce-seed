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
        TokenKind k = peek(1).kind;
        if (k == TokenKind::RBracket) {
            return true;
        }
        if (k == TokenKind::IntLit || k == TokenKind::LParen) {
            return true;
        }
        if (k == TokenKind::Name) {
            string_view t = peek(1).text;
            if (t == "sizeof" || t == "alignof") {
                return true;
            }
            if (is_type_path_ident(t)) {
                return false;
            }
            return true;
        }
        return false;
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
        if (peek_kind(i) != TokenKind::Name) {
            return false;
        }
        Token name = tok[i];
        i++;
        if (name.text == "c" && peek_kind(i) == TokenKind::Dot && peek_kind(i + 1) == TokenKind::Name) {
            i += 2;
        }
        int stars = 0;
        while (true) {
            if (peek_kind(i) == TokenKind::Star || peek_kind(i) == TokenKind::StarQuestion) {
                stars++;
                i++;
                continue;
            }
            if (peek_kind(i) == TokenKind::Question || peek_kind(i) == TokenKind::Bang) {
                i++;
                continue;
            }
            if (peek_kind(i) == TokenKind::LBracket) {
                int close = find_match(i, TokenKind::LBracket, TokenKind::RBracket);
                if (close < 0) {
                    return false;
                }
                i = close + 1;
                continue;
            }
            break;
        }
        if (peek_kind(i) != TokenKind::RParen) {
            return false;
        }
        TokenKind operand = peek_kind(i + 1);
        if (operand == TokenKind::Name || operand == TokenKind::IntLit ||
            operand == TokenKind::FloatLit || operand == TokenKind::CharLit ||
            operand == TokenKind::KwSelf || operand == TokenKind::KwTrue ||
            operand == TokenKind::KwFalse) {
            if (stars == 0 && !is_scalar_type(name.text) && name.text != "c") {
                return false;
            }
            return true;
        }
        if (stars == 0 && !is_scalar_type(name.text) && name.text != "c") {
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
