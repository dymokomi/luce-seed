//==============================================================================================
//
//   parse/decl - Declarations
//
//   DESCRIPTION:
//       Modules, imports, functions with attributes, structs, enums, unions, interfaces and
//       conformance, type aliases, globals, `extern`/`export`, and `test` declarations
//       (base.md §9, §10, §14, §16, §17, §21).
//
//==============================================================================================

#include "parse/parser_impl.h"

namespace lucb {

auto Parser::parse_module() -> Node* {
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

auto Parser::parse_import() -> Node* {
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

auto Parser::parse_module_path() -> string_view {
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

// `weak` before a function or a variable is the attribute of §9.8; elsewhere it is full
// Luce's field marker. Look past the other attribute words to the declaration.
auto Parser::weak_is_attribute() -> bool {
    int i = 1;
    while (true) {
        const Token& t = peek(i);
        if (t.kind == TokenKind::KwWeak) {
            i += 1;
        } else if (t.kind == TokenKind::Name && t.text == "section") {
            i += 4; // section ( "name" )
        } else if (t.kind == TokenKind::Name &&
                   (t.text == "inline" || t.text == "noinline" || t.text == "cold" ||
                    t.text == "naked" || t.text == "used")) {
            i += 1;
        } else {
            return t.kind == TokenKind::KwFunc || t.kind == TokenKind::KwStatic ||
                   t.kind == TokenKind::KwMutating || t.kind == TokenKind::KwVar ||
                   t.kind == TokenKind::KwThreadLocal;
        }
    }
}

// Attach the attribute list parsed ahead of a declaration to the declaration.
static auto with_attrs(Node* n, Node* attrs) -> Node* {
    if (n != nullptr && n->attrs == nullptr) {
        n->attrs = attrs;
    }
    return n;
}

auto Parser::parse_attributes(Node** attrs) -> uint32_t {
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

auto Parser::parse_top() -> Node* {
    if (at(TokenKind::KwClass) || at(TokenKind::KwSpawn) ||
        (at(TokenKind::KwWeak) && !weak_is_attribute())) {
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
        return with_attrs(parse_global(flags), attrs);
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
        return with_attrs(parse_func(flags), attrs);
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

auto Parser::parse_const(uint32_t flags) -> Node* {
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
    expect_stmt_newline(n->left);
    n->span = span_from(start);
    return n;
}

auto Parser::parse_global(uint32_t flags) -> Node* {
    Token start = cur();
    Node* n = make(NodeKind::Global, start.span);
    if (eat(TokenKind::KwThreadLocal)) {
        flags |= FlagThreadLocal;
    }
    Node* attrs = nullptr;
    flags |= parse_attributes(&attrs);
    n->attrs = attrs;
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
    if (n->left != nullptr) {
        expect_stmt_newline(n->left);
    } else {
        expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
    }
    n->span = span_from(start);
    return n;
}

auto Parser::parse_type_alias(uint32_t flags) -> Node* {
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

auto Parser::parse_func(uint32_t flags) -> Node* {
    Token start = cur();
    Node* attrs = nullptr;
    flags |= parse_attributes(&attrs);
    if (eat(TokenKind::KwStatic)) {
        flags |= FlagStatic;
    }
    if (eat(TokenKind::KwMutating)) {
        flags |= FlagMutating;
    }
    if ((flags & FlagStatic) != 0 && (flags & FlagMutating) != 0) {
        fail("lucb.parse.expect", "a static method cannot be `mutating`");
    }
    expect(TokenKind::KwFunc, "lucb.parse.expect", "expected `func`");
    Node* n = make(NodeKind::Func, start.span);
    n->attrs = attrs;
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

auto Parser::parse_generic_params() -> Node* {
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

auto Parser::parse_params(bool extern_form) -> Node* {
    expect(TokenKind::LParen, "lucb.parse.expect", "expected `(`");
    Node* list = nullptr;
    if (!at(TokenKind::RParen)) {
        while (true) {
            if (eat(TokenKind::DotDotDot)) {
                if (!extern_form) {
                    fail("lucb.parse.expect", "a Base function cannot be variadic");
                }
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
                p->left = parse_expression();
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

auto Parser::parse_struct(uint32_t flags, bool is_extern) -> Node* {
    Token start = cur();
    Node* align_e = nullptr;
    if (at_name("packed")) {
        take();
        flags |= FlagPacked;
    } else if (at_name("align")) {
        take();
        expect(TokenKind::LParen, "lucb.parse.expect", "expected `(`");
        align_e = parse_expression();
        expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
    }
    expect(TokenKind::KwStruct, "lucb.parse.expect", "expected `struct`");
    Node* n = make(is_extern ? NodeKind::ExternStruct : NodeKind::Struct, start.span);
    n->flags = flags;
    n->type = align_e;
    if (!at(TokenKind::Name)) {
        fail("lucb.parse.expect", "expected a type name");
    } else {
        n->text = take().text;
    }
    if (!is_extern && at(TokenKind::LBracket) && !is_array_suffix_ahead()) {
        n->left = parse_generic_params();
    }
    if (!is_extern) {
        n->right = parse_conformance();
    } else {
        expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
    }
    expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
    expect(TokenKind::Indent, "lucb.parse.expect", "expected an indented body");
    while (!at(TokenKind::Dedent) && !at(TokenKind::EndOfFile)) {
        skip_docs();
        if (at(TokenKind::Dedent)) {
            break;
        }
        int here = pos;
        append_node(&n->body, parse_type_member(is_extern));
        if (pos == here) {
            take();
        }
    }
    expect(TokenKind::Dedent, "lucb.parse.expect", "expected a dedent");
    n->span = span_from(start);
    return n;
}

// `struct Name: A, B:` declares that the type implements `A` and `B` (§14.1). The first
// colon opens either the interface list or, when a newline follows it, the body; a list
// ends with the body's own colon. Both colons are consumed here.
auto Parser::parse_conformance() -> Node* {
    expect(TokenKind::Colon, "lucb.parse.expect", "expected `:`");
    if (at(TokenKind::Newline)) {
        return nullptr;
    }
    Node* list = nullptr;
    append_node(&list, parse_type());
    while (eat(TokenKind::Comma)) {
        append_node(&list, parse_type());
    }
    expect(TokenKind::Colon, "lucb.parse.expect", "expected `:` before the body");
    return list;
}

auto Parser::parse_type_member(bool is_extern) -> Node* {
    uint32_t flags = 0;
    if (eat(TokenKind::KwPub)) {
        flags |= FlagPub;
    }
    if (eat(TokenKind::KwExport)) {
        flags |= FlagExport;
    }
    if (at(TokenKind::KwWeak) &&
        (peek(1).kind == TokenKind::KwVar || peek(1).kind == TokenKind::KwLet)) {
        // `weak` on a field is full Luce's reference-counting; Base has none (§3.6)
        fail("lucb.parse.tier", "this construct belongs to full Luce");
        take();
    }
    Node* align_e = nullptr;
    if (at_name("align")) {
        take();
        expect(TokenKind::LParen, "lucb.parse.expect", "expected `(`");
        align_e = parse_expression();
        expect(TokenKind::RParen, "lucb.parse.expect", "expected `)`");
    }
    if (at(TokenKind::KwLet) || at(TokenKind::KwVar) || (is_extern && at(TokenKind::Name))) {
        Token start = cur();
        Node* f = make(NodeKind::Field, start.span);
        f->flags = flags;
        f->right = align_e;
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
    if (at(TokenKind::KwFunc) || at(TokenKind::KwStatic) || at(TokenKind::KwMutating) ||
        at_name("inline")) {
        return parse_func(flags);
    }
    fail("lucb.parse.expect", "expected a field or method");
    sync_line();
    return make(NodeKind::Field, cur().span);
}

auto Parser::parse_enum(uint32_t flags) -> Node* {
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
    Node* impl = parse_conformance();
    if (impl != nullptr) {
        // the interfaces follow the backing type on the same chain
        if (n->right != nullptr) {
            n->right->next = impl;
        } else {
            n->right = impl;
        }
    }
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
        if (at(TokenKind::KwStatic) || at(TokenKind::KwMutating) ||
            (at(TokenKind::KwFunc) && peek(1).kind == TokenKind::Name)) {
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

auto Parser::parse_union(uint32_t flags) -> Node* {
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

auto Parser::parse_interface(uint32_t flags) -> Node* {
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
        int here = pos;
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
        if (pos == here) {
            take();
        }
    }
    expect(TokenKind::Dedent, "lucb.parse.expect", "expected a dedent");
    n->span = span_from(start);
    return n;
}

auto Parser::parse_extern(uint32_t flags) -> Node* {
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

auto Parser::parse_test() -> Node* {
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

auto Parser::parse_assert() -> Node* {
    Token start = take();
    Node* n = make(NodeKind::Assert, start.span);
    n->body = parse_arg_list();
    expect(TokenKind::Newline, "lucb.parse.expect", "expected newline");
    n->span = span_from(start);
    return n;
}

auto Parser::parse_asm() -> Node* {
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
                if (at(TokenKind::KwIn) || at_name("in") || at_name("out") || at_name("inout") ||
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

} // namespace lucb
