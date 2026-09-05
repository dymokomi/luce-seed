#!/bin/sh
# Build the release compiler into build/lucb. This is the binary to use for
# real work; ./test.sh builds and runs the sanitized test binary separately.
set -eu
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_SANITIZERS=OFF > /dev/null
cmake --build build --parallel
echo "built build/lucb"
