# Continuous correctness checks

The `Correctness` workflow runs on every push, pull request, manual dispatch and weekly
schedule, on macOS ARM64 and Linux x86-64. The job asserts its actual architecture and
retains toolchain versions, source revisions, dependency pins, complete gate logs and
failure replay records. Matrix jobs finish independently so a failing host cannot hide
another host's result.

Use `./test.sh` locally. Hosted jobs have a 90-minute deadline. Base and Luce conformance
commands each have a 60-second deadline, kill their subprocess groups on timeout, and
require exact success, rejection or trap statuses. A matching message does not excuse a
signal or timeout. Replay records under `build/failures/` name the command, working
directory, source revision and outputs.

Base's ordinary conformance gate runs C default, C release, native levels 0–3 and the
pinned seed oracle where applicable. Both optimized and unoptimized trap programs run.
The seed oracle is required for the full gate; it must not silently disappear.

Hosted Macs may have no Metal device. CI explicitly sets `LUCE_TEST_GPU=optional`: the
Metal program still compiles, and absence of a device is recorded as missing hardware
evidence. All other failures remain failures. The default local gate still requires a
Metal device on macOS. Release evidence must include both hosted jobs and the full
hardware gate on a capable Mac; a hosted green check alone does not establish GPU support.

Runner labels follow [GitHub's runner documentation](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).
