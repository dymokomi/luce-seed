// Direct process execution must preserve arguments and drain both streams.
#include "emit/host.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <io.h>
#include <fcntl.h>

int main(int argc, char** argv) {
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    if (argc > 1 && std::strcmp(argv[1], "--child") == 0) {
        for (int i = 2; i < argc; ++i) {
            std::printf("%zu:%s\n", std::strlen(argv[i]), argv[i]);
        }
        const std::string bytes(131072, 'x');
        std::fwrite(bytes.data(), 1, bytes.size(), stdout);
        std::fwrite(bytes.data(), 1, bytes.size(), stderr);
        return 37;
    }
    lucb::ScratchDir scratch;
    if (!scratch.ok()) return 1;
    const auto directory = std::filesystem::path(scratch.path) / "a directory with spaces";
    std::error_code error;
    std::filesystem::create_directory(directory, error);
    if (error) return 2;
    const auto executable = directory / "process fixture.exe";
    std::filesystem::copy_file(argv[0], executable, error);
    if (error) return 3;
    const lucb::vector<lucb::string> args = {"--child", "", "two words", "quote\"inside",
        "trailing\\", "two\\\\\"quotes", "& | > < %PATH% $(anything) `anything`"};
    std::string expected;
    for (size_t i = 1; i < args.size(); ++i)
        expected += std::to_string(args[i].size()) + ":" + args[i] + "\n";
    expected += std::string(131072, 'x');
    auto result = lucb::run_exe(executable.string(), args);
    if (result.exit_code != 37 || result.out != expected || result.err != std::string(131072, 'x')) {
        std::fprintf(stderr, "argument/capture test failed: exit %d, stdout %zu, stderr %zu\n",
                     result.exit_code, result.out.size(), result.err.size());
        return 4;
    }
    result = lucb::run_exe((directory / "missing.exe").string());
    if (result.exit_code != 127 || result.err.empty()) return 5;
    std::puts("Windows process arguments, dual-stream capture, exit status and missing executable passed");
    return 0;
}
