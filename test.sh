#!/bin/sh
# The gate. Configure, build, run the unit tests. Fail on the first error.
set -eu
cd "$(dirname "$0")"

BUILD_DIR=${BUILD_DIR:-build}
cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Debug}" \
    -DENABLE_SANITIZERS="${ENABLE_SANITIZERS:-ON}"
cmake --build "$BUILD_DIR" --parallel

"$BUILD_DIR/lucb" --version
"$BUILD_DIR/lucb_tests"
