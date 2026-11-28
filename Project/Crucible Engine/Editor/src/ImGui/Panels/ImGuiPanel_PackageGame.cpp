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
#include "GameInfo.h"
#include "Scene/SceneManager.h"

#include <imgui_internal.h>   // ImGuiSettingsHandler, AddSettingsHandler, MarkIniSettingsDirty

#include <cstring>
#include <exception>
#include <filesystem>
#include <vector>

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

	// Tools/Package/Installer in this checkout (Editor/CMakeLists.txt), or empty.
	fs::path InstallerDir()
	{
#ifdef CRUCIBLE_INSTALLER_DIR
		return fs::path(CRUCIBLE_INSTALLER_DIR);
#else
		return {};
#endif
	}

	// The ISCC.exe CMake found when run.bat ran (may be empty: PackageJob::FindIscc looks further).
	fs::path ConfiguredIscc()
	{
#ifdef CRUCIBLE_ISCC_COMMAND
		return fs::path(CRUCIBLE_ISCC_COMMAND);
#else
		return {};
#endif
	}

	// ---- Remembered choices: imgui.ini ------------------------------------------------------ //
	// [CruciblePackageGame][Settings]
	// MakeZip=1
	// MakeInstaller=1
	// OutputFolder=C:\...\Packages
	//
	// imgui.ini sits beside Editor.exe in the Build folder, so these are this person's, on this
	// PC, and never reach git. Kept here rather than in the panel: ImGui writes the file when its
	// context is destroyed, after the panels are gone.
	struct SavedChoices {
		bool        makeZip = true;
		bool        makeInstaller = true;
		std::string outputFolder;   // UTF-8; empty = the default (Build/Packages)
	};
	SavedChoices g_saved;

	void* IniReadOpen(ImGuiContext*, ImGuiSettingsHandler*, char const* name)
	{
		return std::strcmp(name, "Settings") == 0 ? &g_saved : nullptr;
	}

	void IniReadLine(ImGuiContext*, ImGuiSettingsHandler*, void*, char const* line)
	{
		char const* eq = std::strchr(line, '=');
		if (eq == nullptr)
			return;
		std::string const key(line, eq);
		char const* value = eq + 1;
		if (key == "MakeZip")            g_saved.makeZip = std::strcmp(value, "0") != 0;
		else if (key == "MakeInstaller") g_saved.makeInstaller = std::strcmp(value, "0") != 0;
		else if (key == "OutputFolder")  g_saved.outputFolder = value;
	}

	void IniWriteAll(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out)
	{
		out->appendf("[%s][Settings]\n", handler->TypeName);
		out->appendf("MakeZip=%d\n", g_saved.makeZip ? 1 : 0);
		out->appendf("MakeInstaller=%d\n", g_saved.makeInstaller ? 1 : 0);
		if (!g_saved.outputFolder.empty())
			out->appendf("OutputFolder=%s\n", g_saved.outputFolder.c_str());
		out->append("\n");
	}

}	// anonymous namespace

void Crucible::PackageGamePanel::Init()
{
	m_panelName = "Package Game";
	m_docked = false;   // its own floating window; dock it anywhere and imgui.ini remembers

	// Read from imgui.ini on the first frame (Init runs before it), written whenever SaveChoices
	// marks it changed.
	ImGuiSettingsHandler handler;
	handler.TypeName   = "CruciblePackageGame";
	handler.TypeHash   = ImHashStr("CruciblePackageGame");
	handler.ReadOpenFn = IniReadOpen;
	handler.ReadLineFn = IniReadLine;
	handler.WriteAllFn = IniWriteAll;
	ImGui::AddSettingsHandler(&handler);
}

void Crucible::PackageGamePanel::SaveChoices()
{
	SavedChoices const now{ m_makeZip, m_makeInstaller, m_outputDir };
	if (now.makeZip == g_saved.makeZip && now.makeInstaller == g_saved.makeInstaller
	    && now.outputFolder == g_saved.outputFolder)
		return;
	g_saved = now;
	ImGui::MarkIniSettingsDirty();
}

void Crucible::PackageGamePanel::RefreshInstallerCheck()
{
	m_iscc = PackageJob::FindIscc(ConfiguredIscc());
	m_installerProblem = PackageJob::InstallerProblem(InstallerDir(), m_iscc);
}

void Crucible::PackageGamePanel::Open()
{
	m_open = true;
	m_makeFocus = true;

	// The game's details, fresh from GameInfo.json (someone may have changed it in git) - unless
	// there are unsaved edits in the window, which are kept.
	if (!DetailsChanged())
		LoadGameDetails();

	if (!m_defaultsLoaded) {
		m_defaultsLoaded = true;

		// The choices saved last time (imgui.ini), or the defaults.
		m_makeZip = g_saved.makeZip;
		m_makeInstaller = g_saved.makeInstaller;
		if (!g_saved.outputFolder.empty())
			CopyInto(m_outputDir, g_saved.outputFolder);
		else if (!BuildDir().empty())
			CopyInto(m_outputDir, ToUtf8((BuildDir() / "Packages").make_preferred()));
	}

	// Whether the installer can be made: looked up each time the window opens (and with Check
	// again), so installing Inno Setup or adding VC_redist.x64.exe shows up without a restart.
	RefreshInstallerCheck();

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

		ImGui::TextWrapped("Builds the game in Release, cooks Resources, then makes a zip (players unzip it and run "
		                   "%s.exe) and/or an installer (%s_Setup.exe, made with Inno Setup).", m_name, m_name);
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
	// The game's version (GameInfo.json - the same box as in Installer details). Package saves it
	// first, so the .exe, the zip and the installer all get it.
	ImGui::SetNextItemWidth(160.0f);
	ImGui::InputText("Version", m_version, sizeof(m_version));
	std::string const versionProblem = GameDetails::CheckVersion(m_version);
	bool const validVersion = versionProblem.empty();
	if (!validVersion) {
		ImGui::SameLine();
		ImGui::TextColored(kErrorColour, "%s", versionProblem.c_str());
	}
	else if (DetailsChanged()) {
		ImGui::SameLine();
		ImGui::TextColored(kWarningColour, "unsaved - Package saves it");
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

	ImGui::Checkbox("Zip", &m_makeZip);
	ImGui::SameLine();
	// While the installer cannot be made the box is greyed out and shown unticked; the choice
	// itself is kept, so it comes back on its own once the problem is fixed.
	bool const installerAvailable = m_installerProblem.empty();
	ImGui::BeginDisabled(!installerAvailable);
	bool shownInstaller = m_makeInstaller && installerAvailable;
	if (ImGui::Checkbox("Installer", &shownInstaller))
		m_makeInstaller = shownInstaller;
	ImGui::EndDisabled();
	if (!installerAvailable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("%s", m_installerProblem.c_str());
	ImGui::EndDisabled();
	SaveChoices();

	ImGui::TextDisabled("Starts in: %s", m_startupScene.empty() ? "(no startup scene)" : m_startupScene.c_str());
	ImGui::SameLine();
	ImGui::TextDisabled("- change it in File > Project Settings");

	// Why the installer cannot be made, and a way to look again once it is fixed.
	if (!installerAvailable) {
		ImGui::PushStyleColor(ImGuiCol_Text, kWarningColour);
		ImGui::TextWrapped("Installer unavailable: %s", m_installerProblem.c_str());
		ImGui::PopStyleColor();
		ImGui::BeginDisabled(running);
		if (ImGui::SmallButton("Check again"))
			RefreshInstallerCheck();
		ImGui::EndDisabled();
	}

	DrawInstallerDetails();

	// Things worth knowing before pressing Package.
	if (playing)
		ImGui::TextColored(kWarningColour, "Stop play mode to package.");
	if (m_core.GetScenes().IsDirty())
		ImGui::TextColored(kWarningColour, "The open scene has unsaved changes. The zip gets the saved file: save first (Ctrl+S) to include them.");

	ImGui::Spacing();
	if (!running) {
		bool const nothingChosen = !m_makeZip && !(m_makeInstaller && m_installerProblem.empty());
		bool const detailsOk = m_detailsLoaded && EditedDetails().IsValid();
		ImGui::BeginDisabled(playing || !validVersion || !detailsOk || nothingChosen);
		if (ImGui::Button("Package", ImVec2(120.0f, 0.0f)))
			StartPackaging();
		ImGui::EndDisabled();
		if (nothingChosen) {
			ImGui::SameLine();
			ImGui::TextColored(kWarningColour, "Tick Zip, Installer or both.");
		}
		else if (!detailsOk) {
			ImGui::SameLine();
			ImGui::TextColored(kWarningColour, "Fix the game details under Installer details.");
		}
	}
	else if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
		m_job.Cancel();
	}

	if (m_job.GetStage() == PackageJob::Stage::Done) {
		fs::path const& made = !m_job.InstallerPath().empty() ? m_job.InstallerPath() : m_job.ZipPath();
		if (!made.empty()) {
			ImGui::SameLine();
			if (ImGui::Button("Open Folder"))
				PackageJob::ShowInExplorer(made);
		}
	}

	if (!m_startError.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, kErrorColour);
		ImGui::TextWrapped("%s", m_startError.c_str());
		ImGui::PopStyleColor();
	}
}

void Crucible::PackageGamePanel::DrawProcess()
{
	// While a job runs (or after it), its own steps; otherwise the ones the ticks would run.
	bool const jobShown = m_job.IsRunning() || m_job.GetStage() == PackageJob::Stage::Done;
	bool const zip       = jobShown ? m_job.GetSettings().makeZip       : m_makeZip;
	bool const installer = jobShown ? m_job.GetSettings().makeInstaller : (m_makeInstaller && m_installerProblem.empty());
	std::string const name = m_name;
	std::string const version = m_version;

	struct Step { PackageJob::Stage stage; std::string label; std::string tooltip; };
	std::vector<Step> steps;
	steps.push_back({ PackageJob::Stage::Building, "Build",
		"cmake --build Build --config Release --target Game\n"
		"Compiles the game, and any changed shaders, models and textures.\n"
		"Re-runs CMake first if Assets/Config/GameInfo.json changed." });
	steps.push_back({ PackageJob::Stage::Cooking, "Cook",
		"Copies everything the game reads from Assets into Resources\n"
		"(compiled models, textures, shaders, scenes, prefabs, config).\n"
		"Stops if anything is missing or failed to compile." });
	if (zip)
		steps.push_back({ PackageJob::Stage::Packing, "Zip",
			"cpack: zips the game's files into " + name + "-" + version + "-win64.zip,\n"
			"then copies it into the output folder." });
	if (installer) {
		steps.push_back({ PackageJob::Stage::Staging, "Fill GAMEDIRECTORY",
			"Empties Tools/Package/Installer/GAMEDIRECTORY, then copies the game into it\n"
			"(cmake --install ... --component Game): exactly what the installer installs." });
		steps.push_back({ PackageJob::Stage::Compiling, "Inno Setup",
			"ISCC.exe compiles InstallScript.iss: GAMEDIRECTORY, the DigiPen EULA, the icon\n"
			"and the VC++ redistributable into " + name + "_Setup.exe (version " + version + "),\n"
			"then copies it into the output folder." });
	}
	steps.push_back({ PackageJob::Stage::Done, "Output folder",
		std::string("Where the finished files go:\n") + m_outputDir });

	// Done steps green, the running one highlighted, the rest greyed.
	int const current = m_job.IsRunning() ? m_job.StepNumber() : 0;   // 1-based, among the steps above
	bool const finished = m_job.GetStage() == PackageJob::Stage::Done;

	ImGui::TextUnformatted("Process:");
	ImGui::SameLine();
	for (std::size_t i = 0; i < steps.size(); ++i) {
		int const number = static_cast<int>(i) + 1;
		std::string const label = std::to_string(number) + ". " + steps[i].label;
		ImVec4 colour = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
		if (finished || (current > 0 && number < current))
			colour = kDoneColour;
		else if (number == current)
			colour = kWarningColour;

		// Wrap to the next line rather than off the edge of the window.
		if (i > 0) {
			ImGui::SameLine();
			float const needed = ImGui::CalcTextSize(" > ").x + ImGui::CalcTextSize(label.c_str()).x
			                   + ImGui::GetStyle().ItemSpacing.x * 2.0f;
			if (ImGui::GetContentRegionAvail().x < needed)
				ImGui::NewLine();
			else {
				ImGui::TextDisabled(">");
				ImGui::SameLine();
			}
		}
		ImGui::TextColored(colour, "%s", label.c_str());
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", steps[i].tooltip.c_str());
	}
	ImGui::TextDisabled("Hover a step for what it does. The steps follow the Zip and Installer ticks.");
}

void Crucible::PackageGamePanel::DrawInstallerDetails()
{
	// Closed by default, unless the details have a problem that stops Package.
	bool const detailsBroken = !m_detailsError.empty() || (m_detailsLoaded && !EditedDetails().IsValid());
	if (detailsBroken)
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);
	if (!ImGui::CollapsingHeader("Installer details"))
		return;

	ImGui::Indent();
	DrawProcess();
	ImGui::Separator();
	DrawGameDetails();
	ImGui::Spacing();
	ImGui::Separator();

	std::string const name = m_name;
	std::string const folder = m_installFolder;
	std::string const installPath = folder.empty() ? name : folder + "\\" + name;
	fs::path const script = InstallerDir() / "InstallScript.iss";

	ImGui::Text("Setup file:");
	ImGui::SameLine();
	ImGui::TextDisabled("%s_Setup.exe", name.c_str());

	// {autopf} in InstallScript.iss: which one depends on the player's choice in the installer.
	ImGui::Text("Installs to:");
	ImGui::SameLine();
	ImGui::TextDisabled("Program Files\\%s (all users), or AppData\\Local\\Programs\\%s (just me)",
	                    installPath.c_str(), installPath.c_str());
	ImGui::Text("Settings in:");
	ImGui::SameLine();
	ImGui::TextDisabled("%%LOCALAPPDATA%%\\%s (removed by the uninstaller)", name.c_str());

	ImGui::Text("Inno Setup:");
	ImGui::SameLine();
	if (m_iscc.empty())
		ImGui::TextColored(kWarningColour, "not found");
	else
		ImGui::TextDisabled("%s", ToUtf8(m_iscc).c_str());

	ImGui::Text("Script:");
	ImGui::SameLine();
	ImGui::TextDisabled("%s", ToUtf8(script).c_str());

	std::error_code ec;
	bool const haveScript = !InstallerDir().empty() && fs::is_regular_file(script, ec);
	ImGui::BeginDisabled(!haveScript);
	if (ImGui::Button("Open InstallScript.iss")) {
		// Opens in the Inno Setup app when it is installed; otherwise show the file instead.
		if (!PackageJob::OpenWithDefaultApp(script))
			PackageJob::ShowInExplorer(script);
	}
	ImGui::SameLine();
	if (ImGui::Button("Show installer folder"))
		PackageJob::ShowInExplorer(script);
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(m_job.IsRunning());
	if (ImGui::Button("Check again##Details"))   // ##: a second "Check again" may be above
		RefreshInstallerCheck();
	ImGui::EndDisabled();

	ImGui::Unindent();
}

// ---- The game's details (Assets/Config/GameInfo.json) ------------------------------------------ //

std::filesystem::path Crucible::PackageGamePanel::GameDetailsFile() const
{
	return GameDetails::File(fs::path(m_core.GetAssets().GetAssetRoot()));
}

void Crucible::PackageGamePanel::LoadGameDetails()
{
	GameDetails details;
	std::string error;
	m_detailsLoaded = GameDetails::Load(GameDetailsFile(), details, error);
	m_detailsError = m_detailsLoaded ? std::string{} : error;
	if (!m_detailsLoaded)
		return;
	m_savedDetails = details;
	CopyInto(m_name, details.name);
	CopyInto(m_version, details.version);
	CopyInto(m_publisher, details.publisher);
	CopyInto(m_installFolder, details.installFolder);
}

Crucible::GameDetails Crucible::PackageGamePanel::EditedDetails() const
{
	GameDetails details;
	details.name          = m_name;
	details.version       = m_version;
	details.publisher     = m_publisher;
	details.installFolder = m_installFolder;
	details.appId         = m_savedDetails.appId;   // shown, never edited here
	return details;
}

bool Crucible::PackageGamePanel::SaveGameDetails()
{
	GameDetails const details = EditedDetails();
	std::string error;
	if (!details.Save(GameDetailsFile(), error)) {
		m_detailsError = error;
		CRU_USER_ERROR("[Package] {}", error);
		return false;
	}
	m_detailsError.clear();
	if (details.name != m_savedDetails.name)
		CRU_USER_INFO("[Package] Renamed the game from {} to {}", m_savedDetails.name, details.name);
	m_savedDetails = details;
	CRU_USER_INFO("[Package] Saved the game's details: {} {} by {}", details.name, details.version, details.publisher);
	return true;
}

void Crucible::PackageGamePanel::DrawGameDetails()
{
	ImGui::TextUnformatted("Game details");
	ImGui::SameLine();
	ImGui::TextDisabled("- Assets/Config/GameInfo.json, shared with the team through git");

	if (!m_detailsLoaded) {
		ImGui::PushStyleColor(ImGuiCol_Text, kErrorColour);
		ImGui::TextWrapped("%s", m_detailsError.c_str());
		ImGui::PopStyleColor();
		if (ImGui::SmallButton("Read it again"))
			LoadGameDetails();
		return;
	}

	bool const running = m_job.IsRunning();
	float const labelWidth = ImGui::CalcTextSize("Install folder").x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
	float const boxWidth = 260.0f;
	float const boxX = ImGui::GetCursorPosX() + labelWidth;   // after Indent: the box column

	// One labelled text box, with what is wrong with it (if anything) beside it.
	auto field = [&](char const* label, char const* id, char* buffer, std::size_t size,
	                 std::string const& problem, char const* tooltip) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", tooltip);
		ImGui::SameLine(boxX);
		ImGui::SetNextItemWidth(boxWidth);
		ImGui::InputText(id, buffer, size);
		if (!problem.empty()) {
			ImGui::SameLine();
			ImGui::TextColored(kErrorColour, "%s", problem.c_str());
		}
	};

	ImGui::BeginDisabled(running);
	field("Name", "##GameName", m_name, sizeof(m_name), GameDetails::CheckName(m_name),
	      "The .exe's name, the window title, the installer's name, and the install and AppData folders.");
	field("Version", "##GameVersion", m_version, sizeof(m_version), GameDetails::CheckVersion(m_version),
	      "major.minor.patch, e.g. 0.2.0. The same as the Version box at the top.");
	field("Publisher", "##GamePublisher", m_publisher, sizeof(m_publisher), GameDetails::CheckPublisher(m_publisher),
	      "Shown in the installer and in Windows' Settings > Apps.");
	field("Install folder", "##GameInstallFolder", m_installFolder, sizeof(m_installFolder),
	      GameDetails::CheckInstallFolder(m_installFolder),
	      "The folder under Program Files the game goes in (Program Files\\<this>\\<name>). May be empty.");
	ImGui::EndDisabled();

	// The AppId: shown, never edited here - changing it breaks upgrades of installed copies.
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("AppId");
	ImGui::SameLine(boxX);
	ImGui::TextDisabled("{%s}", m_savedDetails.appId.c_str());
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("How Windows knows a new version is the same game, so it upgrades it instead of\n"
		                  "installing a second copy. It never changes, so it cannot be edited here.\n"
		                  "(\"appId\" in Assets/Config/GameInfo.json - only a brand-new game gets a new one.)");
	ImGui::SameLine();
	if (ImGui::SmallButton("Copy##AppId"))
		ImGui::SetClipboardText(m_savedDetails.appId.c_str());
	std::string const appIdProblem = GameDetails::CheckAppId(m_savedDetails.appId);
	if (!appIdProblem.empty()) {
		ImGui::SameLine();
		ImGui::TextColored(kErrorColour, "%s - fix \"appId\" in GameInfo.json", appIdProblem.c_str());
	}

	// What a rename does, before it is saved (and after, until the Editor is rebuilt).
	bool const renaming = std::string(m_name) != m_savedDetails.name;
	if (renaming && GameDetails::CheckName(m_name).empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, kWarningColour);
		ImGui::TextWrapped("Renaming changes the .exe's name, the install folder and the AppData folder: players' "
		                   "old settings stay in %%LOCALAPPDATA%%\\%s. Installed copies still upgrade (same AppId).",
		                   m_savedDetails.name.c_str());
		ImGui::PopStyleColor();
	}
	else if (m_savedDetails.name != Crucible::GameInfo::Name) {
		ImGui::TextDisabled("This Editor was built as %s; it shows %s everywhere after its next rebuild.",
		                    Crucible::GameInfo::Name, m_savedDetails.name.c_str());
	}

	bool const changed = DetailsChanged();
	bool const valid = EditedDetails().IsValid();
	ImGui::BeginDisabled(running || !changed || !valid);
	if (ImGui::Button("Save details"))
		SaveGameDetails();
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(running || !changed);
	if (ImGui::Button("Revert"))
		LoadGameDetails();
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (changed)
		ImGui::TextColored(kWarningColour, "Unsaved changes - Package saves them first.");
	else
		ImGui::TextDisabled("Saved. The next build (Package starts one) uses them everywhere.");

	if (!m_detailsError.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, kErrorColour);
		ImGui::TextWrapped("%s", m_detailsError.c_str());
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
	case PackageJob::Stage::Packing:
	case PackageJob::Stage::Staging:
	case PackageJob::Stage::Compiling: {
		static char const* const dots[] = { "", ".", "..", "..." };
		int const tick = static_cast<int>(ImGui::GetTime() * 2.0) % 4;
		ImGui::Text("Step %d of %d: %s%s   %.0f s", m_job.StepNumber(), m_job.StepCount(),
		            PackageJob::StageText(stage), dots[tick], seconds);
		break;
	}
	case PackageJob::Stage::Done:
		ImGui::TextColored(kDoneColour, "Done in %.0f s:", seconds);
		if (!m_job.ZipPath().empty())
			ImGui::TextUnformatted(("  Zip:       " + ToUtf8(m_job.ZipPath())).c_str());
		if (!m_job.InstallerPath().empty())
			ImGui::TextUnformatted(("  Installer: " + ToUtf8(m_job.InstallerPath())).c_str());
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

	// Unsaved game details are saved first: the build that starts next re-runs CMake because
	// GameInfo.json changed, so the .exe, the zip and the installer all get the new values.
	if (DetailsChanged() && !SaveGameDetails()) {
		m_startError = "Could not start: " + m_detailsError;
		return;
	}

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
	settings.makeZip       = m_makeZip;
	settings.makeInstaller = m_makeInstaller && m_installerProblem.empty();   // as shown: greyed out = off
	settings.installerDir  = InstallerDir();
	settings.iscc          = m_iscc;

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
		if (!m_job.ZipPath().empty())
			CRU_USER_INFO("[Package] Made {}", ToUtf8(m_job.ZipPath()));
		if (!m_job.InstallerPath().empty())
			CRU_USER_INFO("[Package] Made {}", ToUtf8(m_job.InstallerPath()));
		CRU_USER_INFO("[Package] Done in {:.0f} s", m_job.Seconds());
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
