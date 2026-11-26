#pragma once
/*!
* @file			ImGuiPanel_PackageGame.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			8 November 2026
*
* @brief		The Package Game window (File > Package Game...): makes the zip players get.
*
* Pick a version and an output folder, press Package. The window shows each step's progress
* and the build output while the Editor keeps running (Packaging/PackageJob.h does the work),
* then Open Folder shows the finished zip. A job keeps going when the window is closed.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/ImGuiManager.h"
#include "Packaging/PackageJob.h"

#include <filesystem>
#include <string>

namespace Crucible {

	class Core;

	class PackageGamePanel : public EditorPanel {
	public:
		explicit PackageGamePanel(Core& core) : m_core(core) {}

		// REQUIRED Overrides
		void Init() override;
		void Update(float dt) override;

		void Open();
		bool IsOpen() const { return m_open; }
		void SetOpen(bool open) { m_open = open; }
		bool IsPackaging() const { return m_job.IsRunning(); }

	private:
		void StartPackaging();
		void DrawSettings(bool playing);
		void DrawStatus();
		void DrawOutput();
		void ReportFinish();   // one Console line when a job ends

		Core&       m_core;
		PackageJob  m_job;
		bool        m_open = false;
		bool        m_defaultsLoaded = false;
		PackageJob::Stage m_lastStage = PackageJob::Stage::Idle;

		char        m_version[33] = "";
		char        m_outputDir[512] = "";
		bool        m_makeZip = true;
		bool        m_makeInstaller = true;
		std::filesystem::path m_iscc;          // Inno Setup's ISCC.exe, looked up on Open
		std::string m_installerProblem;        // why the installer cannot be made ("" = it can)
		std::string m_startupScene;      // from Assets/Config/EngineConfig.json, read on Open
		std::string m_startError;        // why the last Package press did not start

		std::string m_output;            // everything the job printed
		bool        m_followOutput = true;   // keep the output scrolled to the newest line
	};

}
