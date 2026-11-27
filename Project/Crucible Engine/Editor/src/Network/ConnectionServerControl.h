/*!
* @file			ConnectionServerControl.h
* @author		Abhishek Akilandeswari Patchamuthu
* @par			a.patchamuthu@digipen.edu
* @date			22 November 2026
*
* @brief		Starts, watches and stops the LAN connection server (ConnectionServer/,
*				CrucibleConnectionServer.exe) from the Editor: Network > Connection Server.
*
* The server stays its own program. The Editor only launches it, reads its log and asks it how it
* is (LobbyServer StatusRequest); closing the Editor leaves it running, and the next Editor finds
* it again (Reattach). Nothing in a game session depends on any of this.
*
*   Start    launches the exe hidden (no console window), its output going to
*            CrucibleConnectionServer.log next to the exe, and writes CrucibleConnectionServer.pid
*            (process id, port, lifetime, start time). Only the log handle is inherited: the
*            Editor's own sockets (a GNS listen socket on the game port) must not be, or the server
*            would keep that port bound after the Editor's session ends.
*   Reattach reads the .pid file and takes the process back only if it is still running AND is
*            the same program started at the same time (a reused process id is not mistaken for
*            the server). A stale file is deleted.
*   Stop     ends the process (TerminateProcess). Nothing is lost: the server keeps lobbies only
*            in memory, and hosts register again when one is running.
*   Update   notices the process exiting (for example "UDP port in use"), keeps the log's last
*            lines, and pumps a status monitor on 127.0.0.1:<port>. Verified() is true once the
*            server that answers there is this very process.
*
* Windows only (like the rest of the engine). No ImGui: the Network window draws it, and the
* tests drive the real exe through it.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Network/LobbyServerClient.h"

namespace Crucible {

    class ConnectionServerControl
    {
    public:
        using Clock = std::chrono::steady_clock;

        enum class State : std::uint8_t
        {
            Stopped,   // not running (never started, stopped, or not found)
            Running,   // our process is alive
            Exited,    // it ended by itself (ExitCode(), and the log says why)
        };

        struct Options
        {
            std::uint16_t port = LobbyServer::kDefaultPort;
            std::uint32_t lifetimeMs = static_cast<std::uint32_t>(LobbyServer::kDefaultLifetime.count());
        };

        static constexpr const char* kExeName = "CrucibleConnectionServer.exe";

        /// The server exe: next to the Editor first, then <buildDir>/ConnectionServer/<config>/,
        /// this build's configuration before the others. Empty if none exists.
        static std::filesystem::path FindExecutable(const std::filesystem::path& editorDir, const std::filesystem::path& buildDir);
        /// The folder of the running program (the Editor's).
        static std::filesystem::path ThisProgramDir();

        explicit ConnectionServerControl(std::filesystem::path executable = {});
        ~ConnectionServerControl();   // leaves the server running
        ConnectionServerControl(const ConnectionServerControl&) = delete;
        ConnectionServerControl& operator=(const ConnectionServerControl&) = delete;

        void SetExecutable(std::filesystem::path executable);
        const std::filesystem::path& Executable() const noexcept { return m_exe; }
        bool HasExecutable() const;

        /// Launches the server. False with a reason if it is already running, the exe is missing,
        /// or Windows refuses. A server that starts but cannot open its port exits at once:
        /// Update reports that (State::Exited, the log's last lines).
        bool Start(const Options& options, std::string& error);
        /// Ends the process this control started or reattached to. Nothing if none.
        void Stop();
        /// Takes back a server started earlier (by this or another Editor) from the .pid file.
        bool Reattach();
        /// Lets go of the process without stopping it (as when the Editor closes).
        void Detach() noexcept;

        void Update(Clock::time_point now);

        State         GetState() const noexcept { return m_state; }
        bool          IsRunning() const noexcept { return m_state == State::Running; }
        std::uint32_t ProcessId() const noexcept { return m_pid; }
        std::uint16_t Port() const noexcept { return m_options.port; }
        std::uint32_t LifetimeMs() const noexcept { return m_options.lifetimeMs; }
        std::uint32_t ExitCode() const noexcept { return m_exitCode; }
        bool          Reattached() const noexcept { return m_reattached; }

        /// The status monitor on 127.0.0.1:Port() (Off while not running).
        LobbyServerStatus MonitorState(Clock::time_point now) const noexcept;
        bool HasServerStatus() const noexcept { return m_monitor.HasServerStatus(); }
        const LobbyServer::StatusReplyMsg& ServerStatus() const noexcept { return m_monitor.ServerStatus(); }
        /// The server answering on the port is this process (its process id matches).
        bool Verified() const noexcept;

        std::filesystem::path LogPath() const;
        std::filesystem::path PidPath() const;
        /// The log's last lines (refreshed by Update about twice a second).
        const std::vector<std::string>& LogTail() const noexcept { return m_logTail; }

    private:
        void Adopt(void* process, std::uint32_t pid, const Options& options, bool reattached);
        void Release() noexcept;
        void ReadLog();
        void WritePidFile(std::uint64_t createdAt) const;
        void DeletePidFile() const noexcept;

        std::filesystem::path    m_exe;
        void*                    m_process = nullptr;   // HANDLE
        std::uint32_t            m_pid = 0;
        Options                  m_options;
        State                    m_state = State::Stopped;
        std::uint32_t            m_exitCode = 0;
        bool                     m_reattached = false;
        LobbyServerClient        m_monitor;
        std::vector<std::string> m_logTail;
        Clock::time_point        m_nextLogRead{};
    };

} // namespace Crucible
