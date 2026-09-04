#include "support/literal.h"

namespace lucb {

bool parse_i64_literal(string_view text, int64_t* out) {
    if (text.empty() || out == nullptr) {
        return false;
    }
    int base = 10;
    size_t i = 0;
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'o' || text[1] == 'b')) {
        if (text[1] == 'x') {
            base = 16;
        } else if (text[1] == 'o') {
            base = 8;
        } else {
            base = 2;
        }
        i = 2;
    }

    uint64_t value = 0;
    bool any = false;
    for (; i < text.size(); i++) {
        char c = text[i];
        if (c == '_') {
            continue;
        }
        int digit = -1;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else if (c >= 'a' && c <= 'z') {
            break; // type suffix
        } else {
            return false;
        }
        if (digit < 0 || digit >= base) {
            return false;
        }
        any = true;
        if (value > (UINT64_MAX - static_cast<uint64_t>(digit)) / static_cast<uint64_t>(base)) {
            return false;
        }
        value = value * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit);
    }
    if (!any || value > static_cast<uint64_t>(INT64_MAX)) {
        return false;
    }
    *out = static_cast<int64_t>(value);
    return true;
}

} // namespace lucb
