#include "lex/lexer.h"

#include <utility>

namespace lucb {
namespace {

bool is_ascii_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_ascii_digit(char c) {
    return c >= '0' && c <= '9';
}

bool is_ident_start(char c) {
    return is_ascii_alpha(c) || c == '_';
}

bool is_ident_continue(char c) {
    return is_ident_start(c) || is_ascii_digit(c);
}

bool is_hex_digit(char c) {
    return is_ascii_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int hex_value(char c) {
    if (c <= '9') {
        return c - '0';
    }
    if (c <= 'F') {
        return c - 'A' + 10;
    }
    return c - 'a' + 10;
}

bool is_digit_for_base(char c, char base) {
    switch (base) {
    case 'b':
        return c == '0' || c == '1';
    case 'o':
        return c >= '0' && c <= '7';
    case 'x':
        return is_hex_digit(c);
    default:
        return is_ascii_digit(c);
    }
}

bool is_integer_suffix(std::string_view s) {
    return s == "u8" || s == "u16" || s == "u32" || s == "u64" || s == "i8" || s == "i16" ||
           s == "i32" || s == "i64";
}

bool is_float_suffix(std::string_view s) {
    return s == "f16" || s == "f32" || s == "f64";
}

enum class Delim : uint8_t { Paren, Bracket, Brace, FormatField };

char closing_for(Delim d) {
    switch (d) {
    case Delim::Paren:
        return ')';
    case Delim::Bracket:
        return ']';
    case Delim::Brace:
        return '}';
    case Delim::FormatField:
        return '}';
    }
    return 0;
}

struct LayoutRegion {
    int depth = 0;
    int base = 0;
};

struct FormattedMode {
    bool is_triple = false;
    uint32_t line = 1;
    uint32_t column = 1;
};

struct SymbolMatch {
    TokenKind kind = TokenKind::EndOfFile;
    size_t width = 0;
};

class Tokenizer {
public:
    Tokenizer(const Source& source, DiagnosticBag& diagnostics)
        : source_(source), diagnostics_(diagnostics), bytes_(source.bytes()) {
        pos_ = source.scan_start();
        line_start_ = pos_;
    }

    std::vector<Token> run() {
        if (!source_.ok()) {
            emit(TokenKind::EndOfFile, pos_, pos_);
            return std::move(tokens_);
        }
        while (!at_end()) {
            if (pending_layout_ && layout_active()) {
                scan_line_start();
            } else {
                scan_token();
            }
            if (tokens_.size() > 1'000'000) {
                error("lucb.lex.limit", "too many tokens");
                break;
            }
        }
        finish();
        return std::move(tokens_);
    }

private:
    const Source& source_;
    DiagnosticBag& diagnostics_;
    std::string_view bytes_;
    size_t pos_ = 0;
    size_t line_start_ = 0;
    uint32_t line_ = 1;
    int indent_width_ = 0;
    bool pending_layout_ = true;
    std::vector<Delim> delimiters_;
    std::vector<LayoutRegion> layout_regions_;
    std::vector<FormattedMode> formatted_;
    std::vector<Token> tokens_;
    uint32_t commented_close_line_ = 0;
    uint32_t commented_close_column_ = 0;

    bool at_end() const { return pos_ >= bytes_.size(); }

    char peek_byte(size_t offset = 0) const {
        if (pos_ + offset >= bytes_.size()) {
            return 0;
        }
        return bytes_[pos_ + offset];
    }

    bool starts_with(std::string_view text) const {
        if (pos_ + text.size() > bytes_.size()) {
            return false;
        }
        return bytes_.substr(pos_, text.size()) == text;
    }

    uint32_t column() const { return static_cast<uint32_t>(pos_ - line_start_ + 1); }

    std::string_view slice(size_t start, size_t end) const {
        if (end < start || end > bytes_.size()) {
            return {};
        }
        return bytes_.substr(start, end - start);
    }

    void error_at(uint32_t at_line, uint32_t at_column, size_t byte, std::string code,
                  std::string message) {
        Span span;
        span.start = static_cast<uint32_t>(byte);
        span.end = static_cast<uint32_t>(byte);
        span.line = at_line;
        span.column = at_column;
        diagnostics_.add(std::move(code), std::string(source_.path()), span, std::move(message));
    }

    void error(std::string code, std::string message) {
        error_at(line_, column(), pos_, std::move(code), std::move(message));
    }

    void emit_at(TokenKind kind, size_t start, size_t end, uint32_t token_line,
                 uint32_t token_column, std::string_view suffix = {}) {
        Token token;
        token.kind = kind;
        token.span.start = static_cast<uint32_t>(start);
        token.span.end = static_cast<uint32_t>(end);
        token.span.line = token_line;
        token.span.column = token_column;
        token.text = slice(start, end);
        token.suffix = suffix;
        tokens_.push_back(token);
    }

    void emit(TokenKind kind, size_t start, size_t end, std::string_view suffix = {}) {
        emit_at(kind, start, end, line_, column_of(start), suffix);
    }

    uint32_t column_of(size_t byte) const {
        if (byte < line_start_) {
            return 1;
        }
        return static_cast<uint32_t>(byte - line_start_ + 1);
    }

    void advance_line() {
        if (peek_byte() == '\r') {
            pos_ += 2;
        } else {
            pos_ += 1;
        }
        line_ += 1;
        line_start_ = pos_;
    }

    bool layout_active() const {
        if (layout_regions_.empty()) {
            return delimiters_.empty();
        }
        return static_cast<int>(delimiters_.size()) == layout_regions_.back().depth;
    }

    void scan_line_start() {
        while (!at_end()) {
            size_t width = 0;
            while (pos_ + width < bytes_.size() && bytes_[pos_ + width] == ' ') {
                width += 1;
            }
            const size_t content = pos_ + width;
            if (content >= bytes_.size()) {
                pos_ = content;
                return;
            }
            const char next = bytes_[content];
            if (next == '\n' || next == '\r') {
                pos_ = content;
                advance_line();
            } else if (next == '#') {
                pos_ = content;
                if (starts_with("##")) {
                    update_indentation(static_cast<int>(width));
                    scan_documentation();
                } else {
                    skip_comment();
                }
            } else {
                pos_ = content;
                update_indentation(static_cast<int>(width));
                pending_layout_ = false;
                return;
            }
        }
    }

    void update_indentation(int width) {
        if (!layout_regions_.empty() && width < layout_regions_.back().base) {
            error("lucb.lex.indent",
                  "a suite inside delimiters cannot dedent past the line that opened it");
            return;
        }
        if (width > indent_width_) {
            if (width != indent_width_ + 4) {
                error("lucb.lex.indent", "indentation must increase by four spaces");
                return;
            }
            indent_width_ = width;
            emit(TokenKind::Indent, pos_, pos_);
            return;
        }
        while (width < indent_width_) {
            indent_width_ -= 4;
            emit(TokenKind::Dedent, pos_, pos_);
        }
        if (width != indent_width_) {
            error("lucb.lex.indent", "dedent does not match any outer indentation");
            return;
        }
        if (!layout_regions_.empty() && width == layout_regions_.back().base) {
            layout_regions_.pop_back();
        }
    }

    void scan_documentation() {
        const size_t start = pos_;
        const uint32_t col = column();
        skip_comment();
        emit_at(TokenKind::DocComment, start, pos_, line_, col);
    }

    void skip_comment() {
        while (!at_end() && peek_byte() != '\n' && peek_byte() != '\r') {
            if (!formatted_.empty() && commented_close_line_ == 0 && starts_with("}\"\"\"")) {
                commented_close_line_ = line_;
                commented_close_column_ = column();
            }
            pos_ += 1;
        }
    }

    void require_multiline_field() {
        if (formatted_.empty()) {
            return;
        }
        const FormattedMode& mode = formatted_.back();
        if (!mode.is_triple) {
            error_at(mode.line, mode.column, pos_, "lucb.lex.string",
                     "formatted string interpolation cannot cross a line");
        }
    }

    void scan_token() {
        const char c = peek_byte();
        if (c == ' ') {
            pos_ += 1;
            return;
        }
        if (c == '#') {
            require_multiline_field();
            skip_comment();
            return;
        }
        if (c == '\n' || c == '\r') {
            require_multiline_field();
            if (layout_active()) {
                emit(TokenKind::Newline, pos_, pos_);
                pending_layout_ = true;
            }
            advance_line();
            return;
        }

        const size_t start = pos_;
        const uint32_t start_line = line_;
        const uint32_t start_column = column();

        if ((c == 'f' || c == 'r' || c == 'b') && peek_byte(1) == '"') {
            scan_string_literal(start, start_line, start_column, c);
            return;
        }
        if (c == '"') {
            scan_string_literal(start, start_line, start_column, 0);
            return;
        }
        if (c == '\'') {
            scan_character(start, start_line, start_column);
            return;
        }
        if (c == '_' && !is_ident_continue(peek_byte(1))) {
            pos_ += 1;
            emit_at(TokenKind::Underscore, start, pos_, start_line, start_column);
            return;
        }
        if (is_ident_start(c)) {
            scan_word(start, start_line, start_column);
            return;
        }
        if (is_ascii_digit(c)) {
            scan_number(start, start_line, start_column);
            return;
        }

        const SymbolMatch symbol = match_symbol();
        if (symbol.width > 0) {
            scan_symbol(start, start_line, start_column, symbol);
            return;
        }

        char32_t cp = 0;
        const size_t width = utf8_next(bytes_, pos_, cp);
        if (width == 0) {
            error("lucb.lex.character", "unexpected character");
            pos_ += 1;
            return;
        }
        if (const char* hint = confusable_hint(cp)) {
            error("lucb.lex.character", hint);
        } else {
            error("lucb.lex.character", "unexpected character");
        }
        pos_ += width;
    }

    void scan_word(size_t start, uint32_t start_line, uint32_t start_column) {
        pos_ += 1;
        while (is_ident_continue(peek_byte())) {
            pos_ += 1;
        }
        const std::string_view word = slice(start, pos_);
        const TokenKind kind = keyword_kind(word);
        emit_at(kind, start, pos_, start_line, start_column);
    }

    void scan_string_literal(size_t start, uint32_t start_line, uint32_t start_column, char prefix) {
        if (prefix != 0) {
            pos_ += 1;
        }
        if (prefix == 'f') {
            const bool is_triple = starts_with("\"\"\"");
            pos_ += is_triple ? 3 : 1;
            emit_at(TokenKind::FormatStart, start, pos_, start_line, start_column);
            formatted_.push_back(FormattedMode{is_triple, start_line, start_column});
            scan_formatted_text();
            return;
        }
        if (!scan_string_body(prefix, start_line, start_column)) {
            return;
        }
        const TokenKind kind = prefix == 'b' ? TokenKind::BytesLit : TokenKind::StringLit;
        emit_at(kind, start, pos_, start_line, start_column);
    }

    bool scan_string_body(char prefix, uint32_t start_line, uint32_t start_column) {
        const bool is_raw = prefix == 'r';
        const bool byte_mode = prefix == 'b';
        const bool is_triple = starts_with("\"\"\"");
        pos_ += is_triple ? 3 : 1;

        while (!at_end()) {
            if (is_triple && starts_with("\"\"\"")) {
                pos_ += 3;
                return true;
            }
            const char c = peek_byte();
            if (!is_triple && c == '"') {
                pos_ += 1;
                return true;
            }
            if (!is_raw && c == '\\') {
                if (!scan_escape(byte_mode)) {
                    return false;
                }
            } else if (c == '\n' || c == '\r') {
                if (!is_triple) {
                    error_at(start_line, start_column, pos_, "lucb.lex.string",
                             "unterminated string literal");
                    return false;
                }
                advance_line();
            } else {
                char32_t cp = 0;
                const size_t width = utf8_next(bytes_, pos_, cp);
                if (width == 0) {
                    error("lucb.lex.string", "invalid UTF-8 in string");
                    return false;
                }
                if (byte_mode && cp > 0x7F) {
                    error("lucb.lex.string", "byte literals require ASCII source characters");
                    pos_ += width;
                    return false;
                }
                pos_ += width;
            }
        }
        error_at(start_line, start_column, pos_, "lucb.lex.string", "unterminated string literal");
        return false;
    }

    void scan_formatted_text() {
        const FormattedMode mode = formatted_.back();
        size_t start = pos_;
        uint32_t start_line = line_;
        uint32_t start_column = column();

        auto emit_text = [&]() {
            if (pos_ > start) {
                emit_at(TokenKind::FormatText, start, pos_, start_line, start_column);
            }
        };

        while (!at_end()) {
            if (mode.is_triple && starts_with("\"\"\"")) {
                emit_text();
                const uint32_t close_column = column();
                const size_t close_start = pos_;
                pos_ += 3;
                emit_at(TokenKind::FormatEnd, close_start, pos_, line_, close_column);
                formatted_.pop_back();
                return;
            }
            const char c = peek_byte();
            if (!mode.is_triple && c == '"') {
                emit_text();
                const uint32_t close_column = column();
                const size_t close_start = pos_;
                pos_ += 1;
                emit_at(TokenKind::FormatEnd, close_start, pos_, line_, close_column);
                formatted_.pop_back();
                return;
            }
            if (c == '\\') {
                if (!scan_escape(false)) {
                    return;
                }
            } else if (c == '{') {
                if (starts_with("{{")) {
                    pos_ += 2;
                } else {
                    emit_text();
                    const uint32_t brace_column = column();
                    const size_t brace_start = pos_;
                    pos_ += 1;
                    delimiters_.push_back(Delim::FormatField);
                    emit_at(TokenKind::LBrace, brace_start, pos_, line_, brace_column);
                    return;
                }
            } else if (c == '}') {
                if (starts_with("}}")) {
                    pos_ += 2;
                } else {
                    error_at(mode.line, mode.column, pos_, "lucb.lex.string",
                             "unescaped '}' in formatted string");
                    pos_ += 1;
                    return;
                }
            } else if (c == '\n' || c == '\r') {
                if (!mode.is_triple) {
                    error_at(mode.line, mode.column, pos_, "lucb.lex.string",
                             "unterminated string literal");
                    return;
                }
                advance_line();
            } else {
                char32_t cp = 0;
                const size_t width = utf8_next(bytes_, pos_, cp);
                pos_ += width == 0 ? 1 : width;
            }
        }
        error_at(mode.line, mode.column, pos_, "lucb.lex.string", "unterminated string literal");
    }

    void scan_character(size_t start, uint32_t start_line, uint32_t start_column) {
        pos_ += 1;
        int scalars = 0;
        while (!at_end()) {
            const char c = peek_byte();
            if (c == '\\') {
                if (!scan_escape(false)) {
                    return;
                }
                scalars += 1;
            } else if (c == '\'') {
                pos_ += 1;
                if (scalars != 1) {
                    error_at(start_line, start_column, start, "lucb.lex.char",
                             "character literal must contain one Unicode scalar");
                    return;
                }
                emit_at(TokenKind::CharLit, start, pos_, start_line, start_column);
                return;
            } else if (c == '\n' || c == '\r') {
                error_at(start_line, start_column, start, "lucb.lex.char",
                         "unterminated character literal");
                return;
            } else {
                char32_t cp = 0;
                const size_t width = utf8_next(bytes_, pos_, cp);
                if (width == 0) {
                    error("lucb.lex.char", "invalid UTF-8 in character literal");
                    return;
                }
                pos_ += width;
                scalars += 1;
            }
        }
        error_at(start_line, start_column, start, "lucb.lex.char",
                 "unterminated character literal");
    }

    bool scan_escape(bool byte_mode) {
        const uint32_t escape_column = column();
        const size_t escape_pos = pos_;
        pos_ += 1;
        if (at_end() || peek_byte() == '\n' || peek_byte() == '\r') {
            error_at(line_, escape_column, escape_pos, "lucb.lex.escape",
                     "unterminated literal escape");
            return false;
        }
        const char escaped = peek_byte();
        if (escaped == '\\' || escaped == '"' || escaped == '\'' || escaped == 'n' ||
            escaped == 'r' || escaped == 't' || escaped == '0') {
            pos_ += 1;
            return true;
        }
        if (escaped == 'u' && peek_byte(1) == '{') {
            pos_ += 2;
            int digits = 0;
            int scalar = 0;
            while (is_hex_digit(peek_byte())) {
                scalar = scalar * 16 + hex_value(peek_byte());
                digits += 1;
                pos_ += 1;
            }
            if (digits == 0 || digits > 6 || peek_byte() != '}') {
                error_at(line_, escape_column, escape_pos, "lucb.lex.escape",
                         "Unicode escape must be \\u{HEX}");
                return false;
            }
            if (scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF)) {
                error_at(line_, escape_column, escape_pos, "lucb.lex.escape",
                         "Unicode escape is not a scalar value");
                return false;
            }
            pos_ += 1;
            return true;
        }
        if (byte_mode && escaped == 'x') {
            pos_ += 1;
            if (!is_hex_digit(peek_byte()) || !is_hex_digit(peek_byte(1))) {
                error_at(line_, escape_column, escape_pos, "lucb.lex.escape",
                         "byte escape must contain two hex digits");
                return false;
            }
            pos_ += 2;
            return true;
        }
        error_at(line_, escape_column, escape_pos, "lucb.lex.escape", "invalid literal escape");
        return false;
    }

    void scan_number(size_t start, uint32_t start_line, uint32_t start_column) {
        TokenKind kind = TokenKind::IntLit;
        std::string_view suffix;

        if (peek_byte() == '0' &&
            (peek_byte(1) == 'X' || peek_byte(1) == 'O' || peek_byte(1) == 'B')) {
            error_at(start_line, start_column, start, "lucb.lex.number",
                     "based integer prefixes must use lowercase `0x`, `0o`, or `0b`");
            pos_ += 2;
            return;
        }

        if (peek_byte() == '0' &&
            (peek_byte(1) == 'x' || peek_byte(1) == 'o' || peek_byte(1) == 'b')) {
            const char base = peek_byte(1);
            pos_ += 2;
            if (!scan_digit_sequence(base)) {
                return;
            }
            if (is_ascii_digit(peek_byte())) {
                error("lucb.lex.number", "digit is not valid for this numeric base");
                return;
            }
            suffix = scan_numeric_suffix();
            if (!suffix.empty() && !is_integer_suffix(suffix)) {
                error_at(start_line, start_column, start, "lucb.lex.number",
                         "invalid integer literal suffix");
                return;
            }
        } else {
            if (!scan_digit_sequence('d')) {
                return;
            }
            if (peek_byte() == '.' && is_ascii_digit(peek_byte(1))) {
                kind = TokenKind::FloatLit;
                pos_ += 1;
                if (!scan_digit_sequence('d')) {
                    return;
                }
            }
            if (peek_byte() == 'e' || peek_byte() == 'E') {
                kind = TokenKind::FloatLit;
                pos_ += 1;
                if (peek_byte() == '+' || peek_byte() == '-') {
                    pos_ += 1;
                }
                if (!scan_digit_sequence('d')) {
                    return;
                }
            }
            suffix = scan_numeric_suffix();
            if (is_float_suffix(suffix)) {
                kind = TokenKind::FloatLit;
            } else if (!suffix.empty() && (kind == TokenKind::FloatLit || !is_integer_suffix(suffix))) {
                error_at(start_line, start_column, start, "lucb.lex.number",
                         "invalid numeric literal suffix");
                return;
            }
        }
        emit_at(kind, start, pos_, start_line, start_column, suffix);
    }

    bool scan_digit_sequence(char base) {
        const uint32_t start_column = column();
        const size_t start_pos = pos_;
        bool has_digit = false;
        bool after_separator = false;
        while (is_digit_for_base(peek_byte(), base) || peek_byte() == '_') {
            if (peek_byte() == '_') {
                if (!has_digit || after_separator) {
                    error("lucb.lex.number", "underscores must separate numeric digits");
                    return false;
                }
                after_separator = true;
            } else {
                has_digit = true;
                after_separator = false;
            }
            pos_ += 1;
        }
        if (!has_digit) {
            error_at(line_, start_column, start_pos, "lucb.lex.number",
                     "numeric literal requires a digit");
            return false;
        }
        if (after_separator) {
            error("lucb.lex.number", "numeric literal cannot end with underscore");
            return false;
        }
        return true;
    }

    std::string_view scan_numeric_suffix() {
        if (!is_ascii_alpha(peek_byte())) {
            return {};
        }
        const size_t start = pos_;
        while (is_ident_continue(peek_byte())) {
            pos_ += 1;
        }
        return slice(start, pos_);
    }

    SymbolMatch match_symbol() const {
        // Longest first. Base-only family before shared so +%= is not + then %=.
        auto take = [&](std::string_view s, TokenKind k) -> SymbolMatch {
            if (starts_with(s)) {
                return {k, s.size()};
            }
            return {};
        };

        if (SymbolMatch m = take("+%=", TokenKind::PlusPercentEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take("-%=", TokenKind::MinusPercentEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take("*%=", TokenKind::StarPercentEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take("+|=", TokenKind::PlusPipeEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take("-|=", TokenKind::MinusPipeEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take("*|=", TokenKind::StarPipeEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take("//=", TokenKind::SlashSlashEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take("<<=", TokenKind::LtLtEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take(">>=", TokenKind::GtGtEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take("---", TokenKind::DashDashDash); m.width) {
            return m;
        }
        if (SymbolMatch m = take("...", TokenKind::DotDotDot); m.width) {
            return m;
        }
        if (SymbolMatch m = take("..<", TokenKind::DotDotLt); m.width) {
            return m;
        }
        if (SymbolMatch m = take("..=", TokenKind::DotDotEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take("+%", TokenKind::PlusPercent); m.width) {
            return m;
        }
        if (SymbolMatch m = take("-%", TokenKind::MinusPercent); m.width) {
            return m;
        }
        if (SymbolMatch m = take("*%", TokenKind::StarPercent); m.width) {
            return m;
        }
        if (SymbolMatch m = take("+|", TokenKind::PlusPipe); m.width) {
            return m;
        }
        if (SymbolMatch m = take("-|", TokenKind::MinusPipe); m.width) {
            return m;
        }
        if (SymbolMatch m = take("*|", TokenKind::StarPipe); m.width) {
            return m;
        }
        if (SymbolMatch m = take("+?", TokenKind::PlusQuestion); m.width) {
            return m;
        }
        if (SymbolMatch m = take("-?", TokenKind::MinusQuestion); m.width) {
            return m;
        }
        if (SymbolMatch m = take("*?", TokenKind::StarQuestion); m.width) {
            return m;
        }
        if (SymbolMatch m = take("->", TokenKind::Arrow); m.width) {
            return m;
        }
        if (SymbolMatch m = take("=>", TokenKind::FatArrow); m.width) {
            return m;
        }
        if (SymbolMatch m = take("==", TokenKind::EqEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take("!=", TokenKind::NotEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take("<=", TokenKind::LtEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take(">=", TokenKind::GtEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take("//", TokenKind::SlashSlash); m.width) {
            return m;
        }
        if (SymbolMatch m = take("<<", TokenKind::LtLt); m.width) {
            return m;
        }
        if (SymbolMatch m = take(">>", TokenKind::GtGt); m.width) {
            return m;
        }
        if (SymbolMatch m = take("+=", TokenKind::PlusEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take("-=", TokenKind::MinusEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take("*=", TokenKind::StarEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take("/=", TokenKind::SlashEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take("%=", TokenKind::PercentEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take("&=", TokenKind::AmpEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take("|=", TokenKind::PipeEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take("^=", TokenKind::CaretEq); m.width) {
            return m;
        }
        if (SymbolMatch m = take("..", TokenKind::DotDot); m.width) {
            return m;
        }

        switch (peek_byte()) {
        case '(':
            return {TokenKind::LParen, 1};
        case ')':
            return {TokenKind::RParen, 1};
        case '[':
            return {TokenKind::LBracket, 1};
        case ']':
            return {TokenKind::RBracket, 1};
        case '{':
            return {TokenKind::LBrace, 1};
        case '}':
            return {TokenKind::RBrace, 1};
        case ',':
            return {TokenKind::Comma, 1};
        case ':':
            return {TokenKind::Colon, 1};
        case '.':
            return {TokenKind::Dot, 1};
        case '?':
            return {TokenKind::Question, 1};
        case '!':
            return {TokenKind::Bang, 1};
        case '@':
            return {TokenKind::At, 1};
        case '+':
            return {TokenKind::Plus, 1};
        case '-':
            return {TokenKind::Minus, 1};
        case '*':
            return {TokenKind::Star, 1};
        case '/':
            return {TokenKind::Slash, 1};
        case '%':
            return {TokenKind::Percent, 1};
        case '&':
            return {TokenKind::Amp, 1};
        case '|':
            return {TokenKind::Pipe, 1};
        case '^':
            return {TokenKind::Caret, 1};
        case '~':
            return {TokenKind::Tilde, 1};
        case '<':
            return {TokenKind::Lt, 1};
        case '>':
            return {TokenKind::Gt, 1};
        case '=':
            return {TokenKind::Eq, 1};
        default:
            return {};
        }
    }

    bool colon_ends_line() const {
        size_t offset = 0;
        while (peek_byte(offset) == ' ') {
            offset += 1;
        }
        const char next = peek_byte(offset);
        return next == '#' || next == '\n' || next == '\r' || next == 0;
    }

    void finish_layout_region() {
        if (layout_regions_.empty()) {
            return;
        }
        const LayoutRegion region = layout_regions_.back();
        if (region.depth != static_cast<int>(delimiters_.size())) {
            return;
        }
        if (!tokens_.empty()) {
            const TokenKind last = tokens_.back().kind;
            if (last != TokenKind::Newline) {
                emit(TokenKind::Newline, pos_, pos_);
            }
        }
        while (indent_width_ > region.base) {
            indent_width_ -= 4;
            emit(TokenKind::Dedent, pos_, pos_);
        }
        layout_regions_.pop_back();
    }

    bool close_delimiter(char closing, uint32_t col, size_t at) {
        if (delimiters_.empty()) {
            error_at(line_, col, at, "lucb.lex.delimiter",
                     "closing delimiter has no matching opener");
            return false;
        }
        const Delim opening = delimiters_.back();
        if (closing != closing_for(opening)) {
            error_at(line_, col, at, "lucb.lex.delimiter",
                     "closing delimiter does not match its opener");
            return false;
        }
        delimiters_.pop_back();
        return opening == Delim::FormatField;
    }

    void scan_symbol(size_t start, uint32_t start_line, uint32_t start_column, SymbolMatch symbol) {
        pos_ = start + symbol.width;
        bool closes_field = false;

        if (symbol.width == 1) {
            const char ch = bytes_[start];
            if (ch == '(') {
                delimiters_.push_back(Delim::Paren);
            } else if (ch == '[') {
                delimiters_.push_back(Delim::Bracket);
            } else if (ch == '{') {
                delimiters_.push_back(Delim::Brace);
            } else if (ch == ')' || ch == ']' || ch == '}') {
                finish_layout_region();
                closes_field = close_delimiter(ch, start_column, start);
            } else if (ch == ':' && !delimiters_.empty() && colon_ends_line()) {
                layout_regions_.push_back(LayoutRegion{
                    .depth = static_cast<int>(delimiters_.size()),
                    .base = indent_width_,
                });
            }
        }

        emit_at(symbol.kind, start, pos_, start_line, start_column);
        if (closes_field) {
            scan_formatted_text();
        }
    }

    void finish() {
        if (!formatted_.empty()) {
            if (commented_close_line_ > 0) {
                error_at(commented_close_line_, commented_close_column_, pos_, "lucb.lex.string",
                         "formatted string interpolation cannot close inside a comment");
            } else {
                const FormattedMode& mode = formatted_.back();
                error_at(mode.line, mode.column, pos_, "lucb.lex.string",
                         "unterminated formatted string interpolation");
            }
        }
        if (!delimiters_.empty() && formatted_.empty()) {
            error("lucb.lex.delimiter", "unclosed delimiter");
        }

        if (!tokens_.empty()) {
            const TokenKind last = tokens_.back().kind;
            const bool needs_newline = last != TokenKind::Newline && last != TokenKind::Indent &&
                                       last != TokenKind::Dedent && last != TokenKind::DocComment;
            if (needs_newline) {
                emit(TokenKind::Newline, pos_, pos_);
            }
        }
        while (indent_width_ > 0) {
            indent_width_ -= 4;
            emit(TokenKind::Dedent, pos_, pos_);
        }
        emit(TokenKind::EndOfFile, pos_, pos_);
    }
};

} // namespace

std::vector<Token> tokenize(const Source& source, DiagnosticBag& diagnostics) {
    Tokenizer tokenizer(source, diagnostics);
    return tokenizer.run();
}

} // namespace lucb
