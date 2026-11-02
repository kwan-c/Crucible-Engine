/*************************************************************************
* \file:         ScriptCompiler.cpp
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          1 November 2026
* \version       1.0
* \brief         Implementation - see ScriptCompiler.h.
*
**************************************************************************/
#include "Scripting/ScriptCompiler.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <regex>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace Crucible
{
	namespace
	{
		double Now()
		{
			using namespace std::chrono;
			return duration<double>(steady_clock::now().time_since_epoch()).count();
		}

		bool HasCsExtension(const fs::path& file)
		{
			std::string extension = file.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(),
			               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return extension == ".cs";
		}

		// Response files take one argument per line; quote paths (they may contain spaces).
		std::string Quoted(const fs::path& path)
		{
			return "\"" + path.generic_string() + "\"";
		}

		const char* MonoExecutable()
		{
#ifdef _WIN32
			return "bin/mono.exe";
#else
			return "bin/mono";
#endif
		}

		unsigned long ProcessId()
		{
#ifdef _WIN32
			return static_cast<unsigned long>(GetCurrentProcessId());
#else
			return static_cast<unsigned long>(getpid());
#endif
		}
	}

	ScriptCompiler::ScriptCompiler(Paths paths)
	    : m_paths(std::move(paths))
	{
		std::error_code ec;
		m_workDir = fs::temp_directory_path(ec) / ("CrucibleScripts-" + std::to_string(ProcessId()));
	}

	ScriptCompiler::~ScriptCompiler()
	{
		if (m_worker.joinable())
			m_worker.join(); // a compile takes a few seconds; never leave the thread running
	}

	fs::path ScriptCompiler::ExecutableDirectory()
	{
#ifdef _WIN32
		std::vector<wchar_t> buffer(512);
		for (;;)
		{
			const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (size == 0)
				return {};
			if (size < buffer.size())
				return fs::path(std::wstring(buffer.data(), size)).parent_path();
			if (buffer.size() >= 32768)
				return {};
			buffer.resize(buffer.size() * 2);
		}
#else
		std::error_code ec;
		return fs::read_symlink("/proc/self/exe", ec).parent_path();
#endif
	}

	std::vector<fs::path> ScriptCompiler::SourceFiles(const fs::path& projectRoot)
	{
		std::vector<fs::path> files;
		std::error_code ec;
		const fs::path assemblyInfo = projectRoot / "ManagedScripts/AssemblyInfo.cs";
		const fs::path scripts = projectRoot / "Assets/Scripts";
		if (fs::is_directory(scripts, ec))
		{
			for (auto it = fs::recursive_directory_iterator(scripts, ec); !ec && it != fs::recursive_directory_iterator();
			     it.increment(ec))
			{
				if (it->is_regular_file(ec) && HasCsExtension(it->path()))
					files.push_back(it->path());
			}
		}
		std::sort(files.begin(), files.end());
		if (fs::is_regular_file(assemblyInfo, ec))
			files.insert(files.begin(), assemblyInfo);
		return files;
	}

	uint64_t ScriptCompiler::SourceSignature(const fs::path& projectRoot)
	{
		// FNV-1a over every file's path, size and last-write time.
		uint64_t hash = 1469598103934665603ull;
		const auto mix = [&hash](const void* data, size_t size)
		{
			const auto* bytes = static_cast<const unsigned char*>(data);
			for (size_t i = 0; i < size; ++i)
				hash = (hash ^ bytes[i]) * 1099511628211ull;
		};
		for (const auto& file : SourceFiles(projectRoot))
		{
			std::error_code ec;
			const std::string name = file.generic_string();
			const auto size = static_cast<uint64_t>(fs::file_size(file, ec));
			const auto time = static_cast<int64_t>(fs::last_write_time(file, ec).time_since_epoch().count());
			mix(name.data(), name.size());
			mix(&size, sizeof(size));
			mix(&time, sizeof(time));
		}
		return hash;
	}

	std::vector<ScriptCompiler::Diagnostic> ScriptCompiler::ParseDiagnostics(const std::string& output,
	                                                                         const fs::path& projectRoot)
	{
		// mcs: "C:/x/Assets/Scripts/Test.cs(18,35): error CS0119: message"
		//      "error CS2001: Source file `x' could not be found"   (no position)
		static const std::regex located(R"(^(.*)\((\d+),(\d+)\)\s*:\s*(error|warning)\s+(CS\d+)\s*:\s*(.*)$)");
		static const std::regex general(R"(^\s*(error|warning)\s+(CS\d+)\s*:\s*(.*)$)");
		std::vector<Diagnostic> diagnostics;
		size_t begin = 0;
		while (begin < output.size())
		{
			size_t end = output.find('\n', begin);
			if (end == std::string::npos)
				end = output.size();
			std::string line = output.substr(begin, end - begin);
			begin = end + 1;
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			std::smatch match;
			Diagnostic diagnostic;
			if (std::regex_match(line, match, located))
			{
				std::error_code ec;
				const fs::path file = fs::path(match[1].str());
				const fs::path relative = fs::relative(file, projectRoot, ec);
				diagnostic.file = (!ec && !relative.empty() && *relative.begin() != "..") ? relative.generic_string()
				                                                                          : file.generic_string();
				diagnostic.line = std::stoi(match[2].str());
				diagnostic.column = std::stoi(match[3].str());
				diagnostic.error = match[4].str() == "error";
				diagnostic.code = match[5].str();
				diagnostic.message = match[6].str();
			}
			else if (std::regex_match(line, match, general))
			{
				diagnostic.error = match[1].str() == "error";
				diagnostic.code = match[2].str();
				diagnostic.message = match[3].str();
			}
			else
				continue;
			diagnostics.push_back(std::move(diagnostic));
		}
		return diagnostics;
	}

	bool ScriptCompiler::Start(std::string& error)
	{
		error.clear();
		if (m_worker.joinable())
		{
			error = "a compile is already running";
			return false;
		}
		std::error_code ec;
		const fs::path mono = m_paths.monoSdk / MonoExecutable();
		const fs::path mcs = m_paths.monoSdk / "lib/mono/4.5/mcs.exe";
		const fs::path scriptCore = m_paths.managedDir / "Crucible.ScriptCore.dll";
		if (!fs::is_regular_file(mono, ec) || !fs::is_regular_file(mcs, ec))
		{
			error = "the Mono SDK compiler was not found in " + m_paths.monoSdk.generic_string();
			return false;
		}
		if (!fs::is_regular_file(scriptCore, ec))
		{
			error = "Crucible.ScriptCore.dll is missing from " + m_paths.managedDir.generic_string() +
			        " (do one normal Editor build first)";
			return false;
		}
		const auto sources = SourceFiles(m_paths.projectRoot);
		if (sources.empty() || sources.front().filename() != "AssemblyInfo.cs")
		{
			error = "ManagedScripts/AssemblyInfo.cs was not found under " + m_paths.projectRoot.generic_string();
			return false;
		}

		// Fresh work folder: never deploy a stale DLL from an earlier compile.
		fs::create_directories(m_workDir, ec);
		const fs::path output = m_workDir / "Crucible.Scripts.dll";
		fs::remove(output, ec);
		fs::remove(fs::path(output.string() + ".mdb"), ec);

		// Same options as ManagedScripts/ManagedLibrary.cmake, minus -sdk:4.5: the
		// vendored SDK has no 4.5-api folder, so that option only produced warning CS8001.
		const fs::path response = m_workDir / "Crucible.Scripts.rsp";
		{
			std::ofstream file(response, std::ios::binary | std::ios::trunc);
			file << "-noconfig\n-langversion:7.2\n-platform:anycpu\n-target:library\n-debug\n"
			     << "-r:System.dll\n-r:System.Core.dll\n"
			     << (m_paths.debug ? "-optimize-\n-define:TRACE\n-define:DEBUG\n" : "-optimize+\n-define:TRACE\n")
			     << "-r:" << Quoted(scriptCore) << "\n"
			     << "-out:" << Quoted(output) << "\n";
			for (const auto& source : sources)
				file << Quoted(source) << "\n";
			if (!file)
			{
				error = "could not write " + response.generic_string();
				return false;
			}
		}

		m_done = false;
		m_started = false;
		m_exitCode = -1;
		m_output.clear();
		m_launchError.clear();
		m_pendingSignature = SourceSignature(m_paths.projectRoot);
		m_startTime = Now();
		m_worker = std::thread([this] { Run(); });
		return true;
	}

	void ScriptCompiler::Run()
	{
		const fs::path mono = m_paths.monoSdk / MonoExecutable();
		const fs::path mcs = m_paths.monoSdk / "lib/mono/4.5/mcs.exe";
		const fs::path response = m_workDir / "Crucible.Scripts.rsp";
#ifdef _WIN32
		// Hidden child process; its stdout and stderr go into one pipe we read.
		SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
		HANDLE readPipe = nullptr, writePipe = nullptr;
		if (!CreatePipe(&readPipe, &writePipe, &security, 0))
		{
			m_launchError = "could not create a pipe for the compiler output";
			m_done.store(true, std::memory_order_release);
			return;
		}
		SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdOutput = writePipe;
		startup.hStdError = writePipe;
		PROCESS_INFORMATION process{};
		std::wstring commandLine = L"\"" + mono.wstring() + L"\" \"" + mcs.wstring() + L"\" @\"" + response.wstring() + L"\"";
		const BOOL launched = CreateProcessW(mono.c_str(), commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
		                                     nullptr, nullptr, &startup, &process);
		CloseHandle(writePipe); // only the child writes now; ReadFile ends when it exits
		if (!launched)
		{
			CloseHandle(readPipe);
			m_launchError = "could not start " + mono.generic_string() + " (error " + std::to_string(GetLastError()) + ")";
			m_done.store(true, std::memory_order_release);
			return;
		}
		m_started = true;
		CloseHandle(process.hThread);
		char buffer[4096];
		DWORD read = 0;
		while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
			m_output.append(buffer, read);
		CloseHandle(readPipe);
		WaitForSingleObject(process.hProcess, INFINITE);
		DWORD exitCode = 1;
		GetExitCodeProcess(process.hProcess, &exitCode);
		CloseHandle(process.hProcess);
		m_exitCode = static_cast<int>(exitCode);
#else
		const std::string command =
		    "\"" + mono.string() + "\" \"" + mcs.string() + "\" @\"" + response.string() + "\" 2>&1";
		FILE* pipe = popen(command.c_str(), "r");
		if (!pipe)
		{
			m_launchError = "could not start " + mono.generic_string();
			m_done.store(true, std::memory_order_release);
			return;
		}
		m_started = true;
		char buffer[4096];
		size_t read = 0;
		while ((read = fread(buffer, 1, sizeof(buffer), pipe)) > 0)
			m_output.append(buffer, read);
		const int status = pclose(pipe);
		m_exitCode = (status >= 0 && WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
#endif
		m_done.store(true, std::memory_order_release);
	}

	bool ScriptCompiler::Deploy()
	{
		std::error_code ec;
		for (const char* name : {"Crucible.Scripts.dll", "Crucible.Scripts.dll.mdb"})
		{
			const fs::path source = m_workDir / name;
			const fs::path target = m_paths.managedDir / name;
			const fs::path partial = fs::path(target.string() + ".partial");
			if (!fs::is_regular_file(source, ec))
			{
				m_last.failure = std::string("the compiler did not produce ") + name;
				return false;
			}
			// Copy beside it, then rename over it: a reader never sees half a file.
			if (!fs::copy_file(source, partial, fs::copy_options::overwrite_existing, ec) || ec)
			{
				m_last.failure = "could not copy " + std::string(name) + " into " + m_paths.managedDir.generic_string() +
				                 ": " + ec.message();
				return false;
			}
			fs::rename(partial, target, ec);
			if (ec)
			{
				fs::remove(partial, ec);
				m_last.failure = "could not replace " + target.generic_string() + ": " + ec.message();
				return false;
			}
		}
		return true;
	}

	bool ScriptCompiler::Poll()
	{
		if (!m_worker.joinable() || !m_done.load(std::memory_order_acquire))
			return false;
		m_worker.join();
		m_last = {};
		m_last.signature = m_pendingSignature;
		m_last.seconds = Now() - m_startTime;
		m_last.output = m_output;
		m_last.diagnostics = ParseDiagnostics(m_output, m_paths.projectRoot);
		if (!m_started)
			m_last.failure = m_launchError;
		else if (m_exitCode == 0)
			m_last.success = Deploy();
		else if (std::none_of(m_last.diagnostics.begin(), m_last.diagnostics.end(), [](const auto& d) { return d.error; }))
			m_last.failure = "the compiler failed (exit code " + std::to_string(m_exitCode) + ")";
		return true;
	}
}
