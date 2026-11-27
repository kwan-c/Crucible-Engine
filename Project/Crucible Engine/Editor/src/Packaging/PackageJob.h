/*!
* @file			PackageJob.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			8 November 2026
*
* @brief		Makes the game's zip and/or installer from inside the Editor (File > Package Game).
*
* Does what a person would do by hand, one step after another, while the Editor keeps running:
*
*   1. Build    cmake --build <Build> --config Release --target Game
*               (a background process; also recompiles changed shaders, models and textures)
*   2. Cook     Cook::Run, on the main thread (Tools/Cook/Cook.h): fills Resources with
*               everything the game reads. Stops here if anything is missing.
*   3. Pack     (zip) cpack -C Release -R <version>: zips the "Game" install component
*               (Game/CMakeLists.txt) into Build/PackageStaging, then copies the zip into the
*               chosen output folder
*   4. Stage    (installer) empties Tools/Package/Installer/GAMEDIRECTORY, then
*               cmake --install <Build> --config Release --component Game --prefix GAMEDIRECTORY
*   5. Compile  (installer) ISCC.exe /DAppVersion=<version> InstallScript.iss (Inno Setup 6) into
*               Build/InstallerStaging, then copies <game>_Setup.exe into the output folder
*
* Needs the machine that made the Build folder (CMake + Visual Studio, and Inno Setup 6 for the
* installer): the paths come from Editor/CMakeLists.txt. Call Poll() every frame; it moves the
* job from step to step. Cancel() stops the running process and everything it started (MSBuild,
* the compilers, ISCC).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace Crucible {

	class PackageJob {
	public:
		struct Settings {
			std::filesystem::path cmake;       ///< cmake.exe
			std::filesystem::path cpack;       ///< cpack.exe
			std::filesystem::path buildDir;    ///< the CMake build folder (has CPackConfig.cmake)
			std::filesystem::path assets;      ///< Assets folder (Cook reads it)
			std::filesystem::path resources;   ///< Resources folder (Cook fills it)
			std::filesystem::path outputDir;   ///< where the finished zip / installer is put
			std::string           version;     ///< e.g. "0.2.0" - part of the zip's name and the installer's version

			bool                  makeZip = true;         ///< step 3
			bool                  makeInstaller = false;  ///< steps 4 and 5
			std::filesystem::path installerDir;  ///< Tools/Package/Installer (InstallScript.iss, GAMEDIRECTORY, ...)
			std::filesystem::path iscc;          ///< Inno Setup 6's ISCC.exe (see FindIscc)
		};

		enum class Stage { Idle, Building, Cooking, Packing, Staging, Compiling, Done, Failed, Cancelled };

		PackageJob() = default;
		~PackageJob();   ///< cancels a running job and waits for it
		PackageJob(const PackageJob&) = delete;
		PackageJob& operator=(const PackageJob&) = delete;

		/// Starts at step 1. False (and why) if a job is running or a setting is wrong.
		bool Start(Settings settings, std::string& error);

		/// Call every frame (main thread): moves on to the next step when one finishes.
		void Poll();

		/// Stops the running step. The stage becomes Cancelled at the next Poll().
		void Cancel();

		Stage GetStage() const { return m_stage; }
		bool  IsRunning() const {
			return m_stage == Stage::Building || m_stage == Stage::Cooking || m_stage == Stage::Packing
			    || m_stage == Stage::Staging  || m_stage == Stage::Compiling;
		}
		static const char* StageText(Stage stage);   ///< "Building the game (Release)", ...

		/// For "Step 2 of 4": the running step's number and how many steps this job has (they
		/// depend on makeZip / makeInstaller). 0 when no step is running.
		int StepNumber() const;
		int StepCount() const;

		/// Moves the output written since the last call onto the end of `out`.
		void TakeNewOutput(std::string& out);

		const std::filesystem::path& ZipPath() const { return m_zip; }      ///< set when Done (if makeZip)
		const std::filesystem::path& InstallerPath() const { return m_installer; }  ///< set when Done (if makeInstaller)
		const Settings&              GetSettings() const { return m_settings; }     ///< the last job's (zip / installer / ...)
		const std::string&           Failure() const { return m_failure; }  ///< set when Failed
		double                       Seconds() const;                       ///< since Start (stops when finished)

		/// Letters, digits, '.', '-' and '_' (it goes into a file name and a command line).
		static bool IsValidVersion(std::string_view version);

		/// CPACK_PACKAGE_VERSION from <buildDir>/CPackConfig.cmake, or "" if there is none.
		static std::string ConfiguredVersion(const std::filesystem::path& buildDir);

		/// ISCC.exe: `configured` (found by CMake) if it exists, else wherever Inno Setup 6's
		/// uninstall entry in the registry says it is installed (so installing it after run.bat
		/// works too). Empty if Inno Setup 6 is not installed.
		static std::filesystem::path FindIscc(const std::filesystem::path& configured);

		/// What stops the installer being made, for the Package Game window ("" = nothing): no
		/// ISCC.exe, no InstallScript.iss / GameInfo.iss, or no REDIST\VC_redist.x64.exe.
		static std::string InstallerProblem(const std::filesystem::path& installerDir, const std::filesystem::path& iscc);

		/// Opens File Explorer with this file selected.
		static void ShowInExplorer(const std::filesystem::path& file);

		/// Opens a file in the program Windows uses for it (InstallScript.iss: the Inno Setup app).
		/// False if Windows has no program for it.
		static bool OpenWithDefaultApp(const std::filesystem::path& file);

		/// Windows' folder picker, opened at `start` (or the nearest folder above it that exists).
		/// Blocks until it closes. True with the folder in `chosen`; false if cancelled.
		static bool PickFolder(const std::filesystem::path& start, std::filesystem::path& chosen);

	private:
		void Launch(std::wstring commandLine);      // runs one process on m_worker
		void RunProcess(std::wstring commandLine);  // worker thread
		void Append(std::string_view text);         // adds to the output (any thread)
		void Finish(Stage stage, std::string failure = {});

		void RunCook();          // step 2
		void StartPacking();     // step 3 (zip)
		void CollectZip();       //   and copy it out
		void StartStaging();     // step 4 (installer): fill GAMEDIRECTORY
		void StartCompiling();   // step 5 (installer): run ISCC
		void CollectInstaller(); //   and copy it out
		void AfterCook();        // the next step once Cook is done
		void AfterZip();         // the next step once the zip is out

		Settings    m_settings;
		Stage       m_stage = Stage::Idle;
		std::string m_failure;
		std::filesystem::path m_zip;
		std::filesystem::path m_installer;
		std::chrono::steady_clock::time_point m_start{}, m_end{};

		// The running process. The worker writes m_exitCode and m_launchError before
		// m_workerDone; the main thread reads them after.
		std::thread       m_worker;
		std::atomic<bool> m_workerDone{ false };
		std::atomic<bool> m_cancelled{ false };
		int               m_exitCode = -1;
		std::string       m_launchError;
		std::string       m_firstError;   // first output line that reports an error, for Failure()

		mutable std::mutex m_mutex;       // guards m_newOutput and m_processGroup
		std::string        m_newOutput;   // not yet taken by TakeNewOutput
		void*              m_processGroup = nullptr;   // Windows job object holding the process tree
	};

}
