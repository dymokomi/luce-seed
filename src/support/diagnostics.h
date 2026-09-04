// Diagnostics with stable codes. Tests pin the code, never the wording.

#pragma once

#include "source/source.h"

namespace lucb {

struct Diagnostic {
    string code;
    string path;
    Span span;
    string message;

    string format() const;
};

struct DiagnosticBag {
    vector<Diagnostic> items;

    void add(string code, string path, Span span, string message);
    bool has_code(string_view code) const;
    const Diagnostic* first() const;
    size_t count() const { return items.size(); }
    bool empty() const { return items.empty(); }
};

} // namespace lucb
