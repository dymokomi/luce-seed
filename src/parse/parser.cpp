//==============================================================================================
//
//   parse/parser - Parser state and recovery
//
//   DESCRIPTION:
//       The token cursor, checkpoints, error recovery that forces progress, and the `parse`
//       entry point.
//
//==============================================================================================

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

// Whether the bracket at `open` holds something no type can: an arithmetic or comparison
// operator, a literal past the first token, a lowercase field after a dot, or `*` in the
// middle. Such brackets are an index or an array length whatever their first name looks like
// (§5.4, §7.6); a bracket holding only names is decided by what the names resolve to.
auto Parser::bracket_holds_expression(int open) const -> bool {
    int close = find_match(open, TokenKind::LBracket, TokenKind::RBracket);
    if (close < 0) {
        return false;
    }
    int depth = 0;
    for (int i = open + 1; i < close; i++) {
        TokenKind k = tok[i].kind;
        if (k == TokenKind::LParen || k == TokenKind::LBracket) {
            depth++;
            continue;
        }
        if (k == TokenKind::RParen || k == TokenKind::RBracket) {
            depth--;
            continue;
        }
        if (depth > 0) {
            continue;
        }
        TokenKind next = tok[i + 1].kind;
        switch (k) {
        case TokenKind::Plus: case TokenKind::Minus: case TokenKind::Slash: case TokenKind::Percent:
        case TokenKind::Lt: case TokenKind::Gt: case TokenKind::LtEq: case TokenKind::GtEq:
        case TokenKind::EqEq: case TokenKind::NotEq: case TokenKind::SlashSlash: case TokenKind::Pipe: case TokenKind::Caret:
        case TokenKind::KwAnd: case TokenKind::KwOr: case TokenKind::KwNot:
        case TokenKind::FloatLit: case TokenKind::CharLit: case TokenKind::StringLit:
        case TokenKind::DotDot: case TokenKind::DotDotLt:
            return true;
        case TokenKind::IntLit:
            if (i > open + 1) {
                return true; // `T[3]` is an array type; `n - 3` is not
            }
            break;
        case TokenKind::Star:
            if (next == TokenKind::Name || next == TokenKind::IntLit || next == TokenKind::LParen) {
                return true; // `a * b`, not `T*`
            }
            break;
        case TokenKind::Dot:
            if (next == TokenKind::Name && !tok[i + 1].text.empty() &&
                tok[i + 1].text[0] >= 'a' && tok[i + 1].text[0] <= 'z' && i > open + 1 &&
                tok[i - 1].text != "c") {
                return true; // `s.count`: a field, not `module.Type`
            }
            break;
        default:
            break;
        }
    }
    return false;
}

auto Parser::is_array_suffix_ahead() const -> bool {
    if (!at(TokenKind::LBracket)) {
        return false;
    }
    TokenKind k = peek(1).kind;
    if (k == TokenKind::RBracket) {
        return true;
    }
    if (bracket_holds_expression(pos)) {
        return true;
    }
    if (k == TokenKind::IntLit || k == TokenKind::LParen || k == TokenKind::KwSelf) {
        return true; // `T[N]`, `T[(n)]`, `T[self.count]`
    }
    if (k == TokenKind::Name) {
        string_view t = peek(1).text;
        if (t == "sizeof" || t == "alignof") {
            return true;
        }
        if (is_type_path_ident(t)) {
            return false;
        }
        // `module.Type` inside the brackets names a type argument
        if (peek_kind(pos + 2) == TokenKind::Dot && peek_kind(pos + 3) == TokenKind::Name) {
            string_view q = tok[pos + 3].text;
            if (!q.empty() && q[0] >= 'A' && q[0] <= 'Z') {
                return false;
            }
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
    if (close < 0 || close + 1 >= n) {
        return false;
    }
    TokenKind after = tok[close + 1].kind;
    if (after != TokenKind::LParen && after != TokenKind::Dot) {
        return false;
    }
    if (bracket_holds_expression(pos)) {
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
    // `module.Type`: a lowercase module name, a dot, then a type name; `c.str` and the
    // other members of the `c` module are types spelled in lowercase (base.md §5.2).
    if (peek_kind(pos + 2) == TokenKind::Dot && peek_kind(pos + 3) == TokenKind::Name) {
        Token qualified = tok[pos + 3];
        if (!qualified.text.empty() && qualified.text[0] >= 'A' && qualified.text[0] <= 'Z') {
            return true;
        }
        if (first.text == "c") {
            return true;
        }
    }
    return false;
}

auto Parser::is_scalar_cast_ahead() const -> bool {
    if (!at(TokenKind::LParen)) {
        return false;
    }
    if (peek_kind(pos + 1) == TokenKind::KwFunc) {
        // `(func(i64) -> i64)p`: a parenthesised function type is a cast (§7.5)
        int close = find_match(pos, TokenKind::LParen, TokenKind::RParen);
        TokenKind operand = close > 0 ? peek_kind(close + 1) : TokenKind::EndOfFile;
        return operand == TokenKind::Name || operand == TokenKind::LParen ||
               operand == TokenKind::Amp || operand == TokenKind::Star || operand == TokenKind::KwSelf;
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
    if (peek_kind(i) == TokenKind::Dot && peek_kind(i + 1) == TokenKind::Name) {
        i += 2; // `c.int`, `module.Type`, `module.alias`
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
        operand == TokenKind::StringLit || operand == TokenKind::BytesLit ||
        operand == TokenKind::KwSelf || operand == TokenKind::KwTrue ||
        operand == TokenKind::KwFalse) {
        // `(Kind)n` casts to an integer-backed enum (§10.3); `(x) y` means nothing else,
        // while `(Name)(x)` stays a call
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
        diagnostics.add("lucb.parse.internal", string(source.path()), Span{}, "no tokens");
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
