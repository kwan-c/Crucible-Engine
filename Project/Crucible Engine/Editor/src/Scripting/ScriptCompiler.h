/*************************************************************************
* \file:         ScriptCompiler.h
* \author:       <author>
* \par:          Email: <email>
* \date          1 November 2026
* \version       1.0
* \brief         Compiles the project's C# scripts from inside the Editor.
*
* \details
*    Runs the Mono SDK's C# compiler (mono.exe + mcs.exe) as a hidden
*    background process over every .cs under Assets/Scripts plus
*    ManagedScripts/AssemblyInfo.cs, with the same options the CMake build
*    uses (ManagedScripts/ManagedLibrary.cmake). The Editor keeps running
*    while it compiles; Poll() each frame to collect the result.
*
*    On success the new Crucible.Scripts.dll (+ .mdb) is swapped into the
*    Editor's Managed/ folder (copied beside it, then renamed over it, so a
*    reader never sees half a file). ScriptRuntime loads DLLs from memory,
*    so the file is not locked; call ScriptRuntime::ReloadScripts() to use it.
*    On failure the previous DLL is left untouched.
*
*    Used by ScriptHotReload; it does not reload anything itself.
*
**************************************************************************/
#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace Crucible
{
	class ScriptCompiler
	{
	public:
		struct Paths
		{
			std::filesystem::path projectRoot; // folder holding Assets/ and ManagedScripts/
			std::filesystem::path managedDir;  // the Editor's Managed/ folder (has Crucible.ScriptCore.dll)
			std::filesystem::path monoSdk;     // has bin/mono(.exe) and lib/mono/4.5/mcs.exe
			bool debug = true;                 // DEBUG define + no optimisation, like a Debug build
		};

		/*********************************************************************
		 \brief  One compiler message, e.g.
		         Assets/Scripts/Test.cs(18,35): error CS0119: Expression denotes...
		 *********************************************************************/
		struct Diagnostic
		{
			std::string file;   // relative to the project root when possible
			int line = 0, column = 0;
			bool error = true;  // false = warning
			std::string code;   // e.g. "CS0119"
			std::string message;
		};

		struct Result
		{
			bool success = false;
			std::vector<Diagnostic> diagnostics;
			std::string failure;   // why it could not run or deploy (not a C# error)
			std::string output;    // full compiler output, for the log
			uint64_t signature = 0; // SourceSignature() of the sources that were compiled
			double seconds = 0;
		};

		explicit ScriptCompiler(Paths paths);
		~ScriptCompiler(); // waits for a running compile to finish
		ScriptCompiler(const ScriptCompiler&) = delete;
		ScriptCompiler& operator=(const ScriptCompiler&) = delete;

		/*********************************************************************
		 \brief  Starts compiling in the background.
		 \return false (and why) if a compile is already running or the SDK,
		         ScriptCore or AssemblyInfo.cs cannot be found.
		 *********************************************************************/
		bool Start(std::string& error);

		/*********************************************************************
		 \brief  Call every frame. Returns true exactly once per compile, when it
		         has finished (and been deployed, on success); then read Last().
		 *********************************************************************/
		bool Poll();

		bool IsCompiling() const
		{
			return m_worker.joinable();
		}
		const Result& Last() const
		{
			return m_last;
		}
		const Paths& GetPaths() const
		{
			return m_paths;
		}

		/*********************************************************************
		 \brief  A number that changes whenever a script source is added,
		         removed, renamed or saved (name + size + time of every file).
		 *********************************************************************/
		static uint64_t SourceSignature(const std::filesystem::path& projectRoot);

		/*********************************************************************
		 \brief  Every file that gets compiled: AssemblyInfo.cs, then every .cs
		         under Assets/Scripts (sorted).
		 *********************************************************************/
		static std::vector<std::filesystem::path> SourceFiles(const std::filesystem::path& projectRoot);

		/*********************************************************************
		 \brief  Parses mcs output lines into diagnostics (paths made relative
		         to projectRoot).
		 *********************************************************************/
		static std::vector<Diagnostic> ParseDiagnostics(const std::string& output,
		                                                const std::filesystem::path& projectRoot);

		/*********************************************************************
		 \brief  Folder the running executable is in.
		 *********************************************************************/
		static std::filesystem::path ExecutableDirectory();

	private:
		void Run();      // worker thread: runs the compiler process
		bool Deploy();   // main thread: swaps the new DLL into managedDir

		Paths m_paths;
		std::filesystem::path m_workDir; // temp folder for the response file and outputs
		std::thread m_worker;
		std::atomic<bool> m_done{false};
		// Written by the worker before m_done, read by the main thread after it.
		bool m_started = false;
		int m_exitCode = -1;
		std::string m_output, m_launchError;
		uint64_t m_pendingSignature = 0;
		double m_startTime = 0;
		Result m_last;
	};
}
