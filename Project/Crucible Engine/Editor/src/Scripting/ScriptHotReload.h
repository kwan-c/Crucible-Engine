/*************************************************************************
* \file:         ScriptHotReload.h
* \author:       Tan Chun Kwang Xavier
* \par:          Email: chunkwangxavier.tan@digipen.edu
* \date          1 November 2026
* \version       1.0
* \brief         Editor script hot reload: save a .cs file and the Editor
*                compiles and reloads the scripts by itself.
*
* \details
*    Every frame (Tick):
*      1. Collects a finished compile (ScriptCompiler) and reports its errors
*         and warnings to the Console, with file and line.
*      2. Checks Assets/Scripts twice a second. When a .cs file was added,
*         removed or saved and then left alone for a moment (so a half-saved
*         file is not compiled), it starts a compile.
*      3. After a successful compile, calls ScriptRuntime::ReloadScripts -
*         straight away in Edit mode, or as soon as Play is stopped.
*
*    A failed compile keeps the previous scripts loaded, and the same broken
*    sources are not recompiled again until they change. At startup, it
*    compiles once if any source is newer than the deployed DLL.
*
**************************************************************************/
#pragma once
#include "Scripting/ScriptCompiler.h"

#include <chrono>
#include <memory>
#include <string>

namespace Crucible
{
	class ScriptRuntime;

	class ScriptHotReload
	{
	public:
		enum class Status
		{
			Disabled,      // no Mono-enabled runtime, or not configured
			UpToDate,      // loaded scripts match the sources
			Waiting,       // sources changed; compiling shortly (or Auto Recompile is off)
			Compiling,
			ReloadPending, // compiled; reloads when Play stops
			Errors         // last compile failed; previous scripts still loaded
		};

		/*********************************************************************
		 \brief  Sets the folders to use (once). Compiles straight away if any
		         source is newer than the deployed Crucible.Scripts.dll.
		 *********************************************************************/
		void Configure(const ScriptCompiler::Paths& paths);
		bool IsConfigured() const
		{
			return m_compiler != nullptr;
		}

		/*********************************************************************
		 \brief  Call once per frame on the main thread, outside any C# call.
		 \param  playing  true during a Play session (reload waits until Stop).
		 *********************************************************************/
		void Tick(ScriptRuntime& runtime, bool playing);

		/*********************************************************************
		 \brief  Compile now (Scripts > Recompile), even if nothing changed or
		         Auto Recompile is off.
		 *********************************************************************/
		void RequestRecompile()
		{
			m_requested = true;
		}

		bool autoRecompile = true; // compile automatically when a .cs file changes

		Status GetStatus() const
		{
			return m_status;
		}
		int ErrorCount() const
		{
			return m_errorCount;
		}
		const std::string& FirstError() const
		{
			return m_firstError;
		}
		// One-line description of GetStatus() for the menu bar.
		std::string StatusText() const;

	private:
		using Clock = std::chrono::steady_clock;
		void OnCompiled();

		std::unique_ptr<ScriptCompiler> m_compiler;
		uint64_t m_compiled = 0;  // signature of the sources last compiled (or loaded at startup)
		uint64_t m_seen = 0;      // signature found by the last scan
		Clock::time_point m_seenAt{}, m_nextScan{};
		bool m_requested = false, m_reloadPending = false;
		Status m_status = Status::Disabled;
		int m_errorCount = 0;
		std::string m_firstError;
	};
}
