/*!
* @file			ImGuiPanel_PackageGame.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			8 November 2026
*
* @brief		The Package Game window - see ImGuiPanel_PackageGame.h.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/Panels/ImGuiPanel_PackageGame.h"

#include "Core.h"
#include "AssetPipe/Assetmanager.h"
#include "Configuration/EngineConfigLoader.h"
#include "Diagnostics/Logger.h"
#include "Scene/SceneManager.h"

#include <cstring>
#include <exception>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

	ImVec4 const kWarningColour{ 1.0f, 0.78f, 0.30f, 1.0f };
	ImVec4 const kErrorColour  { 1.0f, 0.45f, 0.45f, 1.0f };
	ImVec4 const kDoneColour   { 0.45f, 0.85f, 0.45f, 1.0f };

	// The Output box keeps the newest text only: a full engine rebuild prints a lot.
	constexpr std::size_t kMaxOutput = 2 * 1024 * 1024;

	// The build folder this Editor was configured from (Editor/CMakeLists.txt), or empty.
	fs::path BuildDir()
	{
#ifdef CRUCIBLE_BUILD_DIR
		return fs::path(CRUCIBLE_BUILD_DIR);
#else
		return {};
#endif
	}

	fs::path FromUtf8(char const* text)
	{
		return fs::path(std::u8string(reinterpret_cast<char8_t const*>(text)));
	}

	std::string ToUtf8(fs::path const& path)
	{
		std::u8string const text = fs::path(path).make_preferred().u8string();
		return std::string(reinterpret_cast<char const*>(text.data()), text.size());
	}

	template <std::size_t N>
	void CopyInto(char (&buffer)[N], std::string const& text)
	{
		strncpy_s(buffer, N, text.c_str(), _TRUNCATE);   // cuts a too-long text short, always ends with '\0'
	}

	int StepNumber(Crucible::PackageJob::Stage stage)
	{
		using Stage = Crucible::PackageJob::Stage;
		switch (stage) {
		case Stage::Building: return 1;
		case Stage::Cooking:  return 2;
		case Stage::Packing:  return 3;
		default:              return 0;
		}
	}

}	// anonymous namespace

void Crucible::PackageGamePanel::Init()
{
	m_panelName = "Package Game";
	m_docked = false;   // its own floating window; dock it anywhere and imgui.ini remembers
}

void Crucible::PackageGamePanel::Open()
{
	m_open = true;
	m_makeFocus = true;

	if (!m_defaultsLoaded) {
		m_defaultsLoaded = true;
		std::string version = PackageJob::ConfiguredVersion(BuildDir());
		CopyInto(m_version, version.empty() ? std::string("0.1.0") : version);
		if (!BuildDir().empty())
			CopyInto(m_outputDir, ToUtf8((BuildDir() / "Packages").make_preferred()));
	}

	// The scene the game starts in, as saved (what Cook puts in the zip).
	m_startupScene.clear();
	try {
		fs::path const config = EngineConfigLoader::FindConfigFile();
		if (!config.empty())
			m_startupScene = EngineConfigLoader::Load(config).project.startupScene;
	}
	catch (std::exception const&) {
		m_startupScene = "(EngineConfig.json could not be read)";
	}
}

void Crucible::PackageGamePanel::Update(float)
{
	// The job keeps going while the window is closed.
	m_job.Poll();
	m_job.TakeNewOutput(m_output);
	if (m_output.size() > kMaxOutput) {
		std::size_t cut = m_output.find('\n', m_output.size() - kMaxOutput);
		m_output.erase(0, cut == std::string::npos ? m_output.size() - kMaxOutput : cut + 1);
	}
	ReportFinish();

	if (!m_open)
		return;

	ImGui::SetNextWindowSize(ImVec2(660.0f, 580.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(m_panelName.c_str(), &m_open, m_flags)) {
		bool const playing = m_core.GetScenes().IsPlaySessionActive();

		ImGui::TextWrapped("Builds the game in Release, cooks Resources and zips everything a player needs. "
		                   "Send the zip: players unzip it and run Mekanomachia.exe.");
		ImGui::Separator();
		DrawSettings(playing);
		ImGui::Separator();
		DrawStatus();
		DrawOutput();
	}
	ImGui::End();
}

void Crucible::PackageGamePanel::DrawSettings(bool playing)
{
	bool const running = m_job.IsRunning();

	ImGui::BeginDisabled(running);
	ImGui::SetNextItemWidth(160.0f);
	ImGui::InputText("Version", m_version, sizeof(m_version));
	bool const validVersion = PackageJob::IsValidVersion(m_version);
	if (!validVersion) {
		ImGui::SameLine();
		ImGui::TextColored(kErrorColour, "letters, digits, '.', '-' and '_' only");
	}
	// Type a folder, or Browse... to pick one in Windows' folder picker.
	float const browseWidth = 90.0f;
	ImGui::SetNextItemWidth(-(browseWidth + ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize("Output folder").x
	                          + ImGui::GetStyle().ItemInnerSpacing.x));
	ImGui::InputText("##OutputFolder", m_outputDir, sizeof(m_outputDir));
	ImGui::SameLine();
	if (ImGui::Button("Browse...", ImVec2(browseWidth, 0.0f))) {
		fs::path chosen;
		if (PackageJob::PickFolder(FromUtf8(m_outputDir), chosen))
			CopyInto(m_outputDir, ToUtf8(chosen));
	}
	ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
	ImGui::TextUnformatted("Output folder");
	ImGui::EndDisabled();

	ImGui::TextDisabled("Starts in: %s", m_startupScene.empty() ? "(no startup scene)" : m_startupScene.c_str());
	ImGui::SameLine();
	ImGui::TextDisabled("- change it in File > Project Settings");

	// Things worth knowing before pressing Package.
	if (playing)
		ImGui::TextColored(kWarningColour, "Stop play mode to package.");
	if (m_core.GetScenes().IsDirty())
		ImGui::TextColored(kWarningColour, "The open scene has unsaved changes. The zip gets the saved file: save first (Ctrl+S) to include them.");

	ImGui::Spacing();
	if (!running) {
		ImGui::BeginDisabled(playing || !validVersion);
		if (ImGui::Button("Package", ImVec2(120.0f, 0.0f)))
			StartPackaging();
		ImGui::EndDisabled();
	}
	else if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
		m_job.Cancel();
	}

	if (m_job.GetStage() == PackageJob::Stage::Done && !m_job.ZipPath().empty()) {
		ImGui::SameLine();
		if (ImGui::Button("Open Folder"))
			PackageJob::ShowInExplorer(m_job.ZipPath());
	}

	if (!m_startError.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, kErrorColour);
		ImGui::TextWrapped("%s", m_startError.c_str());
		ImGui::PopStyleColor();
	}
}

void Crucible::PackageGamePanel::DrawStatus()
{
	PackageJob::Stage const stage = m_job.GetStage();
	double const seconds = m_job.Seconds();

	switch (stage) {
	case PackageJob::Stage::Idle:
		ImGui::TextDisabled("The first package takes several minutes (everything is compiled in Release). Later ones only rebuild what changed.");
		break;
	case PackageJob::Stage::Building:
	case PackageJob::Stage::Cooking:
	case PackageJob::Stage::Packing: {
		static char const* const dots[] = { "", ".", "..", "..." };
		int const tick = static_cast<int>(ImGui::GetTime() * 2.0) % 4;
		ImGui::Text("Step %d of 3: %s%s   %.0f s", StepNumber(stage), PackageJob::StageText(stage), dots[tick], seconds);
		break;
	}
	case PackageJob::Stage::Done:
		ImGui::TextColored(kDoneColour, "Done in %.0f s:", seconds);
		ImGui::SameLine();
		ImGui::TextUnformatted(ToUtf8(m_job.ZipPath()).c_str());
		break;
	case PackageJob::Stage::Failed:
		ImGui::PushStyleColor(ImGuiCol_Text, kErrorColour);
		ImGui::TextWrapped("Failed after %.0f s: %s", seconds, m_job.Failure().c_str());
		ImGui::PopStyleColor();
		break;
	case PackageJob::Stage::Cancelled:
		ImGui::TextDisabled("Cancelled after %.0f s.", seconds);
		break;
	}
}

void Crucible::PackageGamePanel::DrawOutput()
{
	ImGui::Spacing();
	ImGui::TextUnformatted("Output");
	ImGui::SameLine();
	if (ImGui::SmallButton("Copy"))
		ImGui::SetClipboardText(m_output.c_str());
	ImGui::SameLine();
	ImGui::BeginDisabled(m_job.IsRunning());
	if (ImGui::SmallButton("Clear"))
		m_output.clear();
	ImGui::EndDisabled();

	if (ImGui::BeginChild("##PackageOutput", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
		ImGui::TextUnformatted(m_output.data(), m_output.data() + m_output.size());
		// Follow new output while scrolled to the bottom; scrolling up stops following.
		if (m_followOutput)
			ImGui::SetScrollHereY(1.0f);
		m_followOutput = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
	}
	ImGui::EndChild();
}

void Crucible::PackageGamePanel::StartPackaging()
{
	m_startError.clear();
	m_output.clear();
	m_followOutput = true;

	PackageJob::Settings settings;
#if defined(CRUCIBLE_CMAKE_COMMAND) && defined(CRUCIBLE_CPACK_COMMAND)
	settings.cmake = fs::path(CRUCIBLE_CMAKE_COMMAND);   // set by Editor/CMakeLists.txt
	settings.cpack = fs::path(CRUCIBLE_CPACK_COMMAND);
#endif
	settings.buildDir  = BuildDir();
	settings.assets    = fs::path(m_core.GetAssets().GetAssetRoot());
	settings.resources = fs::path(m_core.GetAssets().GetResourceRoot());
	settings.outputDir = FromUtf8(m_outputDir);
	settings.version   = m_version;

	std::string error;
	if (settings.buildDir.empty())
		error = "this Editor was built without its build folder's path (Editor/CMakeLists.txt)";
	else if (settings.resources.empty())
		error = "there is no Resources folder next to Assets";
	else if (m_job.Start(std::move(settings), error))
		CRU_USER_INFO("[Package] Packaging version {}...", m_version);

	if (!error.empty()) {
		m_startError = "Could not start: " + error;
		CRU_USER_ERROR("[Package] {}", m_startError);
	}
	m_lastStage = m_job.GetStage();
}

void Crucible::PackageGamePanel::ReportFinish()
{
	PackageJob::Stage const stage = m_job.GetStage();
	if (stage == m_lastStage)
		return;
	m_lastStage = stage;

	switch (stage) {
	case PackageJob::Stage::Done:
		CRU_USER_INFO("[Package] Made {} in {:.0f} s", ToUtf8(m_job.ZipPath()), m_job.Seconds());
		break;
	case PackageJob::Stage::Failed:
		CRU_USER_ERROR("[Package] {}", m_job.Failure());
		break;
	case PackageJob::Stage::Cancelled:
		CRU_USER_WARN("[Package] Cancelled");
		break;
	default:
		break;
	}
}
