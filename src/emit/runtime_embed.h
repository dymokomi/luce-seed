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
/* The Base text of a standard module the seed carries as source (`math`), or null. */
const char* lucb_std_source(const char* name);
/* The Base text of the `platform` module (base.md §19.5): the constants of the host the
   seed was compiled on, which is the only target it emits. */
const char* lucb_std_platform();

} // namespace lucb
