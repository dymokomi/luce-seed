#!/bin/sh
# The gate. Configure, build, run the unit tests. Fail on the first error.
set -eu
cd "$(dirname "$0")"

BUILD_DIR=${BUILD_DIR:-build}
HERE=$(pwd)
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    CACHED=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt" | head -n 1)
    if [ -n "$CACHED" ] && [ "$CACHED" != "$HERE" ]; then
        rm -rf "$BUILD_DIR"
    fi
fi
cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Debug}" \
    -DENABLE_SANITIZERS="${ENABLE_SANITIZERS:-ON}"
cmake --build "$BUILD_DIR" --parallel

"$BUILD_DIR/lucb" --version
"$BUILD_DIR/lucb_tests"
