//==============================================================================================
//
//   parse/ast - Syntax tree printing
//
//   DESCRIPTION:
//       The s-expression dump behind `lucb dump`, and the node-kind names used by the parser
//       tests.
//
//==============================================================================================

#include "parse/ast.h"

namespace lucb {
namespace {

void dump(const Node* n, string& out);

void dump_list(const Node* n, string& out) {
    for (const Node* p = n; p != nullptr; p = p->next) {
        out += ' ';
        dump(p, out);
    }
}

void dump_flags(const Node* n, string& out) {
    if (n->flags & FlagPub) {
        out += " pub";
    }
    if (n->flags & FlagExport) {
        out += " export";
    }
    if (n->flags & FlagStatic) {
        out += " static";
    }
    if (n->flags & FlagMutating) {
        out += " mutating";
    }
    if (n->flags & FlagThreadLocal) {
        out += " thread_local";
    }
    if (n->flags & FlagPacked) {
        out += " packed";
    }
    if (n->flags & FlagUninit) {
        out += " uninit";
    }
    if (n->flags & FlagByPtr) {
        out += " byptr";
    }
    if (n->flags & FlagIfLet) {
        out += " iflet";
    }
    if (n->flags & FlagVariadic) {
        out += " variadic";
    }
    if (n->flags & FlagBlocking) {
        out += " blocking";
    }
}

void quote(string_view text, string& out) {
    out += '"';
    out.append(text.data(), text.size());
    out += '"';
}

void dump(const Node* n, string& out) {
    if (n == nullptr) {
        out += "nil";
        return;
    }
    out += '(';
    out += node_kind_name(n->kind);
    dump_flags(n, out);
    if (!n->text.empty()) {
        out += ' ';
        quote(n->text, out);
    }
    if (n->op != TokenKind::EndOfFile && n->kind != NodeKind::Literal) {
        out += ' ';
        out += token_kind_name(n->op);
    }
    if (n->kind == NodeKind::Literal) {
        out += ' ';
        out += token_kind_name(n->op);
    }

    switch (n->kind) {
    case NodeKind::Module:
    case NodeKind::Block:
    case NodeKind::Tuple:
    case NodeKind::ArrayLit:
    case NodeKind::Formatted:
        dump_list(n->body, out);
        break;
    case NodeKind::Func:
    case NodeKind::ExternFunc:
        if (n->left != nullptr) {
            out += " (generics";
            dump_list(n->left, out);
            out += ')';
        }
        out += " (params";
        dump_list(n->right, out);
        out += ')';
        if (n->type != nullptr) {
            out += " (result ";
            dump(n->type, out);
            out += ')';
        }
        if (n->body != nullptr) {
            out += ' ';
            dump(n->body, out);
        }
        break;
    case NodeKind::Lambda:
        out += " (params";
        dump_list(n->right, out);
        out += ')';
        if (n->body != nullptr) {
            out += ' ';
            dump(n->body, out);
        }
        break;
    case NodeKind::Struct:
    case NodeKind::Enum:
    case NodeKind::Union:
    case NodeKind::Interface:
        if (n->left != nullptr) {
            out += " (generics";
            dump_list(n->left, out);
            out += ')';
        }
        if (n->right != nullptr) {
            out += " (extra ";
            dump(n->right, out);
            out += ')';
        }
        dump_list(n->body, out);
        break;
    default:
        if (n->type != nullptr) {
            out += " (type ";
            dump(n->type, out);
            out += ')';
        }
        if (n->left != nullptr) {
            out += ' ';
            dump(n->left, out);
        }
        if (n->right != nullptr) {
            out += ' ';
            dump(n->right, out);
        }
        if (n->body != nullptr) {
            if (n->body->next != nullptr || n->kind == NodeKind::Call ||
                n->kind == NodeKind::FromImport || n->kind == NodeKind::Match) {
                out += " (list";
                dump_list(n->body, out);
                out += ')';
            } else {
                out += ' ';
                dump(n->body, out);
            }
        }
        break;
    }
    out += ')';
}

} // namespace

// A small cache of list tails keeps appending O(1) for the long sibling
// lists (a module's declarations, a function's statements) that the parser
// builds one item at a time; a miss just walks from the head as before.
struct TailEntry {
    Node** list = nullptr;
    Node* head = nullptr;
    Node* tail = nullptr;
};
static TailEntry tail_cache[64];

void append_node(Node** list, Node* item) {
    if (item == nullptr) {
        return;
    }
    item->next = nullptr;
    if (*list == nullptr) {
        *list = item;
        return;
    }
    TailEntry& entry = tail_cache[(reinterpret_cast<uintptr_t>(list) >> 3) %
                                  (sizeof(tail_cache) / sizeof(tail_cache[0]))];
    Node* p = *list;
    if (entry.list == list && entry.head == p && entry.tail != nullptr) {
        p = entry.tail; // still a member of this list; walk on from it
    }
    while (p->next != nullptr) {
        p = p->next;
    }
    entry.list = list;
    entry.head = *list;
    entry.tail = item;
    p->next = item;
}

int node_list_count(const Node* list) {
    int n = 0;
    for (const Node* p = list; p != nullptr; p = p->next) {
        n++;
    }
    return n;
}

string dump_tree(const Node* node) {
    string out;
    dump(node, out);
    return out;
}

const char* node_kind_name(NodeKind kind) {
    switch (kind) {
    case NodeKind::Module:
        return "module";
    case NodeKind::Import:
        return "import";
    case NodeKind::FromImport:
        return "from";
    case NodeKind::Func:
        return "func";
    case NodeKind::Struct:
        return "struct";
    case NodeKind::Enum:
        return "enum";
    case NodeKind::Union:
        return "union";
    case NodeKind::Interface:
        return "interface";
    case NodeKind::TypeAlias:
        return "alias";
    case NodeKind::Const:
        return "const";
    case NodeKind::Global:
        return "global";
    case NodeKind::Test:
        return "test";
    case NodeKind::ExternFunc:
        return "extern_func";
    case NodeKind::ExternType:
        return "extern_type";
    case NodeKind::ExternVar:
        return "extern_var";
    case NodeKind::ExternStruct:
        return "extern_struct";
    case NodeKind::ExternUnion:
        return "extern_union";
    case NodeKind::Assert:
        return "assert";
    case NodeKind::Asm:
        return "asm";
    case NodeKind::Field:
        return "field";
    case NodeKind::EnumCase:
        return "case";
    case NodeKind::Param:
        return "param";
    case NodeKind::GenericParam:
        return "generic";
    case NodeKind::Attr:
        return "attr";
    case NodeKind::Block:
        return "block";
    case NodeKind::Let:
        return "let";
    case NodeKind::Var:
        return "var";
    case NodeKind::Assign:
        return "assign";
    case NodeKind::If:
        return "if";
    case NodeKind::While:
        return "while";
    case NodeKind::For:
        return "for";
    case NodeKind::Match:
        return "match";
    case NodeKind::MatchArm:
        return "arm";
    case NodeKind::Return:
        return "return";
    case NodeKind::Break:
        return "break";
    case NodeKind::Continue:
        return "continue";
    case NodeKind::Defer:
        return "defer";
    case NodeKind::Errdefer:
        return "errdefer";
    case NodeKind::Recover:
        return "recover";
    case NodeKind::Free:
        return "free";
    case NodeKind::With:
        return "with";
    case NodeKind::ExprStmt:
        return "expr";
    case NodeKind::Name:
        return "name";
    case NodeKind::Self:
        return "self";
    case NodeKind::Literal:
        return "lit";
    case NodeKind::Unary:
        return "unary";
    case NodeKind::Binary:
        return "binary";
    case NodeKind::Call:
        return "call";
    case NodeKind::Member:
        return "member";
    case NodeKind::Index:
        return "index";
    case NodeKind::Slice:
        return "slice";
    case NodeKind::Tuple:
        return "tuple";
    case NodeKind::ArrayLit:
        return "array";
    case NodeKind::Group:
        return "group";
    case NodeKind::Lambda:
        return "lambda";
    case NodeKind::Conditional:
        return "cond";
    case NodeKind::Else:
        return "else";
    case NodeKind::Catch:
        return "catch";
    case NodeKind::Cast:
        return "cast";
    case NodeKind::CaseValue:
        return "caseval";
    case NodeKind::Formatted:
        return "fmt";
    case NodeKind::FormatText:
        return "fmt_text";
    case NodeKind::FormatField:
        return "fmt_field";
    case NodeKind::New:
        return "new";
    case NodeKind::Alloc:
        return "alloc";
    case NodeKind::SpanMake:
        return "span";
    case NodeKind::MatchExpr:
        return "match_expr";
    case NodeKind::Unit:
        return "unit";
    case NodeKind::Type:
        return "type";
    case NodeKind::Pattern:
        return "pat";
    }
    return "???";
}

} // namespace lucb
