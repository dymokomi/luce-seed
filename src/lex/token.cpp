#include "lex/token.h"

#include <cstring>

namespace lucb {
namespace {

struct KeywordEntry {
    const char* spelling;
    TokenKind kind;
};

// Sorted by spelling for bsearch.
constexpr KeywordEntry k_keywords[] = {
    {"alloc", TokenKind::KwAlloc},
    {"and", TokenKind::KwAnd},
    {"as", TokenKind::KwAs},
    {"asm", TokenKind::KwAsm},
    {"break", TokenKind::KwBreak},
    {"catch", TokenKind::KwCatch},
    {"class", TokenKind::KwClass},
    {"const", TokenKind::KwConst},
    {"continue", TokenKind::KwContinue},
    {"defer", TokenKind::KwDefer},
    {"elif", TokenKind::KwElif},
    {"else", TokenKind::KwElse},
    {"enum", TokenKind::KwEnum},
    {"errdefer", TokenKind::KwErrdefer},
    {"export", TokenKind::KwExport},
    {"extern", TokenKind::KwExtern},
    {"false", TokenKind::KwFalse},
    {"for", TokenKind::KwFor},
    {"free", TokenKind::KwFree},
    {"from", TokenKind::KwFrom},
    {"func", TokenKind::KwFunc},
    {"goto", TokenKind::KwGoto},
    {"if", TokenKind::KwIf},
    {"implements", TokenKind::KwImplements},
    {"import", TokenKind::KwImport},
    {"in", TokenKind::KwIn},
    {"interface", TokenKind::KwInterface},
    {"let", TokenKind::KwLet},
    {"match", TokenKind::KwMatch},
    {"mutating", TokenKind::KwMutating},
    {"new", TokenKind::KwNew},
    {"none", TokenKind::KwNone},
    {"not", TokenKind::KwNot},
    {"or", TokenKind::KwOr},
    {"pub", TokenKind::KwPub},
    {"recover", TokenKind::KwRecover},
    {"return", TokenKind::KwReturn},
    {"self", TokenKind::KwSelf},
    {"spawn", TokenKind::KwSpawn},
    {"static", TokenKind::KwStatic},
    {"struct", TokenKind::KwStruct},
    {"test", TokenKind::KwTest},
    {"thread_local", TokenKind::KwThreadLocal},
    {"true", TokenKind::KwTrue},
    {"try", TokenKind::KwTry},
    {"type", TokenKind::KwType},
    {"union", TokenKind::KwUnion},
    {"var", TokenKind::KwVar},
    {"volatile", TokenKind::KwVolatile},
    {"weak", TokenKind::KwWeak},
    {"while", TokenKind::KwWhile},
    {"with", TokenKind::KwWith},
};

} // namespace

TokenKind keyword_kind(std::string_view word) {
    size_t lo = 0;
    size_t hi = sizeof(k_keywords) / sizeof(k_keywords[0]);
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        const int cmp = word.compare(k_keywords[mid].spelling);
        if (cmp == 0) {
            return k_keywords[mid].kind;
        }
        if (cmp < 0) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return TokenKind::Name;
}

bool is_keyword(TokenKind kind) {
    return kind >= TokenKind::KwAlloc && kind <= TokenKind::KwWith;
}

const char* token_kind_name(TokenKind kind) {
    switch (kind) {
    case TokenKind::EndOfFile:
        return "eof";
    case TokenKind::Newline:
        return "newline";
    case TokenKind::Indent:
        return "indent";
    case TokenKind::Dedent:
        return "dedent";
    case TokenKind::RawLine:
        return "raw";
    case TokenKind::Name:
        return "name";
    case TokenKind::IntLit:
        return "int";
    case TokenKind::FloatLit:
        return "float";
    case TokenKind::CharLit:
        return "char";
    case TokenKind::StringLit:
        return "string";
    case TokenKind::BytesLit:
        return "bytes";
    case TokenKind::FormatStart:
        return "format_start";
    case TokenKind::FormatText:
        return "format_text";
    case TokenKind::FormatEnd:
        return "format_end";
    case TokenKind::DocComment:
        return "doc";
    case TokenKind::Underscore:
        return "_";
    case TokenKind::KwAlloc:
        return "alloc";
    case TokenKind::KwAnd:
        return "and";
    case TokenKind::KwAs:
        return "as";
    case TokenKind::KwAsm:
        return "asm";
    case TokenKind::KwBreak:
        return "break";
    case TokenKind::KwCatch:
        return "catch";
    case TokenKind::KwClass:
        return "class";
    case TokenKind::KwConst:
        return "const";
    case TokenKind::KwContinue:
        return "continue";
    case TokenKind::KwDefer:
        return "defer";
    case TokenKind::KwElif:
        return "elif";
    case TokenKind::KwElse:
        return "else";
    case TokenKind::KwEnum:
        return "enum";
    case TokenKind::KwErrdefer:
        return "errdefer";
    case TokenKind::KwExport:
        return "export";
    case TokenKind::KwExtern:
        return "extern";
    case TokenKind::KwFalse:
        return "false";
    case TokenKind::KwFor:
        return "for";
    case TokenKind::KwFree:
        return "free";
    case TokenKind::KwFrom:
        return "from";
    case TokenKind::KwFunc:
        return "func";
    case TokenKind::KwGoto:
        return "goto";
    case TokenKind::KwIf:
        return "if";
    case TokenKind::KwImplements:
        return "implements";
    case TokenKind::KwImport:
        return "import";
    case TokenKind::KwIn:
        return "in";
    case TokenKind::KwInterface:
        return "interface";
    case TokenKind::KwLet:
        return "let";
    case TokenKind::KwMatch:
        return "match";
    case TokenKind::KwMutating:
        return "mutating";
    case TokenKind::KwNew:
        return "new";
    case TokenKind::KwNone:
        return "none";
    case TokenKind::KwNot:
        return "not";
    case TokenKind::KwOr:
        return "or";
    case TokenKind::KwPub:
        return "pub";
    case TokenKind::KwRecover:
        return "recover";
    case TokenKind::KwReturn:
        return "return";
    case TokenKind::KwSelf:
        return "self";
    case TokenKind::KwSpawn:
        return "spawn";
    case TokenKind::KwStatic:
        return "static";
    case TokenKind::KwStruct:
        return "struct";
    case TokenKind::KwTest:
        return "test";
    case TokenKind::KwThreadLocal:
        return "thread_local";
    case TokenKind::KwTrue:
        return "true";
    case TokenKind::KwTry:
        return "try";
    case TokenKind::KwType:
        return "type";
    case TokenKind::KwUnion:
        return "union";
    case TokenKind::KwVar:
        return "var";
    case TokenKind::KwVolatile:
        return "volatile";
    case TokenKind::KwWeak:
        return "weak";
    case TokenKind::KwWhile:
        return "while";
    case TokenKind::KwWith:
        return "with";
    case TokenKind::LParen:
        return "(";
    case TokenKind::RParen:
        return ")";
    case TokenKind::LBracket:
        return "[";
    case TokenKind::RBracket:
        return "]";
    case TokenKind::LBrace:
        return "{";
    case TokenKind::RBrace:
        return "}";
    case TokenKind::Comma:
        return ",";
    case TokenKind::Colon:
        return ":";
    case TokenKind::Dot:
        return ".";
    case TokenKind::Question:
        return "?";
    case TokenKind::Bang:
        return "!";
    case TokenKind::At:
        return "@";
    case TokenKind::Plus:
        return "+";
    case TokenKind::Minus:
        return "-";
    case TokenKind::Star:
        return "*";
    case TokenKind::Slash:
        return "/";
    case TokenKind::Percent:
        return "%";
    case TokenKind::Amp:
        return "&";
    case TokenKind::Pipe:
        return "|";
    case TokenKind::Caret:
        return "^";
    case TokenKind::Tilde:
        return "~";
    case TokenKind::Lt:
        return "<";
    case TokenKind::Gt:
        return ">";
    case TokenKind::Eq:
        return "=";
    case TokenKind::Arrow:
        return "->";
    case TokenKind::FatArrow:
        return "=>";
    case TokenKind::EqEq:
        return "==";
    case TokenKind::NotEq:
        return "!=";
    case TokenKind::LtEq:
        return "<=";
    case TokenKind::GtEq:
        return ">=";
    case TokenKind::SlashSlash:
        return "//";
    case TokenKind::DotDot:
        return "..";
    case TokenKind::DotDotLt:
        return "..<";
    case TokenKind::DotDotEq:
        return "..=";
    case TokenKind::LtLt:
        return "<<";
    case TokenKind::GtGt:
        return ">>";
    case TokenKind::PlusEq:
        return "+=";
    case TokenKind::MinusEq:
        return "-=";
    case TokenKind::StarEq:
        return "*=";
    case TokenKind::SlashEq:
        return "/=";
    case TokenKind::SlashSlashEq:
        return "//=";
    case TokenKind::PercentEq:
        return "%=";
    case TokenKind::AmpEq:
        return "&=";
    case TokenKind::PipeEq:
        return "|=";
    case TokenKind::CaretEq:
        return "^=";
    case TokenKind::LtLtEq:
        return "<<=";
    case TokenKind::GtGtEq:
        return ">>=";
    case TokenKind::PlusPercent:
        return "+%";
    case TokenKind::MinusPercent:
        return "-%";
    case TokenKind::StarPercent:
        return "*%";
    case TokenKind::PlusPipe:
        return "+|";
    case TokenKind::MinusPipe:
        return "-|";
    case TokenKind::StarPipe:
        return "*|";
    case TokenKind::PlusQuestion:
        return "+?";
    case TokenKind::MinusQuestion:
        return "-?";
    case TokenKind::StarQuestion:
        return "*?";
    case TokenKind::PlusPercentEq:
        return "+%=";
    case TokenKind::MinusPercentEq:
        return "-%=";
    case TokenKind::StarPercentEq:
        return "*%=";
    case TokenKind::PlusPipeEq:
        return "+|=";
    case TokenKind::MinusPipeEq:
        return "-|=";
    case TokenKind::StarPipeEq:
        return "*|=";
    case TokenKind::DashDashDash:
        return "---";
    case TokenKind::DotDotDot:
        return "...";
    }
    return "???";
}

} // namespace lucb
