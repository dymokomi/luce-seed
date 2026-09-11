# Wrap runtime C sources as C++ raw string literals.

if(NOT DEFINED RT_DIR OR NOT DEFINED OUT)
    message(FATAL_ERROR "embed_runtime.cmake needs RT_DIR and OUT")
endif()

file(READ "${RT_DIR}/lucb_rt.h" RT_H)
file(READ "${RT_DIR}/lucb_rt.c" RT_C)
file(READ "${RT_DIR}/start.c" RT_START)
file(READ "${STD_DIR}/math.lucb" STD_MATH)
file(READ "${STD_DIR}/paths.lucb" STD_PATHS)
file(READ "${STD_DIR}/strings.lucb" STD_STRINGS)

set(DELIM "LUCB_EMBED")
foreach(chunk IN ITEMS RT_H RT_C RT_START STD_MATH STD_PATHS STD_STRINGS)
    string(FIND "${${chunk}}" ")${DELIM}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "runtime source contains the embed delimiter")
    endif()
endforeach()

file(WRITE "${OUT}"
"// Generated. Do not edit.
#include \"emit/runtime_embed.h\"
#include <string_view>

namespace lucb {
namespace {

const char k_rt_h[] = R\"${DELIM}(${RT_H})${DELIM}\";
const char k_rt_c[] = R\"${DELIM}(${RT_C})${DELIM}\";
const char k_start[] = R\"${DELIM}(${RT_START})${DELIM}\";
const char k_std_math[] = R\"${DELIM}(${STD_MATH})${DELIM}\";
const char k_std_paths[] = R\"${DELIM}(${STD_PATHS})${DELIM}\";
const char k_std_strings[] = R\"${DELIM}(${STD_STRINGS})${DELIM}\";

} // namespace

const char* lucb_rt_h() { return k_rt_h; }
const char* lucb_rt_c() { return k_rt_c; }
const char* lucb_start_c() { return k_start; }

const char* lucb_std_source(const char* name) {
    if (std::string_view(name) == \"math\") {
        return k_std_math;
    }
    if (std::string_view(name) == \"paths\") {
        return k_std_paths;
    }
    if (std::string_view(name) == \"strings\") {
        return k_std_strings;
    }
    return nullptr;
}

} // namespace lucb
")
