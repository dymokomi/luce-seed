//==============================================================================================
//
//   parse/ast - The untyped syntax tree
//
//   DESCRIPTION:
//       Arena-allocated nodes with sibling lists. The checker records resolved types and
//       declarations on the same nodes, so there is one tree from parsing through emission.
//
//==============================================================================================

#pragma once

#include "lex/token.h"
#include "support/arena.h"

namespace lucb {

enum class NodeKind : uint16_t {
    Module,

    Import,
    FromImport,
    Func,
    Struct,
    Enum,
    Union,
    Interface,
    TypeAlias,
    Const,
    Global,
    Test,
    ExternFunc,
    ExternType,
    ExternVar,
    ExternStruct,
    ExternUnion,
    Assert,
    Asm,
    Field,
    EnumCase,
    Param,
    GenericParam,
    Attr,

    Block,
    Let,
    Var,
    Assign,
    If,
    While,
    For,
    Match,
    MatchArm,
    Return,
    Break,
    Continue,
    Defer,
    Errdefer,
    Recover,
    Free,
    With,
    ExprStmt,

    Name,
    Self,
    Literal,
    Unary,
    Binary,
    Call,
    Member,
    Index,
    Slice,
    Tuple,
    ArrayLit,
    Group,
    Lambda,
    Conditional,
    Else,
    Catch,
    Cast,
    CaseValue,
    Formatted,
    FormatText,
    FormatField,
    New,
    Alloc,
    SpanMake,
    MatchExpr,
    Unit,

    Type,
    Pattern,
};

enum {
    FlagPub = 1u << 0,
    FlagMutating = 1u << 1,
    FlagStatic = 1u << 2,
    FlagExport = 1u << 3,
    FlagPacked = 1u << 4,
    FlagThreadLocal = 1u << 5,
    FlagUninit = 1u << 6,
    FlagConst = 1u << 7,
    FlagVolatile = 1u << 8,
    FlagAtomic = 1u << 9,
    FlagOptional = 1u << 10,
    FlagFallible = 1u << 11,
    FlagByPtr = 1u << 12,
    FlagNoalias = 1u << 13,
    FlagOut = 1u << 14,
    FlagBlocking = 1u << 15,
    FlagVariadic = 1u << 16,
    FlagStar = 1u << 17,  // type: pointer suffix
    FlagSpan = 1u << 18,  // type: T[]
    FlagArray = 1u << 19, // type: T[N]
    FlagFuncType = 1u << 20,
    FlagVoid = 1u << 21,
    FlagTupleType = 1u << 22,
    FlagIfLet = 1u << 23,
    FlagInline = 1u << 24,
    FlagNoinline = 1u << 25,
    FlagCold = 1u << 26,
    FlagNaked = 1u << 27,
    FlagUsed = 1u << 28,
    FlagWeakAttr = 1u << 29,
    FlagBuiltin = 1u << 30,       // synthesized by the checker for a standard module
    FlagLiteralCached = 1u << 31, // `cached` holds the decoded integer literal
    FlagLocal = 1u << 30,         // pointer/span/str derived from a local
    FlagImportUsed = 1u << 31,
};

struct Node {
    NodeKind kind = NodeKind::Name;
    Span span;
    string_view text;
    TokenKind op = TokenKind::EndOfFile;
    uint32_t flags = 0;
    Node* left = nullptr;
    Node* right = nullptr;
    Node* body = nullptr;
    Node* type = nullptr;
    Node* next = nullptr;
    // Filled by check:
    struct Type* ty = nullptr; // resolved type of this node
    Node* resolved = nullptr;  // declaration a name/call refers to
    // Filled by the oracle the first time a literal is evaluated:
    uint64_t cached = 0;
};

// Field map, by kind:
//   Module:            body = decls
//   Import:            text = path, left = alias
//   FromImport:        text = path, body = names
//   Func:              text = name, left = generics, right = params,
//                      type = result, body = suite
//   Struct/Enum/Union/Interface: text = name, left = generics,
//                      right = implements / enum-as-type, body = members
//   Field/Param:       text = name, type = type, left = default
//   EnumCase:          text = name, body = payload params, left = value
//   TypeAlias/Const/Global: text = name, type = type, left = value
//   Test:              text = title, body = suite
//   ExternFunc:        text = name, left = as-string, right = params, type = result
//   Attr:              text = name, left = argument (section string)
//   Block:             body = statements
//   Let/Var:           text = name, type = type, left = value; Var FlagUninit
//   Assign:            left = lvalue, right = value, op = ASSIGN_OP
//   If/While:          left = cond, body = then, right = else; FlagIfLet
//   For:               text = name, type = type, left = second name (pairs),
//                      right = iter, body = suite; FlagByPtr
//   Match:             left = expr, body = arms
//   MatchArm:          left = patterns, type = guard, body = suite or expr
//   Return/Recover:    left = value
//   Break/Continue:    text = label
//   Defer/Errdefer:    left = call, body = catch suite, text = catch name
//   Free:              left = value, right = allocator
//   With:              left = allocator, body = suite
//   ExprStmt:          left = expr
//   Name/Self/Literal: text = spelling; Literal.op = token kind
//   Unary:             op, left = operand
//   Binary:            op, left, right
//   Call:              left = callee, body = args, type = type-args
//   Member:            left = value, text = name
//   Index:             left = value, body = index
//   Slice:             left = value, body = start, right = end
//   Tuple/ArrayLit:    body = elements
//   Group:             left = inner
//   Lambda:            right = params, body = expr
//   Conditional:       left = value, type = cond, right = alternative
//   Else:              left = value, right = fallback
//   Catch:             left = value, text = name, body = suite
//   Cast:              type = target, left = value
//   CaseValue:         text = name, body = args
//   Formatted:         body = parts (FormatText / FormatField)
//   New/Alloc:         type = allocated type, body = args / count, right = `in`
//   SpanMake:          type = element, body = args
//   Type:              text = name, left = inner, body = args, right = length
//   Pattern:           text / op / left / right / body as for match patterns
//   Param (call arg):  text = name if named, left = value
//   Asm:               text = arch, left = operands, body = raw lines

const char* node_kind_name(NodeKind kind);

void append_node(Node** list, Node* item);
int node_list_count(const Node* list);

// One-line s-expression. Tests pin this spelling.
string dump_tree(const Node* node);

} // namespace lucb
