// Windows host tools. Invoke executables directly; arguments never pass through cmd.exe.
#include "emit/host.h"
#include "emit/runtime_embed.h"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace lucb {
namespace {
std::wstring wide(const string& text) {
    if (text.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                               static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(n, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                             static_cast<int>(text.size()), out.data(), n);
    return out;
}

std::wstring quote(const string& text) {
    if (!text.empty() && text.find_first_of(" \t\r\n\"") == string::npos) return wide(text);
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : wide(text)) {
        if (ch == L'\\') { ++slashes; continue; }
        out.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        out += ch;
        slashes = 0;
    }
    out.append(slashes * 2, L'\\');
    return out + L'"';
}

string slurp(const string& path) {
    std::ifstream in(std::filesystem::path(wide(path)), std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

bool write_text(const string& path, const char* text, string* error) {
    std::ofstream out(std::filesystem::path(wide(path)), std::ios::binary);
    out << text;
    if (out) return true;
    if (error) *error = "could not write " + path;
    return false;
}

bool compile(const string& source, const string& output, string* error,
             bool start, bool release, const NativeInputs* native, bool object) {
    ScratchDir scratch;
    if (!scratch.ok()) {
        if (error) *error = "could not create a temporary directory";
        return false;
    }
    const string& root = scratch.path;
    if (!write_text(root + "/gen.c", source.c_str(), error) ||
        !write_text(root + "/lucb_rt.h", lucb_rt_h(), error) ||
        !write_text(root + "/lucb_rt.c", lucb_rt_c(), error)) return false;
    vector<string> args = {"-std=gnu11", "-Wall", "-Werror", "-fno-strict-aliasing",
                           release ? "-O2" : "-O0", "-I", root, root + "/gen.c"};
    if (object) args.push_back("-c");
    else {
        args.push_back(root + "/lucb_rt.c");
        if (start) {
            if (!write_text(root + "/start.c", lucb_start_c(), error)) return false;
            args.push_back(root + "/start.c");
        }
        if (native) {
            for (const auto& s : native->sources) args.push_back(native->root + "/" + s);
            for (const auto& s : native->link_search) args.push_back("-L" + s);
            for (const auto& s : native->libraries) args.push_back("-l" + s);
            if (!native->frameworks.empty()) {
                if (error) *error = "Apple frameworks are unavailable on Windows";
                return false;
            }
            for (const auto& s : native->pkg_config) {
                auto flags = run_exe("pkg-config", {"--cflags", "--libs", s});
                if (flags.exit_code != 0) {
                    if (error) *error = flags.err;
                    return false;
                }
                // pkg-config shell quoting needs an explicit parser before it is supported.
                if (error) *error = "pkg-config inputs on Windows are not implemented";
                return false;
            }
        }
        args.push_back("-lm");
        args.push_back("-pthread");
    }
    args.push_back("-o");
    args.push_back(object ? root + "/gen.o" : output);
    const char* cc = std::getenv("CC");
    auto result = run_exe(cc && *cc ? cc : "gcc", args);
    if (result.exit_code == 0) return true;
    if (error) *error = result.err.empty() ? "C compile failed" : result.err;
    return false;
}
}

ScratchDir::ScratchDir() {
    std::error_code error;
    auto base = std::filesystem::temp_directory_path(error);
    if (error) return;
    static std::atomic<unsigned long> serial{0};
    for (int attempt = 0; attempt < 128; ++attempt) {
        auto candidate = base / ("lucb-" + std::to_string(GetCurrentProcessId()) + "-" +
                                std::to_string(GetTickCount64()) + "-" + std::to_string(serial++));
        if (std::filesystem::create_directory(candidate, error)) {
            auto utf8 = candidate.generic_u8string();
            path.assign(reinterpret_cast<const char*>(utf8.data()), utf8.size());
            return;
        }
    }
}

ScratchDir::~ScratchDir() {
    if (!path.empty()) {
        std::error_code error;
        std::filesystem::remove_all(std::filesystem::path(wide(path)), error);
    }
}

RunResult run_exe(const string& executable, const vector<string>& args) {
    RunResult result;
    ScratchDir scratch;
    if (!scratch.ok()) { result.err = "could not create a temporary directory"; return result; }
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    string out_path = scratch.path + "/stdout";
    string err_path = scratch.path + "/stderr";
    HANDLE out = CreateFileW(wide(out_path).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                             &security, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    HANDLE err = CreateFileW(wide(err_path).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                             &security, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &security, OPEN_EXISTING, 0, nullptr);
    if (out == INVALID_HANDLE_VALUE || err == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
        if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
        if (err != INVALID_HANDLE_VALUE) CloseHandle(err);
        if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
        result.err = "could not open child streams";
        return result;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = out;
    startup.hStdError = err;
    startup.hStdInput = input;
    PROCESS_INFORMATION child{};
    auto command = quote(executable);
    for (const auto& arg : args) command += L" " + quote(arg);
    BOOL launched = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child);
    DWORD failure = GetLastError();
    CloseHandle(out);
    CloseHandle(err);
    CloseHandle(input);
    if (!launched) {
        result.exit_code = 127;
        result.err = "could not launch " + executable + " (Windows error " + std::to_string(failure) + ")";
        return result;
    }
    DWORD code = 1;
    if (WaitForSingleObject(child.hProcess, INFINITE) == WAIT_OBJECT_0)
        GetExitCodeProcess(child.hProcess, &code);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    result.exit_code = static_cast<int>(code);
    result.out = slurp(out_path);
    result.err = slurp(err_path);
    return result;
}

bool compile_c(const string& source, const string& output, string* error,
               bool start, bool release, const NativeInputs* native) {
    return compile(source, output, error, start, release, native, false);
}

bool compile_c_object(const string& source, string* error) {
    return compile(source, "", error, false, false, nullptr, true);
}
} // namespace lucb
