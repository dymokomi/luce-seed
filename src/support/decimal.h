//==============================================================================================
//
//   support/decimal - Decimal literals to IEEE bits
//
//   DESCRIPTION:
//       The value of a decimal floating-point literal, correctly rounded to one IEEE
//       width (base.md §4.3): the text is converted exactly, with one rounding, ties to
//       even, into binary16, binary32, or binary64, subnormals and the overflow to
//       infinity included.
//
//==============================================================================================

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lucb {

// The bits of `text` (digits, one optional point, an optional exponent, `_` separators,
// no sign and no suffix) as a float of `width` bits, 16, 32, or 64. False when the text
// is not such a literal.
bool decimal_to_bits(std::string_view text, int width, uint64_t* bits);

// The same value as a double, exact since every narrower float is a double; the width
// decides the rounding, so an `f32` literal is rounded once, to `f32`.
double decimal_to_double(std::string_view text, int width);

// The bits of a float of `width` bits as C's hexadecimal literal, `0x1.8p+1f`: exact,
// a subnormal included, with `f` or `f16` naming the narrower widths.
std::string hex_float(uint64_t bits, int width);

// A float field's text in a formatted string (base.md §14): the shortest decimal that
// reads back as the value, the closest of those, laid out as double-conversion's
// ToShortest (decimal notation for a decimal exponent from -6 to 14, else `1.5e-7`,
// `1e15`), a `.0` after digits with no point (`5.0`, `-0.0`), and `inf`, `-inf`, `nan`.
// `width` 64 takes the double's digits; 32 and 16 the float's (an f16 shows its value's
// shortest f32 digits).
std::string float_display(double value, int width);

} // namespace lucb
