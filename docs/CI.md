# Continuous correctness checks

The `Correctness` workflow runs on every push, pull request, manual dispatch and weekly
schedule, on macOS ARM64 and Linux x86-64. Each job asserts its actual architecture,
records the toolchain (`tools/ci_provenance.py`), runs `./test.sh`, the sanitised debug
build and the unit tests, and keeps the whole log. The two hosts finish independently, so
one host's failure cannot hide the other's result.

Use `./test.sh` locally; it is the same gate. Hosted jobs have a 90-minute deadline.

Runner labels follow [GitHub's runner documentation](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).
