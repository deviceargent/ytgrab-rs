#include "mux.hpp"

#include <windows.h>

#include <iterator>
#include <vector>

#include "error.hpp"
#include "util.hpp"

namespace ytgrab::mux {
namespace {

std::wstring quote_arg(const std::wstring& arg) {    if (arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (auto it = arg.begin();; ++it) {
        unsigned backslashes = 0;
        while (it != arg.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == arg.end()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out += L'"';
        } else {
            out.append(backslashes, L'\\');
            out += *it;
        }
    }
    out += L'"';
    return out;
}

struct ProcessResult {
    uint32_t exit_code = 1;
    std::string output;
};

/// CreateProcessW does not search PATH (and does not append `.exe`) when the
/// application name is given explicitly, so resolve it the way a shell would.
std::wstring resolve_executable(const std::wstring& name) {
    if (name.find_first_of(L"\\/:") != std::wstring::npos) return name;

    wchar_t buffer[MAX_PATH * 4];
    DWORD len = SearchPathW(nullptr, name.c_str(), L".exe", static_cast<DWORD>(std::size(buffer)),
                            buffer, nullptr);
    if (len > 0 && len < std::size(buffer)) return std::wstring(buffer, len);
    len = SearchPathW(nullptr, name.c_str(), nullptr, static_cast<DWORD>(std::size(buffer)),
                      buffer, nullptr);
    if (len > 0 && len < std::size(buffer)) return std::wstring(buffer, len);

    throw Error::ffmpeg(util::narrow(name) + " was not found on PATH");
}

ProcessResult run_process(const std::wstring& exe, const std::vector<std::wstring>& args,
                          bool capture_stdout) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_end = nullptr;
    HANDLE write_end = nullptr;
    if (!CreatePipe(&read_end, &write_end, &sa, 0)) {
        throw Error::ffmpeg("could not create a pipe");
    }
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

    std::wstring resolved = resolve_executable(exe);
    std::wstring command = quote_arg(resolved);
    for (const auto& arg : args) command += L" " + quote_arg(arg);
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = write_end;
    si.hStdError = write_end;

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(resolved.c_str(), buffer.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(write_end);
    if (!ok) {
        CloseHandle(read_end);
        throw Error::ffmpeg("could not start " + util::narrow(exe));
    }

    ProcessResult result;
    char chunk[4096];
    DWORD read = 0;
    while (ReadFile(read_end, chunk, sizeof(chunk), &read, nullptr) && read > 0) {
        result.output.append(chunk, read);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    result.exit_code = code;

    CloseHandle(read_end);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    (void)capture_stdout;
    return result;
}

void run_ffmpeg(std::vector<std::wstring> args) {
    ProcessResult result = run_process(L"ffmpeg", args, false);
    if (result.exit_code != 0) {
        throw Error::ffmpeg(util::trim(result.output));
    }
}

std::wstring w(const std::string& s) { return util::widen(s); }

}  // namespace

bool ffmpeg_available() {
    try {
        ProcessResult result = run_process(L"ffmpeg", {L"-version"}, true);
        return result.exit_code == 0;
    } catch (...) {
        return false;
    }
}

void merge(const fs::path& video, const fs::path& audio, const fs::path& out, const Meta& meta) {
    std::vector<std::wstring> args = {L"-hide_banner", L"-loglevel", L"error", L"-y",
                                      L"-i",          video.wstring(), L"-i", audio.wstring(),
                                      L"-map",        L"0:v:0",        L"-map",
                                      L"1:a:0",       L"-c",           L"copy"};

    if (!meta.title.empty()) {
        args.push_back(L"-metadata");
        args.push_back(w("title=" + meta.title));
    }
    if (meta.artist) {
        args.push_back(L"-metadata");
        args.push_back(w("artist=" + *meta.artist));
    }
    if (meta.description) {
        args.push_back(L"-metadata");
        args.push_back(w("description=" + *meta.description));
    }
    if (meta.webpage_url) {
        args.push_back(L"-metadata");
        args.push_back(w("comment=" + *meta.webpage_url));
    }
    args.push_back(out.wstring());
    run_ffmpeg(std::move(args));
}

void extract_audio(const fs::path& input, const fs::path& out, const std::string& codec,
                   const std::optional<std::string>& bitrate) {
    std::vector<std::wstring> args = {L"-hide_banner", L"-loglevel", L"error", L"-y",
                                      L"-i",          input.wstring(), L"-vn",
                                      L"-c:a",        w(codec)};
    if (bitrate) {
        args.push_back(L"-b:a");
        args.push_back(w(*bitrate));
    }
    args.push_back(out.wstring());
    run_ffmpeg(std::move(args));
}

void remux(const fs::path& input, const fs::path& out, const Meta& meta) {
    std::vector<std::wstring> args = {L"-hide_banner", L"-loglevel", L"error", L"-y",
                                      L"-i",          input.wstring(), L"-c",
                                      L"copy"};
    if (!meta.title.empty()) {
        args.push_back(L"-metadata");
        args.push_back(w("title=" + meta.title));
    }
    args.push_back(out.wstring());
    run_ffmpeg(std::move(args));
}

}  // namespace ytgrab::mux
