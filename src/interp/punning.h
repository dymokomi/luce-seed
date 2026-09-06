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

} // namespace lucb
