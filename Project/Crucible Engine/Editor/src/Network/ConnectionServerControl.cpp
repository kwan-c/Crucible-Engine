/*!
* @file			ConnectionServerControl.cpp
* @author		Abhishek Akilandeswari Patchamuthu
* @par			a.patchamuthu@digipen.edu
* @date			22 November 2026
*
* @brief		ConnectionServerControl (see the header): the Editor's handle on the
*				connection server process.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "Network/ConnectionServerControl.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace Crucible {

namespace {
    std::uint64_t CreationTime(HANDLE process) {
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return 0;
        return (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    }

    std::filesystem::path ImagePath(HANDLE process) {
        std::wstring buffer(MAX_PATH * 2, L'\0');
        DWORD size = static_cast<DWORD>(buffer.size());
        if (!QueryFullProcessImageNameW(process, 0, buffer.data(), &size)) return {};
        buffer.resize(size);
        return std::filesystem::path(buffer);
    }

    bool SameFile(const std::filesystem::path& a, const std::filesystem::path& b) {
        std::error_code ec;
        if (std::filesystem::equivalent(a, b, ec)) return true;
        std::wstring x = a.lexically_normal().wstring(), y = b.lexically_normal().wstring();
        auto lower = [](std::wstring& s) { for (wchar_t& c : s) c = static_cast<wchar_t>(std::towlower(c)); };
        lower(x);
        lower(y);
        return x == y;
    }
}

std::filesystem::path ConnectionServerControl::FindExecutable(const std::filesystem::path& editorDir, const std::filesystem::path& buildDir) {
    std::vector<std::filesystem::path> candidates;
    if (!editorDir.empty()) candidates.push_back(editorDir / kExeName);
    if (!buildDir.empty()) {
#ifdef _DEBUG
        const char* configs[] = { "Debug", "Release", "RelWithDebInfo", "MinSizeRel" };
#else
        const char* configs[] = { "Release", "RelWithDebInfo", "MinSizeRel", "Debug" };
#endif
        for (const char* config : configs) candidates.push_back(buildDir / "ConnectionServer" / config / kExeName);
    }
    std::error_code ec;
    for (const auto& candidate : candidates)
        if (std::filesystem::is_regular_file(candidate, ec)) return candidate.lexically_normal().make_preferred();
    return {};
}

std::filesystem::path ConnectionServerControl::ThisProgramDir() {
    std::wstring buffer(MAX_PATH * 2, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (n == 0 || n >= buffer.size()) return {};
    buffer.resize(n);
    return std::filesystem::path(buffer).parent_path();
}

ConnectionServerControl::ConnectionServerControl(std::filesystem::path executable)
    : m_exe(std::move(executable)) {}

ConnectionServerControl::~ConnectionServerControl() {
    Detach();   // the server outlives the Editor
}

void ConnectionServerControl::SetExecutable(std::filesystem::path executable) {
    if (IsRunning()) return;   // the running process keeps its own files
    m_exe = std::move(executable);
}

bool ConnectionServerControl::HasExecutable() const {
    std::error_code ec;
    return !m_exe.empty() && std::filesystem::is_regular_file(m_exe, ec);
}

std::filesystem::path ConnectionServerControl::LogPath() const {
    return m_exe.empty() ? std::filesystem::path() : m_exe.parent_path() / "CrucibleConnectionServer.log";
}

std::filesystem::path ConnectionServerControl::PidPath() const {
    return m_exe.empty() ? std::filesystem::path() : m_exe.parent_path() / "CrucibleConnectionServer.pid";
}

bool ConnectionServerControl::Start(const Options& options, std::string& error) {
    if (IsRunning()) { error = "The connection server is already running (process " + std::to_string(m_pid) + ")."; return false; }
    if (!HasExecutable()) {
        error = "CrucibleConnectionServer.exe was not found. Build the CrucibleConnectionServer project.";
        return false;
    }
    if (options.port == 0) { error = "The server port must be 1 to 65535."; return false; }
    if (options.lifetimeMs < 3000 || options.lifetimeMs > 60000) { error = "The lobby lifetime must be 3000 to 60000 ms."; return false; }
    Release();

    // The log: written by the server, read by the Window. Inheritable, and the ONLY inherited
    // handle (PROC_THREAD_ATTRIBUTE_HANDLE_LIST).
    SECURITY_ATTRIBUTES inheritable{ sizeof(inheritable), nullptr, TRUE };
    HANDLE log = CreateFileW(LogPath().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &inheritable,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) { error = "Cannot write the server log next to the exe (" + LogPath().string() + ")."; return false; }

    SIZE_T attributeBytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
    std::vector<unsigned char> attributes(attributeBytes);
    auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    InitializeProcThreadAttributeList(list, 1, 0, &attributeBytes);
    UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &log, sizeof(log), nullptr, nullptr);

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = nullptr;
    startup.StartupInfo.hStdOutput = log;
    startup.StartupInfo.hStdError = log;
    startup.lpAttributeList = list;

    std::wstring command = L"\"" + m_exe.wstring() + L"\" --port " + std::to_wstring(options.port) +
                           L" --lifetime " + std::to_wstring(options.lifetimeMs);
    const std::wstring directory = m_exe.parent_path().wstring();
    // Hidden, its own process group, and out of the Editor's job if it has one (so closing the
    // Editor - or a debugger that killed it - does not take the server down). Not every job
    // allows leaving it: then it is started inside.
    DWORD flags = CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | EXTENDED_STARTUPINFO_PRESENT;
    PROCESS_INFORMATION info{};
    BOOL ok = CreateProcessW(m_exe.c_str(), command.data(), nullptr, nullptr, TRUE, flags | CREATE_BREAKAWAY_FROM_JOB,
                             nullptr, directory.c_str(), &startup.StartupInfo, &info);
    if (!ok && GetLastError() == ERROR_ACCESS_DENIED)
        ok = CreateProcessW(m_exe.c_str(), command.data(), nullptr, nullptr, TRUE, flags, nullptr, directory.c_str(), &startup.StartupInfo, &info);
    const DWORD failure = ok ? 0 : GetLastError();
    DeleteProcThreadAttributeList(list);
    CloseHandle(log);
    if (!ok) {
        error = "Windows could not start the connection server (error " + std::to_string(failure) + ").";
        return false;
    }
    CloseHandle(info.hThread);
    Adopt(info.hProcess, info.dwProcessId, options, false);
    WritePidFile(CreationTime(info.hProcess));
    return true;
}

void ConnectionServerControl::Stop() {
    if (m_process == nullptr) return;
    HANDLE process = static_cast<HANDLE>(m_process);
    if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
        TerminateProcess(process, 0);
        WaitForSingleObject(process, 3000);
    }
    DeletePidFile();
    Release();
    m_state = State::Stopped;
    m_exitCode = 0;
}

bool ConnectionServerControl::Reattach() {
    if (IsRunning()) return true;
    std::ifstream file(PidPath());
    if (!file) return false;
    std::uint32_t pid = 0;
    std::uint64_t createdAt = 0;
    Options options;
    unsigned port = 0;
    if (!(file >> pid >> port >> options.lifetimeMs >> createdAt) || pid == 0 || port == 0 || port > 65535) {
        file.close();
        DeletePidFile();
        return false;
    }
    file.close();
    options.port = static_cast<std::uint16_t>(port);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    const bool same = process != nullptr && WaitForSingleObject(process, 0) == WAIT_TIMEOUT &&
                      CreationTime(process) == createdAt && SameFile(ImagePath(process), m_exe);
    if (!same) {
        if (process != nullptr) CloseHandle(process);
        DeletePidFile();   // that server is gone (or the id now belongs to something else)
        return false;
    }
    Adopt(process, pid, options, true);
    return true;
}

void ConnectionServerControl::Detach() noexcept {
    Release();
    if (m_state == State::Running) m_state = State::Stopped;
}

void ConnectionServerControl::Adopt(void* process, std::uint32_t pid, const Options& options, bool reattached) {
    m_process = process;
    m_pid = pid;
    m_options = options;
    m_state = State::Running;
    m_exitCode = 0;
    m_reattached = reattached;
    m_logTail.clear();
    m_nextLogRead = {};
    m_monitor.SetServer("127.0.0.1", options.port);
    m_monitor.StartMonitoring();
}

void ConnectionServerControl::Release() noexcept {
    m_monitor.StopMonitoring();
    if (m_process != nullptr) CloseHandle(static_cast<HANDLE>(m_process));
    m_process = nullptr;
    m_pid = 0;
}

void ConnectionServerControl::Update(Clock::time_point now) {
    if (m_process != nullptr && WaitForSingleObject(static_cast<HANDLE>(m_process), 0) == WAIT_OBJECT_0) {
        DWORD code = 0;
        GetExitCodeProcess(static_cast<HANDLE>(m_process), &code);
        m_exitCode = code;
        DeletePidFile();
        Release();
        m_state = State::Exited;
        ReadLog();   // its last words ("UDP port 27018 is in use ...")
    }
    if (m_state == State::Running) {
        m_monitor.Pump(now);
        if (now >= m_nextLogRead) {
            ReadLog();
            m_nextLogRead = now + std::chrono::milliseconds(500);
        }
    }
}

LobbyServerStatus ConnectionServerControl::MonitorState(Clock::time_point now) const noexcept {
    return m_state == State::Running ? m_monitor.Status(now) : LobbyServerStatus::Off;
}

bool ConnectionServerControl::Verified() const noexcept {
    return m_state == State::Running && m_monitor.HasServerStatus() && m_monitor.ServerStatus().processId == m_pid;
}

void ConnectionServerControl::ReadLog() {
    constexpr std::streamoff kTail = 8 * 1024;
    std::ifstream file(LogPath(), std::ios::binary);
    if (!file) return;
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    const std::streamoff from = std::max<std::streamoff>(0, size - kTail);
    file.seekg(from);
    std::string text(static_cast<std::size_t>(size - from), '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(file.gcount()));
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    bool first = from > 0;   // a cut first line is dropped
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first) { first = false; continue; }
        lines.push_back(std::move(line));
    }
    constexpr std::size_t kKeep = 200;
    if (lines.size() > kKeep) lines.erase(lines.begin(), lines.end() - kKeep);
    m_logTail = std::move(lines);
}

void ConnectionServerControl::WritePidFile(std::uint64_t createdAt) const {
    std::ofstream file(PidPath(), std::ios::trunc);
    file << m_pid << ' ' << m_options.port << ' ' << m_options.lifetimeMs << ' ' << createdAt << '\n';
}

void ConnectionServerControl::DeletePidFile() const noexcept {
    std::error_code ec;
    if (!m_exe.empty()) std::filesystem::remove(PidPath(), ec);
}

} // namespace Crucible
