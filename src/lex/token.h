//==============================================================================================
//
//   lex/token - Tokens
//
//   DESCRIPTION:
//       A token is a kind and a span into the source; literals keep their spelling for the
//       literal decoder.
//
//==============================================================================================

#pragma once

#include "source/source.h"

namespace lucb {

enum class TokenKind : uint16_t {
    EndOfFile,
    Newline,
    Indent,
    Dedent,
    RawLine, // one physical line of an `asm` suite, baseline indent removed

    Name,
    IntLit,
    FloatLit,
    CharLit,
    StringLit,
    BytesLit,
    FormatStart,
    FormatText,
    FormatEnd,
    DocComment,
    Underscore,

    // Keywords, base.md §3.6, plus class/spawn/weak so they cannot be names.
    KwAlloc,
    KwAnd,
    KwAs,
    KwAsm,
    KwBreak,
    KwCatch,
    KwClass,
    KwConst,
    KwContinue,
    KwDefer,
    KwElif,
    KwElse,
    KwEnum,
    KwErrdefer,
    KwExport,
    KwExtern,
    KwFalse,
    KwFor,
    KwFree,
    KwFrom,
    KwFunc,
    KwGoto,
    KwIf,
    KwImport,
    KwIn,
    KwInterface,
    KwLet,
    KwMatch,
    KwMutating,
    KwNew,
    KwNone,
    KwNot,
    KwOr,
    KwPub,
    KwRecover,
    KwReturn,
    KwSelf,
    KwSpawn,
    KwStatic,
    KwStruct,
    KwTest,
    KwThreadLocal,
    KwTrue,
    KwTry,
    KwType,
    KwUnion,
    KwVar,
    KwVolatile,
    KwWhile,
    KwWith,

    LParen,
    RParen,
    LBracket,
    RBracket,
    LBrace,
    RBrace,
    Comma,
    Colon,
    Dot,
    Question,
    Bang,
    At,

    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Amp,
    Pipe,
    Caret,
    Tilde,
    Lt,
    Gt,
    Eq,

    Arrow,
    FatArrow,
    EqEq,
    NotEq,
    LtEq,
    GtEq,
    SlashSlash,
    DotDot,
    DotDotLt,
    DotDotEq,
    LtLt,
    GtGt,

    PlusEq,
    MinusEq,
    StarEq,
    SlashEq,
    SlashSlashEq,
    PercentEq,
    AmpEq,
    PipeEq,
    CaretEq,
    LtLtEq,
    GtGtEq,

    PlusPercent,
    MinusPercent,
    StarPercent,
    PlusPipe,
    MinusPipe,
    StarPipe,
    PlusQuestion,
    MinusQuestion,
    StarQuestion,

    PlusPercentEq,
    MinusPercentEq,
    StarPercentEq,
    PlusPipeEq,
    MinusPipeEq,
    StarPipeEq,

    DashDashDash,
    DotDotDot,
};

struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    Span span;
    string_view text;
    string_view suffix;
};

const char* token_kind_name(TokenKind kind);
TokenKind keyword_kind(string_view word);
bool is_keyword(TokenKind kind);

} // namespace lucb
