// Diagnostics with stable codes. Tests pin the code, never the wording.

#pragma once

#include "source/source.h"

#include <string>
#include <string_view>
#include <vector>

namespace lucb {

struct Diagnostic {
    std::string code;
    std::string path;
    Span span;
    std::string message;

    std::string format() const;
};

class DiagnosticBag {
public:
    void add(std::string code, std::string path, Span span, std::string message);

    const std::vector<Diagnostic>& items() const { return items_; }
    size_t count() const { return items_.size(); }
    bool empty() const { return items_.empty(); }

    bool has_code(std::string_view code) const;
    const Diagnostic* first() const;

private:
    std::vector<Diagnostic> items_;
};

} // namespace lucb
