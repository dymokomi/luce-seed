//==============================================================================================
//
//   support/common - Standard-library names used throughout
//
//   DESCRIPTION:
//       The few `std` names brought into `lucb` so headers read `string`, not `std::string`.
//
//==============================================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lucb {

using std::size_t;
using std::string;
using std::string_view;
using std::vector;

} // namespace lucb
