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

enum : uint64_t {
    FlagPub = 1ull << 0,
    FlagMutating = 1ull << 1,
    FlagStatic = 1ull << 2,
    FlagExport = 1ull << 3,
    FlagPacked = 1ull << 4,
    FlagThreadLocal = 1ull << 5,
    FlagUninit = 1ull << 6,
    FlagConst = 1ull << 7,
    FlagVolatile = 1ull << 8,
    FlagAtomic = 1ull << 9,
    FlagOptional = 1ull << 10,
    FlagFallible = 1ull << 11,
    FlagByPtr = 1ull << 12,
    FlagNoalias = 1ull << 13,
    FlagOut = 1ull << 14,
    FlagBlocking = 1ull << 15,
    FlagVariadic = 1ull << 16,
    FlagStar = 1ull << 17,  // type: pointer suffix
    FlagSpan = 1ull << 18,  // type: T[]
    FlagArray = 1ull << 19, // type: T[N]
    FlagFuncType = 1ull << 20,
    FlagVoid = 1ull << 21,
    FlagTupleType = 1ull << 22,
    FlagIfLet = 1ull << 23,
    FlagInline = 1ull << 24,
    FlagNoinline = 1ull << 25,
    FlagCold = 1ull << 26,
    FlagNaked = 1ull << 27,
    FlagUsed = 1ull << 28,
    FlagWeakAttr = 1ull << 29,
    FlagBuiltin = 1ull << 30,       // synthesized by the checker for a standard module
    FlagLiteralCached = 1ull << 31, // `cached` holds the decoded integer literal
    FlagLocal = 1ull << 30,         // pointer/span/str derived from a local
    FlagImportUsed = 1ull << 31,
    FlagUnused = 1ull << 32,     // a local no expression read: pruned by the checker
    FlagReferenced = 1ull << 33, // a function some name resolved to; an unreferenced private one is pruned
    FlagFormatSink = 1ull << 34, // the sink of the formatted string being built (a name), the call that
                                 // displays a field through it, and the field itself (§14.4)
    FlagPackageCode = 1ull << 35, // `cached` holds an `ErrorCode.package` value with its package identity
};

struct Node {
    NodeKind kind = NodeKind::Name;
    Span span;
    string_view text;
    TokenKind op = TokenKind::EndOfFile;
    uint64_t flags = 0;
    Node* left = nullptr;
    Node* right = nullptr;
    Node* body = nullptr;
    Node* type = nullptr;
    Node* next = nullptr;
    string_view label;      // a loop's label: `outer: for ...` (§8.5)
    Node* attrs = nullptr;  // declaration attributes with an argument, `section("...")` (§9.8)
    // Filled by check:
    struct Type* ty = nullptr; // resolved type of this node
    Node* resolved = nullptr;  // declaration a name/call refers to
    string_view module;        // for a top-level declaration of an imported module: the
                               // module's name, which qualifies its C symbols (§16.3)
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
//                      right = conformance list / enum-as-type, body = members
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
