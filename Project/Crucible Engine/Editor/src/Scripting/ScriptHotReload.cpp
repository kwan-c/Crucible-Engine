/*************************************************************************
* \file:         ScriptHotReload.cpp
* \author:       <author>
* \par:          Email: <email>
* \date          1 November 2026
* \version       1.0
* \brief         Implementation - see ScriptHotReload.h.
*
**************************************************************************/
#include "Scripting/ScriptHotReload.h"
#include "Scripting/ScriptRuntime.h"
#include "Diagnostics/Logger.h"

#include <algorithm>
#include <system_error>

namespace fs = std::filesystem;

namespace Crucible
{
	namespace
	{
		constexpr auto kScanInterval = std::chrono::milliseconds(500); // how often Assets/Scripts is checked
		constexpr auto kSettleTime = std::chrono::milliseconds(400);   // a change must be this old before compiling
		constexpr size_t kMaxReported = 25;                            // Console lines per compile

		// Newest last-write time among the sources, to compare with the deployed DLL.
		fs::file_time_type NewestSource(const fs::path& projectRoot)
		{
			fs::file_time_type newest = fs::file_time_type::min();
			for (const auto& file : ScriptCompiler::SourceFiles(projectRoot))
			{
				std::error_code ec;
				const auto time = fs::last_write_time(file, ec);
				if (!ec && time > newest)
					newest = time;
			}
			return newest;
		}
	}

	void ScriptHotReload::Configure(const ScriptCompiler::Paths& paths)
	{
		m_compiler = std::make_unique<ScriptCompiler>(paths);
		m_seen = m_compiled = ScriptCompiler::SourceSignature(paths.projectRoot);
		m_seenAt = Clock::now() - kSettleTime;
		m_nextScan = Clock::now() + kScanInterval;
		m_status = Status::UpToDate;
		// Sources edited while the Editor was closed (and not built since): compile now.
		std::error_code ec;
		const auto dll = fs::last_write_time(paths.managedDir / "Crucible.Scripts.dll", ec);
		if (ec || NewestSource(paths.projectRoot) > dll)
			m_compiled = 0;
	}

	void ScriptHotReload::Tick(ScriptRuntime& runtime, bool playing)
	{
		if (!m_compiler || !runtime.IsRunning())
		{
			m_status = Status::Disabled;
			return;
		}
		const auto now = Clock::now();

		// 1. A compile finished: report it.
		if (m_compiler->Poll())
			OnCompiled();

		// 2. Watch the sources.
		if (now >= m_nextScan)
		{
			m_nextScan = now + kScanInterval;
			const uint64_t signature = ScriptCompiler::SourceSignature(m_compiler->GetPaths().projectRoot);
			if (signature != m_seen)
			{
				m_seen = signature;
				m_seenAt = now; // restart the settle time on every change
			}
		}
		const bool changed = m_seen != m_compiled;
		const bool settled = now - m_seenAt >= kSettleTime;
		if (!m_compiler->IsCompiling() && (m_requested || (autoRecompile && changed && settled)))
		{
			m_requested = false;
			std::string error;
			if (m_compiler->Start(error))
				m_status = Status::Compiling;
			else
			{
				m_compiled = m_seen; // do not retry every frame; a change or Recompile retries
				m_status = Status::Errors;
				m_errorCount = 1;
				m_firstError = error;
				CRU_USER_ERROR("Script compile could not start: {}", error);
			}
		}

		// 3. Reload once it is safe: in Edit mode, not while another compile runs.
		if (m_reloadPending && !playing && !m_compiler->IsCompiling())
		{
			m_reloadPending = false;
			if (runtime.ReloadScripts())
				CRU_USER_INFO("Scripts reloaded. New and changed scripts are available in Add Component.");
			else
				CRU_USER_WARN("Script reload: {}", runtime.LastError());
		}

		// Status for the menu bar.
		if (m_compiler->IsCompiling())
			m_status = Status::Compiling;
		else if (m_reloadPending)
			m_status = Status::ReloadPending;
		else if (m_status != Status::Errors)
			m_status = changed ? Status::Waiting : Status::UpToDate;
		else if (changed)
			m_status = Status::Waiting; // errors from the last compile, but the sources moved on
	}

	void ScriptHotReload::OnCompiled()
	{
		const auto& result = m_compiler->Last();
		m_compiled = result.signature;
		size_t reported = 0, warnings = 0;
		m_errorCount = 0;
		m_firstError.clear();
		for (const auto& d : result.diagnostics)
		{
			const std::string where = d.file.empty() ? std::string()
			                                         : d.file + "(" + std::to_string(d.line) + "," +
			                                               std::to_string(d.column) + "): ";
			if (d.error)
			{
				if (m_firstError.empty())
					m_firstError = where + d.code + ": " + d.message;
				++m_errorCount;
				if (reported++ < kMaxReported)
					CRU_USER_ERROR("{}{}: {}", where, d.code, d.message);
			}
			else
			{
				++warnings;
				if (reported++ < kMaxReported)
					CRU_USER_WARN("{}{}: {}", where, d.code, d.message);
			}
		}
		if (result.success)
		{
			m_status = Status::UpToDate;
			m_reloadPending = true;
			CRU_USER_INFO("Scripts compiled in {:.1f}s{}.", result.seconds,
			              warnings ? " (" + std::to_string(warnings) + " warning(s))" : std::string());
			return;
		}
		m_status = Status::Errors;
		if (!result.failure.empty())
		{
			if (m_firstError.empty())
				m_firstError = result.failure;
			m_errorCount = std::max(m_errorCount, 1);
			CRU_USER_ERROR("Script compile failed: {}", result.failure);
		}
		CRU_USER_ERROR("Script compile failed with {} error(s). The previous scripts stay loaded.", m_errorCount);
	}

	std::string ScriptHotReload::StatusText() const
	{
		switch (m_status)
		{
		case Status::Disabled:
			return "";
		case Status::UpToDate:
			return "Scripts up to date";
		case Status::Waiting:
			return autoRecompile ? "Scripts changed..." : "Scripts changed (Scripts > Recompile)";
		case Status::Compiling:
			return "Compiling scripts...";
		case Status::ReloadPending:
			return "Scripts compiled - reload after Stop";
		case Status::Errors:
			return "Script errors: " + std::to_string(m_errorCount);
		}
		return "";
	}
}
