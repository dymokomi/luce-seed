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

void DiagnosticBag::assembled(const string& path, const vector<Segment>& segments) {
    modules.push_back(Assembled{path, segments});
}

void DiagnosticBag::locate(Diagnostic& item) const {
    for (const Assembled& module : modules) {
        if (module.path != item.path || module.segments.empty()) {
            continue;
        }
        const Segment* found = &module.segments[0];
        for (const Segment& segment : module.segments) {
            if (segment.start_line <= item.span.line) {
                found = &segment;
            }
        }
        item.path = found->path;
        item.span.line = item.span.line - found->start_line + 1;
        return;
    }
}

void DiagnosticBag::warn(string code, string path, Span span, string message) {
    Diagnostic item;
    item.code = code;
    item.path = path;
    item.span = span;
    item.message = message;
    item.warning = true;
    locate(item);
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
    locate(item);
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
