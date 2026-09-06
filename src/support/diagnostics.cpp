//==============================================================================================
//
//   support/diagnostics - Diagnostic formatting
//
//   DESCRIPTION:
//       `path:line:column: message [code]`, the shape every stage reports.
//
//==============================================================================================

#include "support/diagnostics.h"

namespace lucb {

string Diagnostic::format() const {
    return path + ":" + std::to_string(span.line) + ":" + std::to_string(span.column) + ": " +
           (warning ? "warning: " : "") + message + " [" + code + "]";
}

void DiagnosticBag::warn(string code, string path, Span span, string message) {
    Diagnostic item;
    item.code = code;
    item.path = path;
    item.span = span;
    item.message = message;
    item.warning = true;
    warnings.push_back(item);
}

bool DiagnosticBag::has_warning(string_view code) const {
    for (size_t i = 0; i < warnings.size(); i++) {
        if (warnings[i].code == code) {
            return true;
        }
    }
    return false;
}

void DiagnosticBag::add(string code, string path, Span span, string message) {
    Diagnostic item;
    item.code = code;
    item.path = path;
    item.span = span;
    item.message = message;
    items.push_back(item);
}

bool DiagnosticBag::has_code(string_view code) const {
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].code == code) {
            return true;
        }
    }
    return false;
}

const Diagnostic* DiagnosticBag::first() const {
    if (items.empty()) {
        return nullptr;
    }
    return &items[0];
}

} // namespace lucb
