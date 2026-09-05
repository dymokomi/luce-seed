# Wrap runtime C sources as C++ raw string literals.

if(NOT DEFINED RT_DIR OR NOT DEFINED OUT)
    message(FATAL_ERROR "embed_runtime.cmake needs RT_DIR and OUT")
endif()

file(READ "${RT_DIR}/lucb_rt.h" RT_H)
file(READ "${RT_DIR}/lucb_rt.c" RT_C)
file(READ "${RT_DIR}/start.c" RT_START)

set(DELIM "LUCB_EMBED")
foreach(chunk IN ITEMS RT_H RT_C RT_START)
    string(FIND "${${chunk}}" ")${DELIM}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "runtime source contains the embed delimiter")
    endif()
endforeach()

file(WRITE "${OUT}"
"// Generated. Do not edit.
#include \"emit/runtime_embed.h\"

namespace lucb {
namespace {

const char k_rt_h[] = R\"${DELIM}(${RT_H})${DELIM}\";
const char k_rt_c[] = R\"${DELIM}(${RT_C})${DELIM}\";
const char k_start[] = R\"${DELIM}(${RT_START})${DELIM}\";

} // namespace

const char* lucb_rt_h() { return k_rt_h; }
const char* lucb_rt_c() { return k_rt_c; }
const char* lucb_start_c() { return k_start; }

} // namespace lucb
")
