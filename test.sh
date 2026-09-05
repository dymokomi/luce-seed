#!/bin/sh
# The gate. Configure a sanitized debug build in build-test/, build it, run
# the unit tests. Fail on the first error. ./build.sh makes the fast binary.
set -eu
cd "$(dirname "$0")"

BUILD_DIR=${BUILD_DIR:-build-test}
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
