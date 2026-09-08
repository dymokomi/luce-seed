//==============================================================================================
//
//   support/decimal - Decimal literals to IEEE bits
//
//   DESCRIPTION:
//       Exact decimal to binary conversion with big integers, rounded once.
//
//==============================================================================================

// The literal is a digit string D and a decimal
// exponent E, D * 10^E. With big integers Num and Den, Num = D * 10^E and Den = 1 for
// a non-negative E, else Num = D and Den = 10^-E, so that the value is Num / Den exactly.
// Scaling by a power of two (which only moves the binary exponent) puts the quotient in
// [2^(p+2), 2^(p+4)), with p the width's precision; that quotient, its remainder as a
// sticky bit, and the exponent are then rounded once to the width, ties to even. A
// literal keeps at most 800 significant digits: every float and every midpoint between
// two floats has a decimal expansion shorter than that, so what follows can only decide
// a tie, which the sticky bit records.
#include "support/decimal.h"

#include <cstring>

namespace lucb {

namespace {

// Big unsigned integers of up to 6144 bits, in 32-bit limbs, least significant first.
// The widest value here is Num scaled up to the length of Den = 10^1200, under 4100 bits.
constexpr int kLimbs = 192;

struct Big {
    uint32_t limb[kLimbs];
    int used = 0; // limbs in use; limb[used - 1] != 0, or used == 0 for zero

    void set(uint64_t v) {
        used = 0;
        std::memset(limb, 0, sizeof(limb));
        while (v != 0) {
            limb[used++] = static_cast<uint32_t>(v);
            v >>= 32;
        }
    }
    bool is_zero() const { return used == 0; }
    // this = this * m + a; false when the capacity is exceeded
    bool mul_add(uint32_t m, uint32_t a) {
        uint64_t carry = a;
        for (int i = 0; i < used; i++) {
            uint64_t t = static_cast<uint64_t>(limb[i]) * m + carry;
            limb[i] = static_cast<uint32_t>(t);
            carry = t >> 32;
        }
        if (carry != 0) {
            if (used == kLimbs) {
                return false;
            }
            limb[used++] = static_cast<uint32_t>(carry);
        }
        return true;
    }
    int bit_length() const {
        if (used == 0) {
            return 0;
        }
        uint32_t top = limb[used - 1];
        int n = 0;
        while (top != 0) {
            n++;
            top >>= 1;
        }
        return (used - 1) * 32 + n;
    }
    bool shift_left(int bits) {
        if (bits == 0 || used == 0) {
            return true;
        }
        int words = bits / 32;
        int rest = bits % 32;
        if (used + words + 1 > kLimbs) {
            return false;
        }
        for (int i = used - 1; i >= 0; i--) {
            uint64_t t = static_cast<uint64_t>(limb[i]) << rest;
            limb[i + words + 1] |= static_cast<uint32_t>(t >> 32);
            limb[i + words] = static_cast<uint32_t>(t);
        }
        for (int i = 0; i < words; i++) {
            limb[i] = 0;
        }
        used += words + 1;
        while (used > 0 && limb[used - 1] == 0) {
            used--;
        }
        return true;
    }
    void shift_right_one() {
        for (int i = 0; i < used; i++) {
            uint32_t low = i + 1 < used ? limb[i + 1] & 1u : 0u;
            limb[i] = (limb[i] >> 1) | (low << 31);
        }
        while (used > 0 && limb[used - 1] == 0) {
            used--;
        }
    }
    // this >= other
    bool at_least(const Big& other) const {
        if (used != other.used) {
            return used > other.used;
        }
        for (int i = used - 1; i >= 0; i--) {
            if (limb[i] != other.limb[i]) {
                return limb[i] > other.limb[i];
            }
        }
        return true;
    }
    // this -= other, with this >= other
    void subtract(const Big& other) {
        int64_t borrow = 0;
        for (int i = 0; i < used; i++) {
            int64_t t = static_cast<int64_t>(limb[i]) - (i < other.used ? other.limb[i] : 0) - borrow;
            borrow = t < 0 ? 1 : 0;
            limb[i] = static_cast<uint32_t>(t + (borrow ? (int64_t{1} << 32) : 0));
        }
        while (used > 0 && limb[used - 1] == 0) {
            used--;
        }
    }
};

struct Format {
    int precision;   // significant bits, the hidden one included
    int min_exponent; // of the smallest normal
    int max_exponent; // of the largest finite
    int exponent_bits;
};

bool format_of(int width, Format* f) {
    switch (width) {
    case 64: *f = {53, -1022, 1023, 11}; return true;
    case 32: *f = {24, -126, 127, 8}; return true;
    case 16: *f = {11, -14, 15, 5}; return true;
    default: return false;
    }
}

uint64_t infinity_bits(const Format& f) {
    return ((uint64_t{1} << f.exponent_bits) - 1) << (f.precision - 1);
}

constexpr int kDigitCap = 800;

} // namespace

bool decimal_to_bits(std::string_view text, int width, uint64_t* bits) {
    Format f;
    if (bits == nullptr || !format_of(width, &f)) {
        return false;
    }
    // the significant digits, leading zeros dropped, and the exponent of the last one
    char digits[kDigitCap];
    int count = 0;
    bool sticky = false;
    int64_t exponent = 0; // the decimal exponent of the digit after the last kept one
    bool seen_digit = false;
    bool seen_point = false;
    size_t i = 0;
    for (; i < text.size(); i++) {
        char c = text[i];
        if (c == '_') {
            continue;
        }
        if (c == '.') {
            if (seen_point) {
                return false;
            }
            seen_point = true;
            continue;
        }
        if (c == 'e' || c == 'E') {
            break;
        }
        if (c < '0' || c > '9') {
            return false;
        }
        seen_digit = true;
        if (seen_point) {
            exponent -= 1;
        }
        if (count == 0 && c == '0') {
            continue; // a leading zero has no weight
        }
        if (count < kDigitCap) {
            digits[count++] = c;
        } else {
            // beyond the cap the digit's weight moves into the exponent; its value only
            // matters for a tie
            exponent += 1;
            if (c != '0') {
                sticky = true;
            }
        }
    }
    if (!seen_digit) {
        return false;
    }
    if (i < text.size()) {
        i++; // the `e`
        bool negative = false;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
            negative = text[i] == '-';
            i++;
        }
        if (i == text.size()) {
            return false;
        }
        int64_t given = 0;
        for (; i < text.size(); i++) {
            char c = text[i];
            if (c == '_') {
                continue;
            }
            if (c < '0' || c > '9') {
                return false;
            }
            if (given < 100000) { // any larger exponent is already an overflow or a zero
                given = given * 10 + (c - '0');
            }
        }
        exponent += negative ? -given : given;
    }
    if (count == 0) {
        *bits = 0;
        return true;
    }
    // trailing zeros carry no information: fold them into the exponent
    while (count > 0 && digits[count - 1] == '0') {
        count--;
        exponent += 1;
    }
    // the value is in [10^(count + exponent - 1), 10^(count + exponent)): past the
    // largest double it is an infinity, below half the smallest subnormal it is zero
    if (count + exponent > 310) {
        *bits = infinity_bits(f);
        return true;
    }
    if (count + exponent < -400) {
        *bits = 0;
        return true;
    }
    Big num;
    num.set(0);
    for (int k = 0; k < count; k++) {
        if (!num.mul_add(10, static_cast<uint32_t>(digits[k] - '0'))) {
            return false;
        }
    }
    Big den;
    den.set(1);
    for (int64_t k = 0; k < exponent; k++) {
        if (!num.mul_add(10, 0)) {
            return false;
        }
    }
    for (int64_t k = 0; k < -exponent; k++) {
        if (!den.mul_add(10, 0)) {
            return false;
        }
    }
    // scale so that the quotient has p + 3 or p + 4 bits
    int scale = den.bit_length() - num.bit_length() + f.precision + 3;
    if (scale >= 0) {
        if (!num.shift_left(scale)) {
            return false;
        }
    } else if (!den.shift_left(-scale)) {
        return false;
    }
    // restoring division: the quotient bit by bit, from its highest
    int top = num.bit_length() - den.bit_length();
    if (!den.shift_left(top)) {
        return false;
    }
    uint64_t quotient = 0;
    for (int j = top; j >= 0; j--) {
        quotient <<= 1;
        if (num.at_least(den)) {
            num.subtract(den);
            quotient |= 1;
        }
        den.shift_right_one();
    }
    if (!num.is_zero()) {
        sticky = true;
    }
    // the value is quotient * 2^-scale, and more when sticky
    int length = 0;
    for (uint64_t q = quotient; q != 0; q >>= 1) {
        length++;
    }
    int64_t exponent2 = static_cast<int64_t>(length - 1) - scale; // of the leading bit
    if (exponent2 > f.max_exponent) {
        *bits = infinity_bits(f);
        return true;
    }
    int keep = f.precision;
    if (exponent2 < f.min_exponent) {
        keep = static_cast<int>(f.precision - (f.min_exponent - exponent2));
    }
    if (keep <= 0) {
        // at most half the smallest subnormal: exactly half is a tie, and even is zero
        if (keep == 0) {
            uint64_t half = uint64_t{1} << (length - 1);
            *bits = (quotient > half || (quotient == half && sticky)) ? 1 : 0;
        } else {
            *bits = 0;
        }
        return true;
    }
    int shift = length - keep;
    uint64_t kept = quotient >> shift;
    uint64_t dropped = quotient & ((uint64_t{1} << shift) - 1);
    uint64_t half = uint64_t{1} << (shift - 1);
    if (dropped > half || (dropped == half && (sticky || (kept & 1) != 0))) {
        kept += 1;
    }
    uint64_t hidden = uint64_t{1} << (f.precision - 1);
    if (kept == hidden << 1) {
        kept = hidden;
        exponent2 += 1;
        if (exponent2 > f.max_exponent) {
            *bits = infinity_bits(f);
            return true;
        }
    }
    if (exponent2 < f.min_exponent) {
        // a subnormal's bits are its mantissa; one that rounded up to the hidden bit is
        // the smallest normal, whose bits are that same number
        *bits = kept;
        return true;
    }
    uint64_t bias = (uint64_t{1} << (f.exponent_bits - 1)) - 1;
    *bits = ((static_cast<uint64_t>(exponent2) + bias) << (f.precision - 1)) | (kept - hidden);
    return true;
}

double decimal_to_double(std::string_view text, int width) {
    uint64_t bits = 0;
    if (!decimal_to_bits(text, width, &bits)) {
        return 0.0;
    }
    if (width == 64) {
        double d;
        std::memcpy(&d, &bits, sizeof d);
        return d;
    }
    if (width == 32) {
        uint32_t narrow = static_cast<uint32_t>(bits);
        float s;
        std::memcpy(&s, &narrow, sizeof s);
        return static_cast<double>(s);
    }
    // binary16 unpacked by hand, so the answer does not depend on the host's `_Float16`
    uint64_t exponent = (bits >> 10) & 0x1F;
    uint64_t fraction = bits & 0x3FF;
    if (exponent == 0x1F) {
        uint64_t wide = fraction == 0 ? 0x7FF0000000000000ull : 0x7FF8000000000000ull;
        double d;
        std::memcpy(&d, &wide, sizeof d);
        return d;
    }
    if (exponent == 0) {
        return static_cast<double>(fraction) * 0x1p-24;
    }
    uint64_t wide = ((exponent - 15 + 1023) << 52) | (fraction << 42);
    double d;
    std::memcpy(&d, &wide, sizeof d);
    return d;
}

std::string hex_float(uint64_t bits, int width) {
    int exponent_bits = width == 64 ? 11 : width == 32 ? 8 : 5;
    int fraction_bits = width == 64 ? 52 : width == 32 ? 23 : 10;
    // the fraction padded to whole hex digits: 13, 6, or 3 of them
    int digits = (fraction_bits + 3) / 4;
    int padding = digits * 4 - fraction_bits;
    uint64_t exponent = (bits >> fraction_bits) & ((uint64_t{1} << exponent_bits) - 1);
    uint64_t fraction = (bits & ((uint64_t{1} << fraction_bits) - 1)) << padding;
    int64_t bias = (int64_t{1} << (exponent_bits - 1)) - 1;
    std::string out;
    if (exponent == 0 && fraction == 0) {
        out = "0x0p+0";
    } else {
        out = exponent == 0 ? "0x0." : "0x1.";
        for (int i = digits - 1; i >= 0; i--) {
            uint64_t nibble = (fraction >> (i * 4)) & 15;
            out += static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
        }
        int64_t power = exponent == 0 ? 1 - bias : static_cast<int64_t>(exponent) - bias;
        out += "p";
        if (power >= 0) {
            out += "+";
        }
        out += std::to_string(power);
    }
    if (width == 32) {
        out += "f";
    } else if (width == 16) {
        out += "f16";
    }
    return out;
}

} // namespace lucb
