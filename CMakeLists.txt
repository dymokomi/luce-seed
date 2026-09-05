cmake_minimum_required(VERSION 3.20)
project(luce-base-c VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_SANITIZERS "Address and undefined sanitizers" OFF)

function(lucb_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -fno-exceptions -fno-rtti
    )
    if(ENABLE_SANITIZERS)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined)
    endif()
endfunction()

set(LUCB_CORE_SOURCES
    src/support/arena.cpp
    src/support/diagnostics.cpp
    src/source/source.cpp
    src/lex/token.cpp
    src/lex/lexer.cpp
    src/parse/ast.cpp
    src/parse/parser.cpp
    src/parse/decl.cpp
    src/parse/stmt.cpp
    src/parse/expr.cpp
    src/support/literal.cpp
    src/check/type.cpp
    src/check/check.cpp
    src/check/expr.cpp
    src/check/stmt.cpp
    src/check/generic.cpp
    src/interp/interp.cpp
    src/interp/eval.cpp
    src/interp/exec.cpp
    src/emit/cgen.cpp
    src/emit/emit.cpp
    src/emit/expr.cpp
    src/emit/stmt.cpp
    src/emit/host.cpp
    src/pkg/package.cpp
)

add_library(lucb_core STATIC ${LUCB_CORE_SOURCES})
target_include_directories(lucb_core PUBLIC src)
target_compile_definitions(lucb_core PUBLIC
    LUCB_RUNTIME_DIR="${CMAKE_SOURCE_DIR}/src/runtime")
lucb_warnings(lucb_core)

add_executable(lucb src/main.cpp)
target_link_libraries(lucb PRIVATE lucb_core)
lucb_warnings(lucb)

add_executable(lucb_tests
    tests/harness.cpp
    tests/source_test.cpp
    tests/lex_test.cpp
    tests/parse_test.cpp
    tests/check_test.cpp
    tests/eval_test.cpp
    tests/agree_test.cpp
    tests/pkg_test.cpp
)
target_link_libraries(lucb_tests PRIVATE lucb_core)
target_include_directories(lucb_tests PRIVATE src tests)
lucb_warnings(lucb_tests)
