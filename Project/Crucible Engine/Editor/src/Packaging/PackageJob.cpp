/*!
* @file			PackageJob.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			8 November 2026
*
* @brief		Implementation - see PackageJob.h.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "Packaging/PackageJob.h"
#include "Cook.h"   // Tools/Cook
#include "AssetBuildReport.h"   // Tools/Common: assets the build could not compile

#include <exception>
#include <fstream>
#include <regex>
#include <sstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>   // ShellExecuteW
#include <shobjidl.h>   // IFileOpenDialog (the folder picker)
#endif

namespace fs = std::filesystem;

namespace Crucible {

	namespace {

		// A path for a command line, in quotes. A trailing separator is dropped: a backslash
		// right before the closing quote would escape it.
		std::wstring Quoted(fs::path const& path)
		{
			std::wstring text = fs::path(path).make_preferred().wstring();
			while (text.size() > 3 && (text.back() == L'\\' || text.back() == L'/'))
				text.pop_back();
			return L"\"" + text + L"\"";
		}

		// Versions are plain ASCII (IsValidVersion), so a straight copy is enough.
		std::wstring Widen(std::string_view text) { return std::wstring(text.begin(), text.end()); }

		// For messages: Windows separators throughout (the build folder's path uses '/').
		std::string ToUtf8(fs::path const& path)
		{
			std::u8string const text = fs::path(path).make_preferred().u8string();
			return std::string(reinterpret_cast<char const*>(text.data()), text.size());
		}

		// A line of build output that reports an error (MSVC, MSBuild or CMake).
		bool LooksLikeError(std::string_view line)
		{
			return line.find(": error ") != std::string_view::npos
				|| line.find(": fatal error ") != std::string_view::npos
				|| line.find("error MSB") != std::string_view::npos
				|| line.find("CMake Error") != std::string_view::npos;
		}

	}	// anonymous namespace

	// ------------------------------------------------------------------------------------------ //

	PackageJob::~PackageJob()
	{
		if (m_worker.joinable()) {
			Cancel();
			m_worker.join();
		}
	}

	const char* PackageJob::StageText(Stage stage)
	{
		switch (stage) {
		case Stage::Idle:      return "Ready";
		case Stage::Building:  return "Building the game (Release)";
		case Stage::Cooking:   return "Cooking Assets into Resources";
		case Stage::Packing:   return "Packing the zip";
		case Stage::Done:      return "Done";
		case Stage::Failed:    return "Failed";
		case Stage::Cancelled: return "Cancelled";
		}
		return "";
	}

	bool PackageJob::IsValidVersion(std::string_view version)
	{
		if (version.empty() || version.size() > 32)
			return false;
		for (char const c : version) {
			bool const ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			             || c == '.' || c == '-' || c == '_';
			if (!ok)
				return false;
		}
		return true;
	}

	std::string PackageJob::ConfiguredVersion(fs::path const& buildDir)
	{
		std::ifstream file{ buildDir / "CPackConfig.cmake" };
		if (!file)
			return {};
		std::stringstream text;
		text << file.rdbuf();
		std::smatch match;
		std::string const content = text.str();
		static std::regex const pattern{ R"(set\(CPACK_PACKAGE_VERSION\s+\"([^\"]*)\"\))" };
		return std::regex_search(content, match, pattern) ? match[1].str() : std::string{};
	}

	void PackageJob::ShowInExplorer(fs::path const& file)
	{
#ifdef _WIN32
		std::wstring const args = L"/select," + Quoted(file);
		ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
#else
		(void)file;
#endif
	}

	bool PackageJob::PickFolder(fs::path const& start, fs::path& chosen)
	{
#ifdef _WIN32
		// The dialog is a COM object. Fine if COM is already set up on this thread (then this
		// call just fails harmlessly and there is nothing to undo).
		HRESULT const init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
		bool picked = false;

		IFileOpenDialog* dialog = nullptr;
		if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
			DWORD options = 0;
			dialog->GetOptions(&options);
			dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
			dialog->SetTitle(L"Choose where to put the game's zip");

			// Open at the current folder; if it does not exist yet, the nearest folder above it.
			std::error_code ec;
			fs::path folder = fs::path(start).make_preferred();
			while (!folder.empty() && !fs::is_directory(folder, ec) && folder.has_parent_path() && folder.parent_path() != folder)
				folder = folder.parent_path();
			IShellItem* startItem = nullptr;
			if (!folder.empty() && fs::is_directory(folder, ec)
			    && SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&startItem)))) {
				dialog->SetFolder(startItem);
				startItem->Release();
			}

			// Owned by the Editor's window, so it stays in front of it.
			if (SUCCEEDED(dialog->Show(GetActiveWindow()))) {
				IShellItem* result = nullptr;
				if (SUCCEEDED(dialog->GetResult(&result))) {
					PWSTR path = nullptr;
					if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
						chosen = fs::path(path);
						picked = true;
						CoTaskMemFree(path);
					}
					result->Release();
				}
			}
			dialog->Release();
		}

		if (SUCCEEDED(init))
			CoUninitialize();
		return picked;
#else
		(void)start; (void)chosen;
		return false;
#endif
	}

	double PackageJob::Seconds() const
	{
		if (m_stage == Stage::Idle)
			return 0.0;
		auto const end = IsRunning() ? std::chrono::steady_clock::now() : m_end;
		return std::chrono::duration<double>(end - m_start).count();
	}

	void PackageJob::TakeNewOutput(std::string& out)
	{
		std::lock_guard lock{ m_mutex };
		out += m_newOutput;
		m_newOutput.clear();
	}

	void PackageJob::Append(std::string_view text)
	{
		std::lock_guard lock{ m_mutex };
		m_newOutput.append(text);
	}

	// ------------------------------------------------------------------------------------------ //

	bool PackageJob::Start(Settings settings, std::string& error)
	{
		if (IsRunning() || m_worker.joinable()) {
			error = "a package is already being made";
			return false;
		}
		std::error_code ec;
		if (!IsValidVersion(settings.version)) {
			error = "the version may only use letters, digits, '.', '-' and '_' (for example 0.2.0)";
			return false;
		}
		if (!fs::is_regular_file(settings.cmake, ec) || !fs::is_regular_file(settings.cpack, ec)) {
			error = "CMake was not found at " + ToUtf8(settings.cmake) + " - run run.bat again";
			return false;
		}
		if (!fs::is_regular_file(settings.buildDir / "CPackConfig.cmake", ec)) {
			error = "the build folder " + ToUtf8(settings.buildDir) + " has no CPackConfig.cmake - run run.bat again";
			return false;
		}
		if (!fs::is_directory(settings.assets, ec) || !fs::is_directory(settings.resources, ec)) {
			error = "the Assets or Resources folder is missing";
			return false;
		}
		if (settings.outputDir.empty()) {
			error = "choose an output folder";
			return false;
		}

		m_settings = std::move(settings);
		m_failure.clear();
		m_zip.clear();
		m_cancelled = false;
		{
			std::lock_guard lock{ m_mutex };
			m_newOutput.clear();
		}
		m_start = std::chrono::steady_clock::now();
		m_stage = Stage::Building;

		// --parallel builds files side by side (/m). /nodeReuse:false: MSBuild's helper processes
		// end with the build instead of staying around for the next one.
		Append("== Building the game (Release) ==\n");
		Launch(Quoted(m_settings.cmake) + L" --build " + Quoted(m_settings.buildDir)
		       + L" --config Release --target Game --parallel -- /nodeReuse:false");
		return true;
	}

	void PackageJob::Cancel()
	{
		if (!IsRunning())
			return;
		m_cancelled = true;
#ifdef _WIN32
		std::lock_guard lock{ m_mutex };
		if (m_processGroup)
			TerminateJobObject(static_cast<HANDLE>(m_processGroup), 1);
#endif
	}

	void PackageJob::Poll()
	{
		// Step 2 runs here, on the main thread, one frame after the build: that frame shows
		// "Cooking" before the Editor waits for it (Cook usually takes well under a second).
		if (m_stage == Stage::Cooking) {
			if (m_cancelled) { Finish(Stage::Cancelled, "Cancelled."); return; }
			RunCook();
			if (m_stage == Stage::Cooking)
				StartPacking();
			return;
		}

		if (!m_worker.joinable() || !m_workerDone.load(std::memory_order_acquire))
			return;
		m_worker.join();

		if (m_cancelled) { Finish(Stage::Cancelled, "Cancelled."); return; }
		if (!m_launchError.empty()) { Finish(Stage::Failed, m_launchError); return; }

		std::string const detail = m_firstError.empty() ? std::string(" - see the output")
		                                                : ". First error:\n" + m_firstError;
		if (m_stage == Stage::Building) {
			if (m_exitCode != 0) {
				Finish(Stage::Failed, "The game did not build" + detail);
				return;
			}
			m_stage = Stage::Cooking;
		}
		else if (m_stage == Stage::Packing) {
			if (m_exitCode != 0) {
				Finish(Stage::Failed, "CPack could not make the zip" + detail);
				return;
			}
			CollectZip();
		}
	}

	void PackageJob::Finish(Stage stage, std::string failure)
	{
		m_stage = stage;
		m_end = std::chrono::steady_clock::now();
		if (!failure.empty())
			Append("\n" + failure + "\n");
		m_failure = std::move(failure);
	}

	// ------------------------------------------------------------------------------------------ //

	void PackageJob::RunCook()
	{
		Append("\n== Cooking Assets into Resources ==\n");

		// The Release build just run lets a model or image that failed to compile through (it only
		// lists it, so the Editor can still open). A shipped game must not have one.
		std::vector<AssetBuildReport::Problem> const failed = AssetBuildReport::ReadAll(m_settings.buildDir);
		if (!failed.empty()) {
			for (AssetBuildReport::Problem const& p : failed) Append("error: " + p.asset + ": " + p.message + "\n");
			Finish(Stage::Failed, std::to_string(failed.size()) + " asset(s) failed to compile, listed above. "
			       "Fix them, then package again.");
			return;
		}

		Cook::Report report;
		try {
			report = Cook::Run(m_settings.assets, m_settings.resources);
		}
		catch (std::exception const& e) {
			Finish(Stage::Failed, std::string("Cook stopped: ") + e.what());
			return;
		}

		for (std::string const& line : report.warnings) Append("warning: " + line + "\n");
		for (std::string const& line : report.missing)  Append("missing: " + line + "\n");
		for (std::string const& line : report.errors)   Append("error: " + line + "\n");
		Append("Cook: " + std::to_string(report.copied) + " copied, " + std::to_string(report.unchanged)
		       + " already up to date, " + std::to_string(report.removed) + " removed\n");

		if (!report.Ok()) {
			Finish(Stage::Failed, "Cook found " + std::to_string(report.missing.size()) + " missing compiled file(s) and "
			       + std::to_string(report.errors.size()) + " error(s), listed above. Compile those assets "
			       "(Font Importer / Texture Importer), then package again.");
		}
	}

	void PackageJob::StartPacking()
	{
		// CPack writes its zip (and its working files) here; CollectZip copies the zip out.
		fs::path const staging = m_settings.buildDir / "PackageStaging";
		std::error_code ec;
		fs::create_directories(staging, ec);
		for (fs::directory_entry const& entry : fs::directory_iterator(staging, ec)) {
			if (entry.path().extension() == ".zip")
				fs::remove(entry.path(), ec);   // so the only zip left afterwards is the new one
		}

		m_stage = Stage::Packing;
		Append("\n== Packing the zip (CPack, version " + m_settings.version + ") ==\n");
		Launch(Quoted(m_settings.cpack) + L" -C Release --config " + Quoted(m_settings.buildDir / "CPackConfig.cmake")
		       + L" -R " + Widen(m_settings.version) + L" -B " + Quoted(staging));
	}

	void PackageJob::CollectZip()
	{
		fs::path const staging = m_settings.buildDir / "PackageStaging";
		std::error_code ec;
		fs::path zip;
		for (fs::directory_entry const& entry : fs::directory_iterator(staging, ec)) {
			if (entry.path().extension() == ".zip") { zip = entry.path(); break; }
		}
		if (zip.empty()) {
			Finish(Stage::Failed, "CPack finished but made no zip in " + ToUtf8(staging));
			return;
		}

		fs::create_directories(m_settings.outputDir, ec);
		fs::path const target = m_settings.outputDir / zip.filename();
		if (!fs::copy_file(zip, target, fs::copy_options::overwrite_existing, ec) || ec) {
			Finish(Stage::Failed, "Could not copy the zip to " + ToUtf8(target) + ": " + ec.message());
			return;
		}

		m_zip = target;
		Append("\nPackaged: " + ToUtf8(target) + "\n");
		Finish(Stage::Done);
	}

	// ------------------------------------------------------------------------------------------ //

	void PackageJob::Launch(std::wstring commandLine)
	{
		m_workerDone = false;
		m_exitCode = -1;
		m_launchError.clear();
		m_firstError.clear();
		m_worker = std::thread([this, command = std::move(commandLine)]() mutable { RunProcess(std::move(command)); });
	}

	void PackageJob::RunProcess(std::wstring commandLine)
	{
#ifdef _WIN32
		// Hidden child process; its stdout and stderr go into one pipe we read.
		SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
		HANDLE readPipe = nullptr, writePipe = nullptr;
		if (!CreatePipe(&readPipe, &writePipe, &security, 0)) {
			m_launchError = "Could not create a pipe for the build output";
			m_workerDone.store(true, std::memory_order_release);
			return;
		}
		SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

		// Everything the process starts (MSBuild, the compilers, ...) joins this job object, so
		// Cancel ends all of it, and closing the object ends anything still left over.
		HANDLE group = CreateJobObjectW(nullptr, nullptr);
		if (group) {
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
			limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			SetInformationJobObject(group, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
		}

		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdOutput = writePipe;
		startup.hStdError = writePipe;
		PROCESS_INFORMATION process{};
		std::wstring const workDir = m_settings.buildDir.wstring();
		// Suspended until it is in the job object, so nothing it starts can slip out.
		BOOL const launched = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
		                                     CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, workDir.c_str(),
		                                     &startup, &process);
		CloseHandle(writePipe);   // only the child writes now
		if (!launched) {
			DWORD const code = GetLastError();
			CloseHandle(readPipe);
			if (group) CloseHandle(group);
			m_launchError = "Could not start the build tool (Windows error " + std::to_string(code) + ")";
			m_workerDone.store(true, std::memory_order_release);
			return;
		}
		if (group)
			AssignProcessToJobObject(group, process.hProcess);
		{
			std::lock_guard lock{ m_mutex };
			m_processGroup = group;
			if (m_cancelled) {   // Cancel came before there was anything to stop
				if (group) TerminateJobObject(group, 1);
				else       TerminateProcess(process.hProcess, 1);
			}
		}
		ResumeThread(process.hThread);
		CloseHandle(process.hThread);

		// Read until the process has exited and the pipe is empty. Not until the pipe closes:
		// a helper the build leaves running (mspdbsrv.exe, for one) can keep it open long after.
		std::string line;
		auto takeOutput = [&](char const* data, std::size_t size) {
			Append(std::string_view(data, size));
			for (std::size_t i = 0; i < size; ++i) {
				if (data[i] == '\n') {
					if (m_firstError.empty() && LooksLikeError(line)) m_firstError = line;
					line.clear();
				}
				else if (data[i] != '\r' && line.size() < 1024) {
					line += data[i];
				}
			}
		};
		char buffer[4096];
		for (;;) {
			DWORD available = 0;
			if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr))
				break;   // pipe closed: every writer has exited
			if (available > 0) {
				DWORD read = 0;
				if (!ReadFile(readPipe, buffer, sizeof(buffer) < available ? sizeof(buffer) : available, &read, nullptr) || read == 0)
					break;
				takeOutput(buffer, read);
				continue;
			}
			if (WaitForSingleObject(process.hProcess, 50) == WAIT_OBJECT_0) {
				// Exited: take whatever is still in the pipe, then stop.
				while (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
					DWORD read = 0;
					if (!ReadFile(readPipe, buffer, sizeof(buffer) < available ? sizeof(buffer) : available, &read, nullptr) || read == 0)
						break;
					takeOutput(buffer, read);
				}
				break;
			}
		}
		if (!line.empty() && m_firstError.empty() && LooksLikeError(line))
			m_firstError = line;
		CloseHandle(readPipe);

		WaitForSingleObject(process.hProcess, INFINITE);
		DWORD exitCode = 1;
		GetExitCodeProcess(process.hProcess, &exitCode);
		CloseHandle(process.hProcess);
		{
			std::lock_guard lock{ m_mutex };
			m_processGroup = nullptr;
		}
		if (group)
			CloseHandle(group);   // ends any helper still running
		m_exitCode = static_cast<int>(exitCode);
#else
		(void)commandLine;
		m_launchError = "Packaging from the Editor is only supported on Windows";
#endif
		m_workerDone.store(true, std::memory_order_release);
	}

}
