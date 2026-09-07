//==============================================================================================
//
//   interp/punning - Union reinterpretation for the interpreter
//
//   DESCRIPTION:
//       The interpreter models memory as typed values, not bytes. A union reads a member
//       other than the one last written by reinterpreting bytes (base.md §10.4), so the
//       union's members are kept in step through the target's byte layout: when a member
//       place is reached and another member was the last one touched, that member is
//       encoded into bytes and the requested member decoded from them. Members that have
//       no byte encoding in this model (pointers, views, optionals) are left as they are.
//
//==============================================================================================

#pragma once

#include "interp/value.h"

namespace lucb {

// The place of member `index` of the union `u`, brought up to date with the member last
// reached through this function.
Value* union_member(Value& u, int index);

// A value's bytes in the target's layout, and a value from them: what `memory.read[T]` and
// `memory.write[T]` move across cells narrower than `T`.
bool encode_value(const Value& v, const Type* t, uint8_t* out);
bool decode_value(Value& v, const Type* t, const uint8_t* in);

// The IEEE bits of `f` at the width of the float type `t`, and the float those bits spell.
uint64_t float_to_bits(double f, TypeKind k);
double float_from_bits(uint64_t bits, TypeKind k);

} // namespace lucb
