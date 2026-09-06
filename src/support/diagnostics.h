//==============================================================================================
//
//   support/diagnostics - Diagnostics with stable codes
//
//   DESCRIPTION:
//       Tests pin the code, never the wording, so messages can improve freely.
//
//==============================================================================================

#pragma once

#include "source/source.h"

namespace lucb {

struct Diagnostic {
    string code;
    string path;
    Span span;
    string message;
    bool warning = false;

    string format() const;
};

struct DiagnosticBag {
    vector<Diagnostic> items;
    // Warnings never fail a check; they are printed on request (`-W`).
    vector<Diagnostic> warnings;

    void add(string code, string path, Span span, string message);
    void warn(string code, string path, Span span, string message);
    bool has_warning(string_view code) const;
    bool has_code(string_view code) const;
    const Diagnostic* first() const;
    size_t count() const {
        return items.size();
    }
    bool empty() const {
        return items.empty();
    }
};

} // namespace lucb
