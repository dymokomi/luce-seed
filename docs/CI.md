# Continuous correctness checks

The seed is tested by the three-platform gate in luce-base (`tools/gate.py`, described in
[luce-base's docs/CI.md](https://github.com/dymokomi/luce-base/blob/main/docs/CI.md)).
`gate.toml` names what it runs here: `./test.sh` on macOS and Linux (the sanitised debug
build and the unit tests), and the CMake build and its tests on Windows.

Use `./test.sh` locally; it is the same check.
