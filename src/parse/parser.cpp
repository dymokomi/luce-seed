#include "parse/parser.h"

namespace lucb {
namespace {

const int k_max_nest = 100;

int prec_of(TokenKind k) {
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

bool is_compare(TokenKind k) {
    return k == TokenKind::EqEq || k == TokenKind::NotEq || k == TokenKind::Lt ||
           k == TokenKind::LtEq || k == TokenKind::Gt || k == TokenKind::GtEq;
}

bool is_range_op(TokenKind k) {
    return k == TokenKind::DotDotLt || k == TokenKind::DotDotEq;
}

bool is_assign_op(TokenKind k) {
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

bool is_core_type(string_view name) {
    return name == "bool" || name == "u8" || name == "u16" || name == "u32" || name == "u64" ||
           name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "usize" ||
           name == "isize" || name == "f16" || name == "f32" || name == "f64" || name == "char" ||
           name == "str" || name == "cstr" || name == "fmt" || name == "unit" || name == "never";
}

bool is_scalar_type(string_view name) {
    return name == "bool" || name == "u8" || name == "u16" || name == "u32" || name == "u64" ||
           name == "i8" || name == "i16" || name == "i32" || name == "i64" || name == "usize" ||
           name == "isize" || name == "f16" || name == "f32" || name == "f64" || name == "char" ||
           name == "str" || name == "cstr";
}

bool is_literal_kind(TokenKind k) {
    return k == TokenKind::IntLit || k == TokenKind::FloatLit || k == TokenKind::CharLit ||
           k == TokenKind::StringLit || k == TokenKind::BytesLit || k == TokenKind::KwTrue ||
           k == TokenKind::KwFalse || k == TokenKind::KwNone;
}

bool expr_starts(TokenKind k) {
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

    bool at(TokenKind k) const { return cur().kind == k; }

    bool at_name(const char* s) const {
        return cur().kind == TokenKind::Name && cur().text == s;
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

    void skip_docs() {
        while (at(TokenKind::DocComment) || at(TokenKind::Newline)) {
            take();
        }
    }

    int find_match(int start, TokenKind open, TokenKind close) const {
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

    bool is_lambda_ahead() const {
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

    bool is_array_suffix_ahead() const {
        if (!at(TokenKind::LBracket)) {
            return false;
        }
        if (peek(1).kind == TokenKind::RBracket) {
            return true;
        }
        return peek(1).kind == TokenKind::IntLit && peek(2).kind == TokenKind::RBracket;
    }

    bool is_generic_call_ahead() const {
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

    bool is_scalar_cast_ahead() const {
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

    void sync_line() {
        while (!at(TokenKind::EndOfFile) && !at(TokenKind::Newline) && !at(TokenKind::Dedent)) {
            take();
        }
        eat(TokenKind::Newline);
    }

    Node* parse_module() {
        Token start = cur();
        Node* mod = make(NodeKind::Module, start.span);
        skip_docs();
        while (at(TokenKind::KwImport) || at(TokenKind::KwFrom)) {
            Node* imp = parse_import();
            append_node(&mod->body, imp);
            skip_docs();
        }
        while (!at(TokenKind::EndOfFile)) {
            skip_docs();
            if (at(TokenKind::EndOfFile)) {
                break;
            }
            if (at(TokenKind::KwImport) || at(TokenKind::KwFrom)) {
                fail("lucb.parse.import", "imports must appear at the top of the file");
                parse_import();
                continue;
            }
            int here = pos;
            Node* decl = parse_top();
            if (decl != nullptr) {
                append_node(&mod->body, decl);
            }
            if (pos == here) {
                take();
            }
        }
        mod->span.end = cur().span.end;
        return mod;
    }

    Node* parse_import() {
        Token start = cur();
        if (eat(TokenKind::KwFrom)) {
            Node* n = make(NodeKind::FromImport, start.span);
            n->text = parse_module_path();
            expect(TokenKind::KwImport, "lucb.parse.expect", "expected `import`");
            Node* names = nullptr;
            do {
                Token id = cur();
                if (!at(TokenKind::Name)) {
                    fail("lucb.parse.expect", "expected a name");
                    break;
                }
                take();
                append_node(&names, make_tok(NodeKind::Name, id));
            } while (eat(TokenKind::Comma));
            n->body = names;
            expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
            n->span = span_from(start);
            return n;
        }
        expect(TokenKind::KwImport, "lucb.parse.expect", "expected `import`");
        Node* n = make(NodeKind::Import, start.span);
        n->text = parse_module_path();
        if (eat(TokenKind::KwAs)) {
            if (!at(TokenKind::Name)) {
                fail("lucb.parse.expect", "expected an alias name");
            } else {
                n->left = make_tok(NodeKind::Name, take());
            }
        }
        expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
        n->span = span_from(start);
        return n;
    }

    string_view parse_module_path() {
        if (!at(TokenKind::Name)) {
            fail("lucb.parse.expect", "expected a module path");
            return {};
        }
        Token first = take();
        size_t start = first.span.start;
        size_t end = first.span.end;
        while (eat(TokenKind::Dot)) {
            if (!at(TokenKind::Name)) {
                fail("lucb.parse.expect", "expected a name after `.`");
                break;
            }
            Token part = take();
            end = part.span.end;
        }
        return source->bytes().substr(start, end - start);
    }

    uint32_t parse_attributes(Node** attrs) {
        uint32_t flags = 0;
        while (true) {
            if (at_name("inline")) {
                take();
                flags |= FlagInline;
            } else if (at_name("noinline")) {
                take();
                flags |= FlagNoinline;
            } else if (at_name("cold")) {
                take();
                flags |= FlagCold;
            } else if (at_name("naked")) {
                take();
                flags |= FlagNaked;
            } else if (at_name("used")) {
                take();
                flags |= FlagUsed;
            } else if (at(TokenKind::KwWeak)) {
                take();
                flags |= FlagWeakAttr;
            } else if (at_name("section")) {
                Token t = take();
                Node* a = make_tok(NodeKind::Attr, t);
                expect(TokenKind::LParen, "lucb.parse.expect", "expected `(`");
                if (at(TokenKind::StringLit)) {
                    a->left = make_tok(NodeKind::Literal, take());
                    a->left->op = TokenKind::StringLit;
                } else {
                    fail("lucb.parse.expect", "expected a string literal");
                }
                expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
                append_node(attrs, a);
            } else {
                break;
            }
        }
        return flags;
    }

    Node* parse_top() {
        if (at(TokenKind::KwClass) || at(TokenKind::KwSpawn)) {
            fail("lucb.parse.tier", "this construct belongs to full Luce");
            take();
            sync_line();
            if (at(TokenKind::Indent)) {
                int depth = 1;
                take();
                while (depth > 0 && !at(TokenKind::EndOfFile)) {
                    if (at(TokenKind::Indent)) {
                        depth++;
                    } else if (at(TokenKind::Dedent)) {
                        depth--;
                    }
                    take();
                }
            }
            return nullptr;
        }
        if (at(TokenKind::KwGoto)) {
            fail("lucb.parse.reserved", "goto is reserved and not implemented");
            take();
            sync_line();
            return nullptr;
        }
        if (at(TokenKind::KwTest)) {
            return parse_test();
        }
        if (at(TokenKind::KwAsm)) {
            return parse_asm();
        }
        if (at(TokenKind::Name) && cur().text == "assert" && peek(1).kind == TokenKind::LParen) {
            return parse_assert();
        }
        if (eat(TokenKind::KwExport)) {
            Node* fn = parse_func(0);
            if (fn != nullptr) {
                fn->flags |= FlagExport;
            }
            return fn;
        }

        uint32_t flags = 0;
        if (eat(TokenKind::KwPub)) {
            flags |= FlagPub;
        }
        Node* attrs = nullptr;
        flags |= parse_attributes(&attrs);

        if (at(TokenKind::KwThreadLocal) || at(TokenKind::KwVar)) {
            return parse_global(flags);
        }
        if (at(TokenKind::KwLet)) {
            return parse_const(flags);
        }
        if (at(TokenKind::KwType)) {
            return parse_type_alias(flags);
        }
        if (at(TokenKind::KwFunc) || at(TokenKind::KwStatic) || at(TokenKind::KwMutating) ||
            at_name("inline") || at(TokenKind::KwExtern)) {
            if (at(TokenKind::KwExtern)) {
                return parse_extern(flags);
            }
            return parse_func(flags);
        }
        if (at(TokenKind::KwStruct) || at_name("packed") || at_name("align")) {
            return parse_struct(flags, false);
        }
        if (at(TokenKind::KwEnum)) {
            return parse_enum(flags);
        }
        if (at(TokenKind::KwUnion)) {
            return parse_union(flags);
        }
        if (at(TokenKind::KwInterface)) {
            return parse_interface(flags);
        }
        if (at(TokenKind::KwExtern)) {
            return parse_extern(flags);
        }
        fail("lucb.parse.expect", "expected a declaration");
        sync_line();
        return nullptr;
    }

    Node* parse_const(uint32_t flags) {
        Token start = cur();
        expect(TokenKind::KwLet, "lucb.parse.expect", "expected `let`");
        Node* n = make(NodeKind::Const, start.span);
        n->flags = flags;
        if (!at(TokenKind::Name)) {
            fail("lucb.parse.expect", "expected a name");
        } else {
            n->text = take().text;
        }
        if (eat(TokenKind::Colon)) {
            n->type = parse_type();
        }
        expect(TokenKind::Eq, "lucb.parse.expect", "expected `=`");
        n->left = parse_expression();
        expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
        n->span = span_from(start);
        return n;
    }

    Node* parse_global(uint32_t flags) {
        Token start = cur();
        Node* n = make(NodeKind::Global, start.span);
        if (eat(TokenKind::KwThreadLocal)) {
            flags |= FlagThreadLocal;
        }
        Node* attrs = nullptr;
        flags |= parse_attributes(&attrs);
        expect(TokenKind::KwVar, "lucb.parse.expect", "expected `var`");
        n->flags = flags;
        if (!at(TokenKind::Name)) {
            fail("lucb.parse.expect", "expected a name");
        } else {
            n->text = take().text;
        }
        expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
        n->type = parse_type();
        if (eat(TokenKind::Eq)) {
            if (eat(TokenKind::DashDashDash)) {
                n->flags |= FlagUninit;
            } else {
                n->left = parse_expression();
            }
        }
        expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
        n->span = span_from(start);
        return n;
    }

    Node* parse_type_alias(uint32_t flags) {
        Token start = cur();
        expect(TokenKind::KwType, "lucb.parse.expect", "expected `type`");
        Node* n = make(NodeKind::TypeAlias, start.span);
        n->flags = flags;
        if (!at(TokenKind::Name)) {
            fail("lucb.parse.expect", "expected a type name");
        } else {
            n->text = take().text;
        }
        expect(TokenKind::Eq, "lucb.parse.expect", "expected `=`");
        n->type = parse_type();
        expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
        n->span = span_from(start);
        return n;
    }

    Node* parse_func(uint32_t flags) {
        Token start = cur();
        Node* attrs = nullptr;
        flags |= parse_attributes(&attrs);
        if (eat(TokenKind::KwStatic)) {
            flags |= FlagStatic;
        }
        if (eat(TokenKind::KwMutating)) {
            flags |= FlagMutating;
        }
        expect(TokenKind::KwFunc, "lucb.parse.expect", "expected `func`");
        Node* n = make(NodeKind::Func, start.span);
        n->flags = flags;
        if (!at(TokenKind::Name)) {
            fail("lucb.parse.expect", "expected a function name");
        } else {
            n->text = take().text;
        }
        if (at(TokenKind::LBracket) && !is_array_suffix_ahead()) {
            n->left = parse_generic_params();
        }
        n->right = parse_params(false);
        if (eat(TokenKind::Arrow)) {
            if (eat(TokenKind::Bang)) {
                Node* t = make(NodeKind::Type, peek(-1).span);
                t->flags = FlagFallible;
                t->text = "unit";
                n->type = t;
            } else {
                n->type = parse_type();
            }
        }
        expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
        n->body = parse_suite();
        n->span = span_from(start);
        return n;
    }

    Node* parse_generic_params() {
        expect(TokenKind::LBracket, "lucb.parse.expect", "expected `[`");
        Node* list = nullptr;
        if (!at(TokenKind::RBracket)) {
            while (true) {
                Token t = cur();
                Node* g = make(NodeKind::GenericParam, t.span);
                if (!at(TokenKind::Name)) {
                    fail("lucb.parse.expect", "expected a type parameter");
                    break;
                }
                g->text = take().text;
                if (eat(TokenKind::Colon)) {
                    Node* bounds = nullptr;
                    append_node(&bounds, parse_type());
                    while (eat(TokenKind::Amp)) {
                        append_node(&bounds, parse_type());
                    }
                    g->type = bounds;
                }
                append_node(&list, g);
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

    Node* parse_params(bool extern_form) {
        expect(TokenKind::LParen, "lucb.parse.expect", "expected `(`");
        Node* list = nullptr;
        if (!at(TokenKind::RParen)) {
            while (true) {
                if (eat(TokenKind::DotDotDot)) {
                    Node* p = make(NodeKind::Param, peek(-1).span);
                    p->flags = FlagVariadic;
                    append_node(&list, p);
                    break;
                }
                Node* p = make(NodeKind::Param, cur().span);
                if (extern_form && at_name("out")) {
                    take();
                    p->flags |= FlagOut;
                }
                if (!at(TokenKind::Name) && !at(TokenKind::KwSelf)) {
                    fail("lucb.parse.expect", "expected a parameter name");
                    break;
                }
                p->text = take().text;
                expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
                if (at_name("noalias")) {
                    take();
                    p->flags |= FlagNoalias;
                }
                p->type = parse_type();
                if (eat(TokenKind::Eq)) {
                    if (at(TokenKind::Name) && cur().text == "location" &&
                        peek(1).kind == TokenKind::LParen) {
                        Node* call = make(NodeKind::Call, cur().span);
                        call->left = make_tok(NodeKind::Name, take());
                        expect(TokenKind::LParen, "lucb.parse.expect", "expected `(`");
                        expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
                        p->left = call;
                    } else {
                        p->left = parse_expression();
                    }
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
        return list;
    }

    Node* parse_struct(uint32_t flags, bool is_extern) {
        Token start = cur();
        if (at_name("packed")) {
            take();
            flags |= FlagPacked;
        } else if (at_name("align")) {
            take();
            expect(TokenKind::LParen, "lucb.parse.expect", "expected `(`");
            parse_expression();
            expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
        }
        expect(TokenKind::KwStruct, "lucb.parse.expect", "expected `struct`");
        Node* n = make(is_extern ? NodeKind::ExternStruct : NodeKind::Struct, start.span);
        n->flags = flags;
        if (!at(TokenKind::Name)) {
            fail("lucb.parse.expect", "expected a type name");
        } else {
            n->text = take().text;
        }
        if (!is_extern && at(TokenKind::LBracket) && !is_array_suffix_ahead()) {
            n->left = parse_generic_params();
        }
        if (!is_extern) {
            n->right = parse_implements();
        }
        expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
        expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
        expect(TokenKind::Indent, "lucb.parse.expect", "expected an indented body");
        while (!at(TokenKind::Dedent) && !at(TokenKind::EndOfFile)) {
            skip_docs();
            if (at(TokenKind::Dedent)) {
                break;
            }
            append_node(&n->body, parse_type_member(is_extern));
        }
        expect(TokenKind::Dedent, "lucb.parse.expect", "expected a dedent");
        n->span = span_from(start);
        return n;
    }

    Node* parse_implements() {
        if (!eat(TokenKind::KwImplements)) {
            return nullptr;
        }
        Node* list = nullptr;
        append_node(&list, parse_type());
        while (eat(TokenKind::Comma)) {
            append_node(&list, parse_type());
        }
        return list;
    }

    Node* parse_type_member(bool is_extern) {
        uint32_t flags = 0;
        if (eat(TokenKind::KwPub)) {
            flags |= FlagPub;
        }
        if (eat(TokenKind::KwExport)) {
            flags |= FlagExport;
        }
        if (at_name("align")) {
            take();
            expect(TokenKind::LParen, "lucb.parse.expect", "expected `(`");
            parse_expression();
            expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
        }
        if (at(TokenKind::KwLet) || at(TokenKind::KwVar) || (is_extern && at(TokenKind::Name))) {
            Token start = cur();
            Node* f = make(NodeKind::Field, start.span);
            f->flags = flags;
            if (at(TokenKind::KwLet) || at(TokenKind::KwVar)) {
                if (at(TokenKind::KwLet)) {
                    f->flags |= FlagConst;
                }
                take();
            }
            if (!at(TokenKind::Name)) {
                fail("lucb.parse.expect", "expected a field name");
            } else {
                f->text = take().text;
            }
            expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
            f->type = parse_type();
            if (eat(TokenKind::Eq)) {
                f->left = parse_expression();
            }
            expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
            f->span = span_from(start);
            return f;
        }
        Node* fn = parse_func(flags);
        return fn;
    }

    Node* parse_enum(uint32_t flags) {
        Token start = cur();
        expect(TokenKind::KwEnum, "lucb.parse.expect", "expected `enum`");
        Node* n = make(NodeKind::Enum, start.span);
        n->flags = flags;
        if (!at(TokenKind::Name)) {
            fail("lucb.parse.expect", "expected a type name");
        } else {
            n->text = take().text;
        }
        if (at(TokenKind::LBracket) && !is_array_suffix_ahead()) {
            n->left = parse_generic_params();
        }
        if (eat(TokenKind::KwAs)) {
            n->right = parse_type();
        }
        Node* impl = parse_implements();
        if (impl != nullptr) {
            // Store implements as sibling of as-type via extra chain on right.
            if (n->right != nullptr) {
                n->right->next = impl;
            } else {
                n->right = impl;
            }
        }
        expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
        expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
        expect(TokenKind::Indent, "lucb.parse.expect", "expected an indented body");
        while (!at(TokenKind::Dedent) && !at(TokenKind::EndOfFile)) {
            skip_docs();
            if (at(TokenKind::Dedent)) {
                break;
            }
            uint32_t mflags = 0;
            if (eat(TokenKind::KwPub)) {
                mflags |= FlagPub;
            }
            if (at(TokenKind::KwFunc) || at(TokenKind::KwStatic) || at(TokenKind::KwMutating)) {
                append_node(&n->body, parse_func(mflags));
                continue;
            }
            Token t = cur();
            Node* c = make(NodeKind::EnumCase, t.span);
            if (!at(TokenKind::Name)) {
                fail("lucb.parse.expect", "expected a case name");
                sync_line();
                continue;
            }
            c->text = take().text;
            if (at(TokenKind::LParen)) {
                c->body = parse_params(false);
            } else if (eat(TokenKind::Eq)) {
                c->left = parse_expression();
            }
            expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
            append_node(&n->body, c);
        }
        expect(TokenKind::Dedent, "lucb.parse.expect", "expected a dedent");
        n->span = span_from(start);
        return n;
    }

    Node* parse_union(uint32_t flags) {
        Token start = cur();
        expect(TokenKind::KwUnion, "lucb.parse.expect", "expected `union`");
        Node* n = make(NodeKind::Union, start.span);
        n->flags = flags;
        if (!at(TokenKind::Name)) {
            fail("lucb.parse.expect", "expected a type name");
        } else {
            n->text = take().text;
        }
        expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
        expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
        expect(TokenKind::Indent, "lucb.parse.expect", "expected an indented body");
        while (!at(TokenKind::Dedent) && !at(TokenKind::EndOfFile)) {
            skip_docs();
            if (at(TokenKind::Dedent)) {
                break;
            }
            if (at(TokenKind::KwFunc) || at(TokenKind::KwPub) || at(TokenKind::KwMutating)) {
                uint32_t mflags = 0;
                if (eat(TokenKind::KwPub)) {
                    mflags |= FlagPub;
                }
                append_node(&n->body, parse_func(mflags));
                continue;
            }
            Token t = cur();
            Node* m = make(NodeKind::Field, t.span);
            if (!at(TokenKind::Name)) {
                fail("lucb.parse.expect", "expected a member name");
                sync_line();
                continue;
            }
            m->text = take().text;
            expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
            m->type = parse_type();
            expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
            append_node(&n->body, m);
        }
        expect(TokenKind::Dedent, "lucb.parse.expect", "expected a dedent");
        n->span = span_from(start);
        return n;
    }

    Node* parse_interface(uint32_t flags) {
        Token start = cur();
        expect(TokenKind::KwInterface, "lucb.parse.expect", "expected `interface`");
        Node* n = make(NodeKind::Interface, start.span);
        n->flags = flags;
        if (!at(TokenKind::Name)) {
            fail("lucb.parse.expect", "expected a type name");
        } else {
            n->text = take().text;
        }
        if (at(TokenKind::LBracket) && !is_array_suffix_ahead()) {
            n->left = parse_generic_params();
        }
        expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
        expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
        expect(TokenKind::Indent, "lucb.parse.expect", "expected an indented body");
        while (!at(TokenKind::Dedent) && !at(TokenKind::EndOfFile)) {
            skip_docs();
            if (at(TokenKind::Dedent)) {
                break;
            }
            // Signature only: mutating func name(...) -> T newline
            Token fs = cur();
            uint32_t mflags = 0;
            if (eat(TokenKind::KwMutating)) {
                mflags |= FlagMutating;
            }
            expect(TokenKind::KwFunc, "lucb.parse.expect", "expected `func`");
            Node* fn = make(NodeKind::Func, fs.span);
            fn->flags = mflags;
            if (!at(TokenKind::Name)) {
                fail("lucb.parse.expect", "expected a method name");
            } else {
                fn->text = take().text;
            }
            if (at(TokenKind::LBracket) && !is_array_suffix_ahead()) {
                fn->left = parse_generic_params();
            }
            fn->right = parse_params(false);
            if (eat(TokenKind::Arrow)) {
                if (eat(TokenKind::Bang)) {
                    Node* t = make(NodeKind::Type, peek(-1).span);
                    t->flags = FlagFallible;
                    t->text = "unit";
                    fn->type = t;
                } else {
                    fn->type = parse_type();
                }
            }
            expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
            append_node(&n->body, fn);
        }
        expect(TokenKind::Dedent, "lucb.parse.expect", "expected a dedent");
        n->span = span_from(start);
        return n;
    }

    Node* parse_extern(uint32_t flags) {
        Token start = cur();
        expect(TokenKind::KwExtern, "lucb.parse.expect", "expected `extern`");
        if (at_name("blocking")) {
            take();
            flags |= FlagBlocking;
        }
        if (at(TokenKind::KwType)) {
            take();
            Node* n = make(NodeKind::ExternType, start.span);
            n->flags = flags;
            if (!at(TokenKind::Name)) {
                fail("lucb.parse.expect", "expected a type name");
            } else {
                n->text = take().text;
            }
            if (eat(TokenKind::Eq)) {
                n->type = parse_type();
            }
            expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
            return n;
        }
        if (at(TokenKind::KwVar)) {
            take();
            Node* n = make(NodeKind::ExternVar, start.span);
            n->flags = flags;
            if (!at(TokenKind::Name)) {
                fail("lucb.parse.expect", "expected a name");
            } else {
                n->text = take().text;
            }
            expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
            n->type = parse_type();
            expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
            return n;
        }
        if (at(TokenKind::KwStruct) || at_name("packed") || at_name("align")) {
            return parse_struct(flags, true);
        }
        if (at(TokenKind::KwUnion)) {
            Node* u = parse_union(flags);
            u->kind = NodeKind::ExternUnion;
            return u;
        }
        if (at_name("blocking")) {
            take();
            flags |= FlagBlocking;
        }
        expect(TokenKind::KwFunc, "lucb.parse.expect", "expected `func`");
        Node* n = make(NodeKind::ExternFunc, start.span);
        n->flags = flags;
        if (!at(TokenKind::Name)) {
            fail("lucb.parse.expect", "expected a function name");
        } else {
            n->text = take().text;
        }
        if (eat(TokenKind::KwAs)) {
            if (at(TokenKind::StringLit)) {
                n->left = make_tok(NodeKind::Literal, take());
                n->left->op = TokenKind::StringLit;
            } else {
                fail("lucb.parse.expect", "expected a string literal");
            }
        }
        n->right = parse_params(true);
        if (eat(TokenKind::Arrow)) {
            if (eat(TokenKind::Bang)) {
                Node* t = make(NodeKind::Type, peek(-1).span);
                t->flags = FlagFallible;
                n->type = t;
            } else {
                n->type = parse_type();
            }
        }
        expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
        n->span = span_from(start);
        return n;
    }

    Node* parse_test() {
        Token start = take();
        Node* n = make(NodeKind::Test, start.span);
        if (!at(TokenKind::StringLit)) {
            fail("lucb.parse.expect", "expected a test name string");
        } else {
            n->text = take().text;
        }
        expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
        n->body = parse_suite();
        n->span = span_from(start);
        return n;
    }

    Node* parse_assert() {
        Token start = take();
        Node* n = make(NodeKind::Assert, start.span);
        n->body = parse_arg_list();
        expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
        n->span = span_from(start);
        return n;
    }

    Node* parse_asm() {
        Token start = take();
        Node* n = make(NodeKind::Asm, start.span);
        if (!at(TokenKind::Name)) {
            fail("lucb.parse.expect", "expected an architecture name");
        } else {
            n->text = take().text;
        }
        if (eat(TokenKind::LParen)) {
            Node* ops = nullptr;
            if (!at(TokenKind::RParen)) {
                while (true) {
                    Node* op = make(NodeKind::Param, cur().span);
                    if (at_name("in") || at_name("out") || at_name("inout") ||
                        at_name("options")) {
                        op->text = take().text;
                    } else if (at(TokenKind::Name)) {
                        op->text = take().text;
                    } else {
                        fail("lucb.parse.expect", "expected an asm operand");
                        break;
                    }
                    if (eat(TokenKind::LParen)) {
                        // place then ) then expression/lvalue
                        if (at(TokenKind::StringLit) || at_name("reg") || at(TokenKind::Name)) {
                            op->type = make_tok(NodeKind::Name, take());
                        }
                        expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
                        if (!at(TokenKind::RParen) && !at(TokenKind::Comma)) {
                            op->left = parse_expression();
                        }
                    }
                    append_node(&ops, op);
                    if (!eat(TokenKind::Comma)) {
                        break;
                    }
                }
            }
            expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
            n->left = ops;
        }
        expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
        expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
        expect(TokenKind::Indent, "lucb.parse.expect", "expected an indented body");
        Node* lines = nullptr;
        while (at(TokenKind::RawLine) || at(TokenKind::Newline)) {
            if (at(TokenKind::RawLine)) {
                append_node(&lines, make_tok(NodeKind::Literal, take()));
            } else {
                take();
            }
        }
        n->body = lines;
        expect(TokenKind::Dedent, "lucb.parse.expect", "expected a dedent");
        n->span = span_from(start);
        return n;
    }

    Node* parse_suite() {
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
        }
        Node* block = make(NodeKind::Block, start.span);
        Node* s = parse_simple_stmt();
        append_node(&block->body, s);
        return block;
    }

    Node* parse_statement() {
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
                loop->text = label.text;
            }
            return loop;
        }
        return parse_simple_stmt();
    }

    Node* parse_simple_stmt() {
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
            expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
            n->span = span_from(start);
            return n;
        }
        if (at(TokenKind::KwBreak) || at(TokenKind::KwContinue)) {
            Token t = take();
            Node* n = make(t.kind == TokenKind::KwBreak ? NodeKind::Break : NodeKind::Continue,
                           start.span);
            if (at(TokenKind::Name)) {
                n->text = take().text;
            }
            expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
            return n;
        }
        if (at(TokenKind::KwDefer) || at(TokenKind::KwErrdefer)) {
            Token t = take();
            Node* n = make(t.kind == TokenKind::KwDefer ? NodeKind::Defer : NodeKind::Errdefer,
                           start.span);
            n->left = parse_expression();
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
            expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
            return n;
        }
        if (at(TokenKind::KwFree)) {
            take();
            Node* n = make(NodeKind::Free, start.span);
            expect(TokenKind::LParen, "lucb.parse.expect", "expected `(`");
            n->left = parse_expression();
            expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
            if (eat(TokenKind::KwIn)) {
                n->right = parse_expression();
            }
            expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
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
            expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
            n->span = span_from(start);
            return n;
        }
        Node* expr = finish_expression(left);
        Node* n = make(NodeKind::ExprStmt, start.span);
        n->left = expr;
        expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
        n->span = span_from(start);
        return n;
    }

    Node* parse_binding() {
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
        expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
        n->span = span_from(start);
        return n;
    }

    Node* parse_condition() {
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

    Node* parse_if() {
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

    Node* parse_while() {
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

    Node* parse_for() {
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

    Node* parse_with() {
        Token start = take();
        Node* n = make(NodeKind::With, start.span);
        n->left = parse_expression();
        expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
        n->body = parse_suite();
        n->span = span_from(start);
        return n;
    }

    Node* parse_match(bool as_expr) {
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
        }
        expect(TokenKind::Dedent, "lucb.parse.expect", "expected a dedent");
        n->body = arms;
        n->span = span_from(start);
        return n;
    }

    Node* parse_pattern() {
        Token start = cur();
        Node* n = make(NodeKind::Pattern, start.span);
        if (eat(TokenKind::Underscore)) {
            n->text = "_";
            n->span = start.span;
            return n;
        }
        if (eat(TokenKind::Dot)) {
            if (at(TokenKind::Name) || at(TokenKind::KwNone)) {
                n->text = take().text;
            } else if (at_name("some") || at(TokenKind::Name)) {
                n->text = take().text;
            } else {
                fail("lucb.parse.expect", "expected a case name");
            }
            if (at(TokenKind::LParen)) {
                n->body = parse_params(false);
            }
            n->span = span_from(start);
            return n;
        }
        if (eat(TokenKind::Minus)) {
            n->op = TokenKind::Minus;
        }
        if (is_literal_kind(cur().kind) || at(TokenKind::IntLit) || at(TokenKind::FloatLit) ||
            at(TokenKind::CharLit) || at(TokenKind::StringLit)) {
            n->left = parse_literal();
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

    Node* parse_expression() {
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

    Node* finish_expression(Node* unary) {
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

    Node* parse_else_expr() {
        Node* n = parse_conditional();
        return parse_else_rest(n);
    }

    Node* parse_else_rest(Node* n) {
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

    Node* parse_conditional() {
        Node* n = parse_binary(1);
        return parse_conditional_rest(n);
    }

    Node* parse_conditional_rest(Node* n) {
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

    Node* parse_binary(int min_prec) {
        Node* left = parse_unary();
        return parse_binary_rest(left, min_prec);
    }

    Node* parse_binary_rest(Node* left, int min_prec) {
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

    Node* parse_unary() {
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

    Node* parse_postfix() {
        Token start = cur();
        Node* value = parse_primary();
        while (true) {
            if (eat(TokenKind::Dot)) {
                Node* m = make(NodeKind::Member, start.span);
                m->left = value;
                if (!at(TokenKind::Name) && !at(TokenKind::KwNone)) {
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

    Node* expr_as_type(Node* value) {
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

    Node* parse_subscript(Node* value, Token start) {
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

    Node* parse_arg_list() {
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

    Node* parse_type_args() {
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

    Node* parse_primary() {
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

    Node* parse_literal() {
        Token t = take();
        Node* n = make(NodeKind::Literal, t.span);
        n->text = t.text;
        n->op = t.kind;
        return n;
    }

    Node* parse_formatted() {
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

    Node* parse_lambda() {
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

    Node* parse_group_or_tuple() {
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

    Node* parse_array_lit() {
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

    Node* parse_new_or_alloc(bool is_alloc) {
        Token start = take();
        Node* n = make(is_alloc ? NodeKind::Alloc : NodeKind::New, start.span);
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
        if (eat(TokenKind::KwIn)) {
            n->right = parse_expression();
        }
        n->span = span_from(start);
        return n;
    }

    Node* parse_type() {
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

    Node* parse_primary_type() {
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
                return first;
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
};

} // namespace

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
