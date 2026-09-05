//==============================================================================================
//
//   parse/parser_impl - Parser state shared by parse/*.cpp
//
//   DESCRIPTION:
//       The `Parser` struct and the declarations of every production, grouped by unit.
//       Implementation header.
//
//==============================================================================================

#pragma once

#include "lex/lexer.h"
#include "parse/parser.h"

namespace lucb {

const int k_max_nest = 100;

inline int prec_of(TokenKind k) {
    switch (k) {
    case TokenKind::KwOr:
        return 1;
    case TokenKind::KwAnd:
        return 2;
    case TokenKind::EqEq:
    case TokenKind::NotEq:
    case TokenKind::Lt:
    case TokenKind::LtEq:
    case TokenKind::Gt:
    case TokenKind::GtEq:
        return 3;
    case TokenKind::DotDotLt:
    case TokenKind::DotDotEq:
        return 4;
    case TokenKind::Pipe:
        return 5;
    case TokenKind::Caret:
        return 6;
    case TokenKind::Amp:
        return 7;
    case TokenKind::LtLt:
    case TokenKind::GtGt:
        return 8;
    case TokenKind::Plus:
    case TokenKind::Minus:
    case TokenKind::PlusPercent:
    case TokenKind::MinusPercent:
    case TokenKind::PlusPipe:
    case TokenKind::MinusPipe:
    case TokenKind::PlusQuestion:
    case TokenKind::MinusQuestion:
        return 9;
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::SlashSlash:
    case TokenKind::Percent:
    case TokenKind::StarPercent:
    case TokenKind::StarPipe:
    case TokenKind::StarQuestion:
        return 10;
    default:
        return 0;
    }
}

inline bool is_compare(TokenKind k) {
    return k == TokenKind::EqEq || k == TokenKind::NotEq || k == TokenKind::Lt ||
           k == TokenKind::LtEq || k == TokenKind::Gt || k == TokenKind::GtEq;
}

inline bool is_range_op(TokenKind k) {
    return k == TokenKind::DotDotLt || k == TokenKind::DotDotEq;
}

inline bool is_assign_op(TokenKind k) {
    switch (k) {
    case TokenKind::Eq:
    case TokenKind::PlusEq:
    case TokenKind::MinusEq:
    case TokenKind::StarEq:
    case TokenKind::SlashEq:
    case TokenKind::SlashSlashEq:
    case TokenKind::PercentEq:
    case TokenKind::PlusPercentEq:
    case TokenKind::MinusPercentEq:
    case TokenKind::StarPercentEq:
    case TokenKind::PlusPipeEq:
    case TokenKind::MinusPipeEq:
    case TokenKind::StarPipeEq:
    case TokenKind::AmpEq:
    case TokenKind::PipeEq:
    case TokenKind::CaretEq:
    case TokenKind::LtLtEq:
    case TokenKind::GtGtEq:
        return true;
    default:
        return false;
    }
}

inline bool is_keyword_kind(TokenKind k) {
    return k >= TokenKind::KwAlloc && k <= TokenKind::KwWith;
}

inline bool is_core_type(string_view name) {
    return name == "bool" || name == "u8" || name == "u16" || name == "u32" || name == "u64" ||
           name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "usize" ||
           name == "isize" || name == "f16" || name == "f32" || name == "f64" || name == "char" ||
           name == "str" || name == "cstr" || name == "fmt" || name == "unit" || name == "never";
}

inline bool is_type_path_ident(string_view name) {
    if (name.empty()) {
        return false;
    }
    if (name[0] >= 'A' && name[0] <= 'Z') {
        return true;
    }
    return is_core_type(name) || name == "void" || name == "c";
}

inline bool is_scalar_type(string_view name) {
    return name == "bool" || name == "u8" || name == "u16" || name == "u32" || name == "u64" ||
           name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "usize" ||
           name == "isize" || name == "f16" || name == "f32" || name == "f64" || name == "char" ||
           name == "str" || name == "cstr";
}

inline bool is_literal_kind(TokenKind k) {
    return k == TokenKind::IntLit || k == TokenKind::FloatLit || k == TokenKind::CharLit ||
           k == TokenKind::StringLit || k == TokenKind::BytesLit || k == TokenKind::KwTrue ||
           k == TokenKind::KwFalse || k == TokenKind::KwNone;
}

inline bool expr_starts(TokenKind k) {
    return k == TokenKind::Name || k == TokenKind::IntLit || k == TokenKind::FloatLit ||
           k == TokenKind::CharLit || k == TokenKind::StringLit || k == TokenKind::BytesLit ||
           k == TokenKind::FormatStart || k == TokenKind::KwSelf || k == TokenKind::KwTrue ||
           k == TokenKind::KwFalse || k == TokenKind::KwNone || k == TokenKind::LParen ||
           k == TokenKind::LBracket || k == TokenKind::KwMatch || k == TokenKind::KwNew ||
           k == TokenKind::KwAlloc || k == TokenKind::KwTry || k == TokenKind::KwNot ||
           k == TokenKind::Plus || k == TokenKind::Minus || k == TokenKind::MinusPercent ||
           k == TokenKind::Tilde || k == TokenKind::Star || k == TokenKind::Amp ||
           k == TokenKind::Dot || k == TokenKind::Underscore;
}

struct Parser {
    const Source* source;
    const Token* tok;
    int n = 0;
    int pos = 0;
    Arena* arena = nullptr;
    DiagnosticBag* diag = nullptr;
    int nest = 0;

    // `catch` and `match` consume their terminating newline via the suite.

    Token cur() const {
        if (pos >= n) {
            return tok[n - 1];
        }
        return tok[pos];
    }

    Token peek(int off) const {
        int i = pos + off;
        if (i < 0) {
            i = 0;
        }
        if (i >= n) {
            return tok[n - 1];
        }
        return tok[i];
    }

    TokenKind peek_kind(int i) const {
        if (i < 0 || i >= n) {
            return TokenKind::EndOfFile;
        }
        return tok[i].kind;
    }

    string_view peek_text(int i) const {
        if (i < 0 || i >= n) {
            return {};
        }
        return tok[i].text;
    }

    bool at(TokenKind k) const {
        return cur().kind == k;
    }

    bool at_name(const char* s) const {
        return cur().kind == TokenKind::Name && cur().text == s;
    }

    bool at_ident() const {
        return at(TokenKind::Name) || is_keyword_kind(cur().kind);
    }

    Token take() {
        Token t = cur();
        if (pos < n - 1) {
            pos++;
        }
        return t;
    }

    bool eat(TokenKind k) {
        if (at(k)) {
            take();
            return true;
        }
        return false;
    }

    void fail(const char* code, const char* message) {
        Token t = cur();
        diag->add(code, string(source->path()), t.span, message);
    }

    bool expect(TokenKind k, const char* code, const char* message) {
        if (eat(k)) {
            return true;
        }
        fail(code, message);
        return false;
    }

    bool ends_with_suite(Node* n) {
        return n != nullptr && (n->kind == NodeKind::Catch || n->kind == NodeKind::Match ||
                                n->kind == NodeKind::MatchExpr);
    }

    void expect_stmt_newline(Node* expr) {
        if (ends_with_suite(expr)) {
            return;
        }
        if (at(TokenKind::RParen) || at(TokenKind::Comma) || at(TokenKind::RBracket)) {
            return;
        }
        expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
    }

    Span span_from(Token start) const {
        Span s = start.span;
        Token last = peek(-1);
        if (pos > 0) {
            s.end = last.span.end;
        }
        return s;
    }

    Node* make(NodeKind kind, Span span) {
        Node* n = arena->make<Node>();
        n->kind = kind;
        n->span = span;
        return n;
    }

    Node* make_tok(NodeKind kind, Token t) {
        Node* n = make(kind, t.span);
        n->text = t.text;
        return n;
    }

    void skip_docs();
    int find_match(int start, TokenKind open, TokenKind close) const;
    bool is_lambda_ahead() const;
    bool is_array_suffix_ahead() const;
    bool is_generic_call_ahead() const;
    bool is_scalar_cast_ahead() const;
    void sync_line();
    Node* parse_module();
    Node* parse_import();
    string_view parse_module_path();
    uint32_t parse_attributes(Node** attrs);
    Node* parse_top();
    Node* parse_const(uint32_t flags);
    Node* parse_global(uint32_t flags);
    Node* parse_type_alias(uint32_t flags);
    Node* parse_func(uint32_t flags);
    Node* parse_generic_params();
    Node* parse_params(bool extern_form);
    Node* parse_struct(uint32_t flags, bool is_extern);
    Node* parse_implements();
    Node* parse_type_member(bool is_extern);
    Node* parse_enum(uint32_t flags);
    Node* parse_union(uint32_t flags);
    Node* parse_interface(uint32_t flags);
    Node* parse_extern(uint32_t flags);
    Node* parse_test();
    Node* parse_assert();
    Node* parse_asm();
    Node* parse_suite();
    Node* parse_statement();
    Node* parse_simple_stmt();
    Node* parse_free();
    Node* parse_binding();
    Node* parse_condition();
    Node* parse_if();
    Node* parse_while();
    Node* parse_for();
    Node* parse_with();
    Node* parse_match(bool as_expr);
    Node* parse_pattern();
    Node* parse_expression();
    Node* finish_expression(Node* unary);
    Node* parse_else_expr();
    Node* parse_else_rest(Node* n);
    Node* parse_conditional();
    Node* parse_conditional_rest(Node* n);
    Node* parse_binary(int min_prec);
    Node* parse_binary_rest(Node* left, int min_prec);
    Node* parse_unary();
    Node* parse_postfix();
    Node* expr_as_type(Node* value);
    Node* parse_subscript(Node* value, Token start);
    Node* parse_arg_list();
    Node* parse_type_args();
    Node* parse_primary();
    Node* parse_type_builtin();
    Node* parse_literal();
    Node* parse_formatted();
    Node* parse_lambda();
    Node* parse_group_or_tuple();
    Node* parse_array_lit();
    Node* parse_new_or_alloc(bool is_alloc);
    Node* parse_type();
    Node* parse_primary_type();
};

} // namespace lucb
