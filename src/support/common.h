// Names we actually use from the standard library, brought into `lucb`.
// Headers then write `string` not `std::string`. Do not `using namespace std`
// in a header — that leaks into every file that includes it.

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
