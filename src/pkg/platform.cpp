//==============================================================================================
//
//   pkg/platform - The `platform` standard module
//
//   DESCRIPTION:
//       base.md §19.5: the compiler writes the `platform` module for the build, whose
//       constants the standard library and `os` branch on. The seed compiles for its host
//       only, so the module is decided here, when the seed itself is compiled.
//
//==============================================================================================

#include "emit/runtime_embed.h"

namespace lucb {

#if defined(__APPLE__)
#define LUCB_OS "macos"
#elif defined(_WIN32)
#define LUCB_OS "windows"
#else
#define LUCB_OS "linux"
#endif

#if defined(__aarch64__)
#define LUCB_ARCH "arm64"
#else
#define LUCB_ARCH "x86_64"
#endif

const char* lucb_std_platform() {
    return "## The target this program is compiled for (§19.5): the seed's host.\n"
           "pub let name: str = \"" LUCB_ARCH "-" LUCB_OS "\"\n"
#if defined(__APPLE__)
           "pub let macos: bool = true\npub let linux: bool = false\npub let windows: bool = false\npub let posix: bool = true\n"
#elif defined(_WIN32)
           "pub let macos: bool = false\npub let linux: bool = false\npub let windows: bool = true\npub let posix: bool = false\n"
#else
           "pub let macos: bool = false\npub let linux: bool = true\npub let windows: bool = false\npub let posix: bool = true\n"
#endif
#if defined(__aarch64__)
           "pub let arm64: bool = true\npub let x86_64: bool = false\n"
#else
           "pub let arm64: bool = false\npub let x86_64: bool = true\n"
#endif
           "pub let pointer_bits: u32 = 64\n";
}

} // namespace lucb
