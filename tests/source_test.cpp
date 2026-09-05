//==============================================================================================
//
//   tests/source_test - Source loading
//
//   DESCRIPTION:
//       BOM and CRLF handling, the encoding gate, and position mapping.
//
//==============================================================================================

#include "source/source.h"
#include "support/diagnostics.h"
#include "support/test.h"

#include <string>

using lucb::DiagnosticBag;
using lucb::Source;

static Source load(std::string bytes, DiagnosticBag& diagnostics, const char* path = "t.lucb") {
    return Source::from_bytes(path, std::move(bytes), diagnostics);
}

TEST(source_plain_ascii_is_ok) {
    DiagnosticBag diagnostics;
    Source source = load("func main:\n    return\n", diagnostics);
    CHECK(source.ok());
    CHECK(diagnostics.empty());
    CHECK_EQ(source.scan_start(), 0u);
}

TEST(source_utf8_bom_at_zero_is_skipped) {
    DiagnosticBag diagnostics;
    std::string bytes = "\xEF\xBB\xBFlet x = 1\n";
    Source source = load(std::move(bytes), diagnostics);
    CHECK(source.ok());
    CHECK(diagnostics.empty());
    CHECK_EQ(source.scan_start(), 3u);
}

TEST(source_nul_is_rejected) {
    DiagnosticBag diagnostics;
    std::string bytes = "let x";
    bytes.push_back('\0');
    bytes += " = 1\n";
    Source source = load(std::move(bytes), diagnostics);
    CHECK(!source.ok());
    CHECK(diagnostics.has_code("lucb.source.nul"));
}

TEST(source_invalid_utf8_is_rejected) {
    DiagnosticBag diagnostics;
    Source source = load("let x = \x80\n", diagnostics);
    CHECK(!source.ok());
    CHECK(diagnostics.has_code("lucb.source.utf8"));
}

TEST(source_bidi_control_is_rejected) {
    DiagnosticBag diagnostics;
    // U+202E RIGHT-TO-LEFT OVERRIDE
    Source source = load("let x = 1\xE2\x80\xAE\n", diagnostics);
    CHECK(!source.ok());
    CHECK(diagnostics.has_code("lucb.source.bidi"));
}

TEST(source_tab_is_rejected) {
    DiagnosticBag diagnostics;
    Source source = load("let x\t= 1\n", diagnostics);
    CHECK(!source.ok());
    CHECK(diagnostics.has_code("lucb.lex.tab"));
}

TEST(source_lone_cr_is_rejected) {
    DiagnosticBag diagnostics;
    Source source = load("let x = 1\r", diagnostics);
    CHECK(!source.ok());
    CHECK(diagnostics.has_code("lucb.source.cr"));
}

TEST(source_crlf_is_accepted) {
    DiagnosticBag diagnostics;
    Source source = load("let x = 1\r\nlet y = 2\r\n", diagnostics);
    CHECK(source.ok());
    CHECK(diagnostics.empty());
}

TEST(source_utf16_le_bom_is_named) {
    DiagnosticBag diagnostics;
    Source source = load("\xFF\xFE"
                         "a\0",
                         diagnostics);
    CHECK(!source.ok());
    CHECK(diagnostics.has_code("lucb.source.encoding"));
}

TEST(source_interior_bom_is_rejected) {
    DiagnosticBag diagnostics;
    Source source = load("let\xEF\xBB\xBF x = 1\n", diagnostics);
    CHECK(!source.ok());
    CHECK(diagnostics.has_code("lucb.source.bom"));
}
