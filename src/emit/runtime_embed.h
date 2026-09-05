//==============================================================================================
//
//   emit/runtime_embed - The runtime sources embedded in lucb
//
//   DESCRIPTION:
//       `lucb_rt.h`, `lucb_rt.c`, and `start.c` as strings, generated at build time by
//       cmake/embed_runtime.cmake so the binary needs no source tree beside it.
//
//==============================================================================================

#pragma once

namespace lucb {

const char* lucb_rt_h();
const char* lucb_rt_c();
const char* lucb_start_c();

} // namespace lucb
