#include "lex/lexer.h"
#include "source/source.h"
#include "support/diagnostics.h"
#include "support/test.h"

#include <initializer_list>
#include <string>
#include <vector>

using lucb::DiagnosticBag;
using lucb::Source;
using lucb::Token;
using lucb::TokenKind;
using lucb::token_kind_name;
using lucb::tokenize;

struct Lexed {
    std::vector<Token> tokens;
    DiagnosticBag diagnostics;
    Source source;

    explicit Lexed(std::string text, const char* path = "t.lucb")
        : source(Source::from_bytes(path, std::move(text), diagnostics)) {
        if (source.ok()) {
            tokens = tokenize(source, diagnostics);
        }
    }

    std::vector<std::string> kinds() const {
        std::vector<std::string> names;
        names.reserve(tokens.size());
        for (const Token& token : tokens) {
            names.emplace_back(token_kind_name(token.kind));
        }
        return names;
    }

    std::string kinds_joined() const {
        std::string out;
        for (size_t i = 0; i < tokens.size(); i++) {
            if (i != 0) {
                out += ' ';
            }
            out += token_kind_name(tokens[i].kind);
        }
        return out;
    }

    bool has(std::string_view code) const { return diagnostics.has_code(code); }
};

static bool kinds_eq(const Lexed& lexed, std::initializer_list<const char*> expected) {
    const std::vector<std::string> got = lexed.kinds();
    if (got.size() != expected.size()) {
        std::fprintf(stderr, "    kind count %zu != %zu\n    got: %s\n", got.size(),
                     expected.size(), lexed.kinds_joined().c_str());
        return false;
    }
    size_t i = 0;
    for (const char* name : expected) {
        if (got[i] != name) {
            std::fprintf(stderr, "    kind %zu: got %s want %s\n    got: %s\n", i, got[i].c_str(),
                         name, lexed.kinds_joined().c_str());
            return false;
        }
        i++;
    }
    return true;
}

TEST(lex_empty_file_is_eof) {
    Lexed lexed("");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"eof"}));
}

TEST(lex_hello_function) {
    Lexed lexed("func main:\n    return\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"func", "name", ":", "newline", "indent", "return", "newline", "dedent",
                           "eof"}));
}

TEST(lex_missing_final_newline_is_inserted) {
    Lexed lexed("func main:\n    return");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"func", "name", ":", "newline", "indent", "return", "newline", "dedent",
                           "eof"}));
}

TEST(lex_keywords_are_not_names) {
    Lexed lexed("let var pub mutating self none true false\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"let", "var", "pub", "mutating", "self", "none", "true", "false",
                           "newline", "eof"}));
}

TEST(lex_base_only_keywords) {
    Lexed lexed("with alloc free const asm union volatile static errdefer thread_local goto\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"with", "alloc", "free", "const", "asm", "union", "volatile", "static",
                           "errdefer", "thread_local", "goto", "newline", "eof"}));
}

TEST(lex_full_luce_words_stay_keywords) {
    // So they cannot be identifiers; the parser refuses them with a tier diagnostic.
    Lexed lexed("class spawn weak\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"class", "spawn", "weak", "newline", "eof"}));
}

TEST(lex_contextual_words_are_names) {
    Lexed lexed("void packed align inline cold used noalias blocking out\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"name", "name", "name", "name", "name", "name", "name", "name", "name",
                           "newline", "eof"}));
}

TEST(lex_underscore_vs_unused) {
    Lexed lexed("_ _unused\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"_", "name", "newline", "eof"}));
    CHECK_STREQ(std::string(lexed.tokens[1].text), "_unused");
}

TEST(lex_same_line_suite_has_no_indent) {
    Lexed lexed("if cached: return result\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"if", "name", ":", "return", "name", "newline", "eof"}));
}

TEST(lex_blank_and_comment_lines_do_not_change_indent) {
    Lexed lexed("func f:\n"
                "    # note\n"
                "\n"
                "    return\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"func", "name", ":", "newline", "indent", "return", "newline", "dedent",
                           "eof"}));
}

TEST(lex_doc_comment_at_line_start) {
    Lexed lexed("## Area of a rectangle.\npub func area:\n    return\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"doc", "pub", "func", "name", ":", "newline", "indent", "return",
                           "newline", "dedent", "eof"}));
    CHECK_STREQ(std::string(lexed.tokens[0].text), "## Area of a rectangle.");
}

TEST(lex_hash_in_the_middle_of_a_line_is_not_a_doc_token) {
    Lexed lexed("let x = 1 ## not documentation\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"let", "name", "=", "int", "newline", "eof"}));
}

TEST(lex_nested_indent_and_dedent) {
    Lexed lexed("func f:\n"
                "    if x:\n"
                "        return\n"
                "    return\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"func", "name", ":", "newline", "indent", "if", "name", ":", "newline",
                           "indent", "return", "newline", "dedent", "return", "newline", "dedent",
                           "eof"}));
}

TEST(lex_bad_indent_step_is_reported) {
    Lexed lexed("func f:\n  return\n");
    CHECK(lexed.has("lucb.lex.indent"));
}

TEST(lex_newlines_inside_parens_are_spacing) {
    Lexed lexed("f(\n    1,\n    2\n)\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"name", "(", "int", ",", "int", ")", "newline", "eof"}));
}

TEST(lex_suite_inside_call) {
    // run(func ():\n    return 1)
    Lexed lexed("run(func ():\n    return 1)\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"name", "(", "func", "(", ")", ":", "newline", "indent", "return", "int",
                           "newline", "dedent", ")", "newline", "eof"}));
}

TEST(lex_base_operators_longest_first) {
    Lexed lexed("a +% b -| c *? d\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"name", "+%", "name", "-|", "name", "*?", "name", "newline", "eof"}));
}

TEST(lex_augmented_wrapping_operators) {
    Lexed lexed("x +%= 1\ny *|= 2\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"name", "+%=", "int", "newline", "name", "*|=", "int", "newline", "eof"}));
}

TEST(lex_markers_dashes_dots_at) {
    Lexed lexed("var b: u8[4] = ---\nextern func p(f: cstr, ...) -> i32\nvar n: @u32\n");
    CHECK(lexed.diagnostics.empty());
    bool has_dashes = false;
    bool has_dots = false;
    bool has_at = false;
    for (const Token& token : lexed.tokens) {
        if (token.kind == TokenKind::DashDashDash) {
            has_dashes = true;
        }
        if (token.kind == TokenKind::DotDotDot) {
            has_dots = true;
        }
        if (token.kind == TokenKind::At) {
            has_at = true;
        }
    }
    CHECK(has_dashes);
    CHECK(has_dots);
    CHECK(has_at);
}

TEST(lex_range_and_slash_slash) {
    Lexed lexed("a += b\nc..<d\ne // f\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"name", "+=", "name", "newline", "name", "..<", "name", "newline", "name",
                           "//", "name", "newline", "eof"}));
}

TEST(lex_dot_dot_eq_outranks_dot_dot) {
    Lexed lexed("1..=4\nitems[1..]\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"int", "..=", "int", "newline", "name", "[", "int", "..", "]", "newline",
                           "eof"}));
}

TEST(lex_arrow_and_fat_arrow) {
    Lexed lexed("func f() -> i64:\n    match x:\n        _ => 1\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"func", "name", "(", ")", "->", "name", ":", "newline", "indent", "match",
                           "name", ":", "newline", "indent", "_", "=>", "int", "newline", "dedent",
                           "dedent", "eof"}));
}

TEST(lex_integer_literals) {
    Lexed lexed("42 1_000_000 0xff 0o755 0b1010_1100 255u8\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"int", "int", "int", "int", "int", "int", "newline", "eof"}));
    CHECK_STREQ(std::string(lexed.tokens[1].text), "1_000_000");
    CHECK_STREQ(std::string(lexed.tokens[2].text), "0xff");
    CHECK_STREQ(std::string(lexed.tokens[5].suffix), "u8");
}

TEST(lex_float_literals) {
    Lexed lexed("1.0 6.022e23 0.5f32 1_000.25\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"float", "float", "float", "float", "newline", "eof"}));
    CHECK_STREQ(std::string(lexed.tokens[2].suffix), "f32");
}

TEST(lex_integer_dot_member_is_not_a_float) {
    Lexed lexed("1.member\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"int", ".", "name", "newline", "eof"}));
}

TEST(lex_uppercase_base_prefix_is_rejected) {
    Lexed lexed("0xFF\n");
    CHECK(lexed.diagnostics.empty());
    Lexed bad("0XFF\n");
    CHECK(bad.has("lucb.lex.number"));
}

TEST(lex_invalid_binary_digit) {
    Lexed lexed("0b102\n");
    CHECK(lexed.has("lucb.lex.number"));
}

TEST(lex_underscore_rules) {
    CHECK(Lexed("1__0\n").has("lucb.lex.number"));
    CHECK(Lexed("1_\n").has("lucb.lex.number"));
    CHECK(Lexed("_1\n").diagnostics.empty()); // name, not a number
}

TEST(lex_invalid_suffix) {
    CHECK(Lexed("1u7\n").has("lucb.lex.number"));
    CHECK(Lexed("1.0f128\n").has("lucb.lex.number"));
}

TEST(lex_character_literals) {
    Lexed lexed("'A' '\\n' '\\u{41}'\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"char", "char", "char", "newline", "eof"}));
}

TEST(lex_character_must_be_one_scalar) {
    CHECK(Lexed("'ab'\n").has("lucb.lex.char"));
    CHECK(Lexed("''\n").has("lucb.lex.char"));
}

TEST(lex_strings_raw_and_bytes) {
    Lexed lexed("\"hello\" r\"C:\\studio\" b\"\\x89PNG\"\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"string", "string", "bytes", "newline", "eof"}));
}

TEST(lex_triple_quoted_string) {
    Lexed lexed("\"\"\"hello\nworld\"\"\"\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"string", "newline", "eof"}));
}

TEST(lex_unterminated_string) {
    CHECK(Lexed("\"hello\n").has("lucb.lex.string"));
}

TEST(lex_formatted_string_fields) {
    Lexed lexed("f\"a {b} c\"\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"format_start", "format_text", "{", "name", "}", "format_text",
                           "format_end", "newline", "eof"}));
    CHECK_STREQ(std::string(lexed.tokens[0].text), "f\"");
    CHECK_STREQ(std::string(lexed.tokens[1].text), "a ");
    CHECK_STREQ(std::string(lexed.tokens[5].text), " c");
}

TEST(lex_formatted_string_adjacent_fields) {
    Lexed lexed("f\"{a}{b}\"\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"format_start", "{", "name", "}", "{", "name", "}", "format_end",
                           "newline", "eof"}));
}

TEST(lex_formatted_string_escaped_braces) {
    Lexed lexed("f\"{{x}}\"\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"format_start", "format_text", "format_end", "newline", "eof"}));
    CHECK_STREQ(std::string(lexed.tokens[1].text), "{{x}}");
}

TEST(lex_crlf_layout) {
    Lexed lexed("func f:\r\n    return\r\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"func", "name", ":", "newline", "indent", "return", "newline", "dedent",
                           "eof"}));
}

TEST(lex_bom_does_not_shift_columns) {
    Lexed lexed("\xEF\xBB\xBFlet x = 1\n");
    CHECK(lexed.diagnostics.empty());
    CHECK_EQ(lexed.tokens[0].span.column, 1u);
    CHECK_STREQ(std::string(lexed.tokens[0].text), "let");
}

TEST(lex_lookalike_quote_is_named) {
    // U+201C LEFT DOUBLE QUOTATION MARK
    Lexed lexed("a = \xE2\x80\x9Cx\n");
    CHECK(lexed.has("lucb.lex.character"));
    CHECK(lexed.diagnostics.first() != nullptr);
    const std::string message = lexed.diagnostics.first()->message;
    CHECK(message.find("like this") != std::string::npos);
}

TEST(lex_lookalike_not_equal) {
    // U+2260 NOT EQUAL TO
    Lexed lexed("a = 1 \xE2\x89\xA0 2\n");
    CHECK(lexed.has("lucb.lex.character"));
    const std::string message = lexed.diagnostics.first()->message;
    CHECK(message.find("!=") != std::string::npos);
}

TEST(lex_unicode_in_a_string_is_allowed) {
    Lexed lexed("\"café — hello\"\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"string", "newline", "eof"}));
}

TEST(lex_unclosed_paren) {
    CHECK(Lexed("f(1\n").has("lucb.lex.delimiter"));
}

TEST(lex_mismatched_delimiter) {
    CHECK(Lexed("f(1]\n").has("lucb.lex.delimiter"));
}

TEST(lex_slash_slash_is_division_not_a_comment) {
    Lexed lexed("a // b # real comment\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"name", "//", "name", "newline", "eof"}));
}

TEST(lex_core_type_names_are_names) {
    Lexed lexed("i64 u32 usize str bool never unit\n");
    CHECK(lexed.diagnostics.empty());
    CHECK(kinds_eq(lexed, {"name", "name", "name", "name", "name", "name", "name", "newline",
                           "eof"}));
}
