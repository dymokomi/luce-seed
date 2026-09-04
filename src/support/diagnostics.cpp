#include "support/diagnostics.h"

#include <sstream>

namespace lucb {

std::string Diagnostic::format() const {
    std::ostringstream out;
    out << path << ':' << span.line << ':' << span.column << ": " << message << " [" << code
        << ']';
    return out.str();
}

void DiagnosticBag::add(std::string code, std::string path, Span span, std::string message) {
    items_.push_back(Diagnostic{
        .code = std::move(code),
        .path = std::move(path),
        .span = span,
        .message = std::move(message),
    });
}

bool DiagnosticBag::has_code(std::string_view code) const {
    for (const Diagnostic& item : items_) {
        if (item.code == code) {
            return true;
        }
    }
    return false;
}

const Diagnostic* DiagnosticBag::first() const {
    if (items_.empty()) {
        return nullptr;
    }
    return &items_.front();
}

} // namespace lucb
