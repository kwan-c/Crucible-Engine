/*!
* @file			ImGuiPanel_AssetBrowser.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			17 September 2026
*
* @brief		The editor's Asset Browser (Unity's "Project" window).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/Panels/ImGuiPanel_AssetBrowser.h"
#include "ImGui/EditorWidgets.h"
#include "AssetPipe/AssetRegistry.h"
#include "Diagnostics/Logger.h"
#include "ECS/ECSAudioSystem.h"
#include "ImGui/Panels/ImGuiPanel_Inspector.h"
#include "Core.h"
#include "ImGui/Panels/ImGuiPanel_Menu.h"
#include "ImGui/Panels/ImGuiPanel_FontImporter.h"
#include "ImGui/Panels/ImGuiPanel_TextureImporter.h"
#include "ImGui/Panels/ImGuiPanel_Animator.h"
#include "ImGui/Panels/ImGuiPanel_BehaviorGraph.h"
#include "Graph/GraphAssets.h"   // Create State Graph / Behavior Graph
#include "ImGui/Panels/ImGuiPanel_StateGraph.h"
#include "Json/JsonWrapper.h"
#include "Scene/SceneManager.h"
#include "Prefabs/PrefabAuthoring.h"
#include "Scene/Prefab.h"   // kPrefabExtension
#include "Scripting/ScriptAuthoring.h"
#include "Resources/ResourceManager.h"   // hot reload after a refresh

#include <GLFW/glfw3.h>   // glfwGetWindowAttrib: refresh when the editor gets the focus back

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>

#ifdef _WIN32
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
	#include <shellapi.h>
#endif

namespace fs = std::filesystem;

// ------------------------------------------------------------------ helpers
namespace {

	std::string ToLowerCopy(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	// "Textures/Characters" -> "Textures",  "Textures" -> ""
	std::string ParentOf(std::string const& virtualPath)
	{
		std::size_t const slash = virtualPath.find_last_of('/');
		return (slash == std::string::npos) ? std::string{} : virtualPath.substr(0, slash);
	}

	// True if 'folder' is 'path' itself or one of its parent folders ("" is everyone's parent).
	bool IsAncestorOrSelf(std::string const& folder, std::string const& path)
	{
		if (folder.empty() || folder == path) return true;
		return path.size() > folder.size()
			&& path.compare(0, folder.size(), folder) == 0
			&& path[folder.size()] == '/';
	}

	// Label drawn on a file tile: the whole extension ("PNG", "CFONT"), or the type name if there
	// is none. Drawn smaller when it is too wide for the tile (see DrawTile), never cut short.
	std::string MakeBadge(std::string const& extension, Crucible::AssetType type)
	{
		std::string badge;
		for (char c : extension) {
			if (c == '.') continue;
			badge.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
		}
		return badge.empty() ? std::string{ Crucible::ToString(type) } : badge;
	}

	ImU32 TypeColor(Crucible::AssetType type)
	{
		using Crucible::AssetType;
		switch (type) {
		case AssetType::Texture:  return IM_COL32(86, 160, 96, 255);
		case AssetType::Model:    return IM_COL32(80, 130, 210, 255);
		case AssetType::Shader:   return IM_COL32(170, 100, 200, 255);
		case AssetType::Audio:    return IM_COL32(220, 130, 60, 255);
		case AssetType::Scene:    return IM_COL32(200, 80, 80, 255);
		case AssetType::Prefab:   return IM_COL32(70, 175, 190, 255);
		case AssetType::Font:     return IM_COL32(140, 140, 150, 255);
		case AssetType::Material: return IM_COL32(200, 170, 70, 255);
		case AssetType::Script:   return IM_COL32(100, 185, 120, 255);
		case AssetType::Text:     return IM_COL32(150, 150, 165, 255);
		case AssetType::AnimatorController: return IM_COL32(170, 120, 220, 255);
		default:                  return IM_COL32(90, 90, 95, 255);
		}
	}

	std::string FormatBytes(std::uintmax_t bytes)
	{
		char buf[32];
		if (bytes < 1024)
			std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
		else if (bytes < 1024ull * 1024ull)
			std::snprintf(buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / 1024.0);
		else
			std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
		return buf;
	}

	// Files that belong to another asset and are edited through it, not on their own: hidden from
	// the browser like .meta files. A font's import settings ("Arial.ttf.fontsettings") are edited
	// in the Font Importer.
	// Their suffixes, appended to the owning asset's full name ("Arial.ttf" + ".fontsettings").
	// Deleting the asset deletes these too (DeleteOnDisk), since nothing else can.
	constexpr std::string_view kCompanionSuffixes[] = { ".fontsettings", ".texsettings" };   // .texsettings: the Texture Importer

	bool IsHiddenCompanionFile(std::string const& name)
	{
		auto const& kSuffixes = kCompanionSuffixes;
		std::string const lower = ToLowerCopy(name);
		for (std::string_view suffix : kSuffixes) {
			if (lower.size() >= suffix.size() && lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0) return true;
		}
		return false;
	}

	// Number of bytes in the UTF-8 character starting with lead byte c.
	std::size_t Utf8Length(unsigned char c)
	{
		if (c < 0x80) return 1;
		if ((c & 0xE0) == 0xC0) return 2;
		if ((c & 0xF0) == 0xE0) return 3;
		if ((c & 0xF8) == 0xF0) return 4;
		return 1;
	}

	// Byte offsets where a character starts (so a line is never cut inside a UTF-8 sequence).
	std::vector<std::size_t> CharacterStarts(std::string const& text)
	{
		std::vector<std::size_t> starts;
		for (std::size_t i = 0; i < text.size(); i += Utf8Length(static_cast<unsigned char>(text[i]))) starts.push_back(i);
		starts.push_back(text.size());
		return starts;
	}

	float TextWidth(std::string_view text)
	{
		return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
	}

	/*
	 * A tile's name in up to two lines of maxWidth pixels:
	 *   - fits on one line: as it is;
	 *   - fits on two: split, preferring just after a '_', '-', ' ' or '.' near the end of line one;
	 *   - longer: line one is the start, line two is "..." plus the END of the name - so the part
	 *     that tells similar files apart ("...Bold.ttf" / "...Italic.ttf") and the extension stay
	 *     visible. The full name is in the tooltip.
	 */
	std::vector<std::string> TileLabel(std::string const& name, float maxWidth)
	{
		if (TextWidth(name) <= maxWidth) return { name };
		std::vector<std::size_t> const starts = CharacterStarts(name);

		// Line one: the longest start that fits...
		std::size_t cut = 0;   // index into starts
		for (std::size_t i = 1; i < starts.size(); ++i) {
			if (TextWidth(std::string_view(name).substr(0, starts[i])) > maxWidth) break;
			cut = i;
		}
		if (cut == 0) cut = 1;   // even one character is too wide: show it anyway
		// ...pulled back to just after a separator, if one is in its last 40%.
		for (std::size_t i = cut; i > cut * 3 / 5 && i > 1; --i) {
			char const c = name[starts[i] - 1];
			if (c == '_' || c == '-' || c == ' ' || c == '.') { cut = i; break; }
		}
		std::string const first = name.substr(0, starts[cut]);
		std::string const rest = name.substr(starts[cut]);
		if (TextWidth(rest) <= maxWidth) return { first, rest };

		// Line two: "..." + the longest end of the name that fits.
		std::vector<std::size_t> const restStarts = CharacterStarts(rest);
		std::string tail = "...";
		for (std::size_t i = restStarts.size() - 1; i-- > 0;) {
			std::string const candidate = "..." + rest.substr(restStarts[i]);
			if (TextWidth(candidate) > maxWidth) break;
			tail = candidate;
		}
		return { first, tail };
	}

	// Opens Explorer on a folder, or with a file selected.
	void ShowInExplorer(fs::path const& path, bool selectFile)
	{
#ifdef _WIN32
		std::wstring const p = path.wstring();
		if (selectFile) {
			std::wstring const args = L"/select,\"" + p + L"\"";
			ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
		}
		else {
			ShellExecuteW(nullptr, L"explore", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
		}
#else
		(void)path; (void)selectFile;
		CRU_WARN("[AssetBrowser] Show in Explorer is only implemented on Windows");
#endif
	}

	// ------------------------------------------------------------------ rename helpers

	std::string Trim(std::string const& s)
	{
		std::size_t const first = s.find_first_not_of(" \t");
		if (first == std::string::npos) return {};
		std::size_t const last = s.find_last_not_of(" \t");
		return s.substr(first, last - first + 1);
	}

	// nullptr if 'name' is a usable file or folder name on Windows, otherwise why it is not.
	char const* InvalidNameReason(std::string const& name)
	{
		if (name.empty()) return "the name is empty";
		if (name.find_first_of("\\/:*?\"<>|") != std::string::npos) return "names cannot contain \\ / : * ? \" < > |";
		for (char const c : name)
			if (static_cast<unsigned char>(c) < 32) return "names cannot contain control characters";
		if (name.back() == '.' || name.back() == ' ') return "names cannot end with a dot or a space";

		// CON, PRN, AUX, NUL, COM1-9, LPT1-9 - with any extension - are device names on Windows.
		std::string stem = ToLowerCopy(name.substr(0, name.find('.')));
		static constexpr std::string_view kReserved[] = { "con", "prn", "aux", "nul" };
		for (std::string_view const r : kReserved)
			if (stem == r) return "that name is reserved by Windows";
		if (stem.size() == 4 && (stem.rfind("com", 0) == 0 || stem.rfind("lpt", 0) == 0) && stem[3] >= '1' && stem[3] <= '9')
			return "that name is reserved by Windows";
		return nullptr;
	}

	// Two absolute paths naming the same location, ignoring separator style and (Windows
	// file systems being case-insensitive) case. Works for paths that no longer exist.
	std::string PathKey(fs::path const& p)
	{
		return ToLowerCopy(fs::path(p).lexically_normal().make_preferred().string());
	}

	bool SamePath(fs::path const& a, fs::path const& b) { return PathKey(a) == PathKey(b); }

	// ------------------------------------------------------------------ delete helpers

	bool IsSceneFile(std::string const& virtualPath)
	{
		return ToLowerCopy(fs::path(virtualPath).extension().string()) == ".scene";
	}

	// Text files that can refer to other assets: by guid (scenes and prefabs store it as a
	// number, e.g. "modelAsset": 1118...) or by path (materials name their textures).
	bool CanReferenceAssets(fs::path const& p)
	{
		std::string const ext = ToLowerCopy(p.extension().string());
		return ext == ".scene" || ext == Crucible::kPrefabExtension || ext == ".mat"
			|| ext == ".animcontroller" || ext == ".json";
	}

	// Removes one file, or a folder with everything in it. On Windows it goes to the Recycle Bin, so a mistaken delete can be
	// restored from there (Unity does the same); elsewhere it is deleted outright.
	bool RemoveFile(fs::path const& path, std::string& error)
	{
#ifdef _WIN32
		std::error_code ec;
		fs::path const full = fs::absolute(path, ec);   // SHFileOperation needs a full path
		// pFrom is a list of paths, each null-terminated, ending in an extra null: the one
		// pushed here, plus the one c_str() adds.
		std::wstring from = (ec ? path : full).wstring();
		from.push_back(L'\0');

		SHFILEOPSTRUCTW op{};
		op.wFunc  = FO_DELETE;
		op.pFrom  = from.c_str();
		op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
		int const result = SHFileOperationW(&op);
		if (result != 0 || op.fAnyOperationsAborted) {
			error = "it could not be moved to the Recycle Bin (error " + std::to_string(result) + ")";
			return false;
		}
		return true;
#else
		std::error_code ec;
		if (fs::remove_all(path, ec) == 0 || ec) {
			error = ec ? ec.message() : "the file does not exist";
			return false;
		}
		return true;
#endif
	}

	// True if 'inner' is 'folder' itself or anything below it.
	bool IsInside(fs::path const& inner, fs::path const& folder)
	{
		std::string const in = PathKey(inner), f = PathKey(folder);
		return in == f || (in.size() > f.size() && in.compare(0, f.size(), f) == 0
			&& (in[f.size()] == '\\' || in[f.size()] == '/'));
	}

	// ------------------------------------------------------------------ VS Code

#ifdef _WIN32
	// Starts a program without waiting for it. hideWindow: no console flash (for cmd.exe).
	bool LaunchDetached(std::wstring commandLine, bool hideWindow)
	{
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
			hideWindow ? CREATE_NO_WINDOW : 0, nullptr, nullptr, &startup, &process))
			return false;
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		return true;
	}

	// Code.exe in its usual install places (per-user first, then machine-wide), or empty.
	fs::path FindVSCodeExe()
	{
		struct Place { wchar_t const* variable; wchar_t const* subPath; };
		static constexpr Place kPlaces[] = {
			{ L"LOCALAPPDATA",      L"Programs\\Microsoft VS Code\\Code.exe" },
			{ L"ProgramFiles",      L"Microsoft VS Code\\Code.exe" },
			{ L"ProgramFiles(x86)", L"Microsoft VS Code\\Code.exe" },
		};
		wchar_t buffer[MAX_PATH];
		for (Place const& place : kPlaces) {
			DWORD const length = GetEnvironmentVariableW(place.variable, buffer, MAX_PATH);
			if (length == 0 || length >= MAX_PATH) continue;
			fs::path const exe = fs::path(buffer) / place.subPath;
			std::error_code ec;
			if (fs::is_regular_file(exe, ec)) return exe;
		}
		return {};
	}
#endif

	// Opens one file in VS Code (just the file, no folder). Falls back to whatever Windows
	// has associated with the extension when VS Code cannot be found.
	void OpenInVSCode(fs::path const& file)
	{
#ifdef _WIN32
		std::wstring const quotedFile = L"\"" + file.wstring() + L"\"";
		std::string const name = file.filename().string();

		// 1. A standard install: start Code.exe directly - no console window at all.
		if (fs::path const exe = FindVSCodeExe(); !exe.empty()
			&& LaunchDetached(L"\"" + exe.wstring() + L"\" " + quotedFile, false)) {
			CRU_USER_INFO("Opened {} in VS Code", name);
			return;
		}

		// 2. A custom install location with 'code' on PATH: code.cmd is a batch file, so it
		//    runs through cmd.exe, hidden. The doubled quotes are cmd /c's quoting rule.
		wchar_t found[MAX_PATH];
		if (SearchPathW(nullptr, L"code.cmd", nullptr, MAX_PATH, found, nullptr) > 0
			&& LaunchDetached(L"cmd.exe /c \"\"" + std::wstring(found) + L"\" " + quotedFile + L"\"", true)) {
			CRU_USER_INFO("Opened {} in VS Code", name);
			return;
		}

		// 3. No VS Code: the default program for the extension, so the click still does something.
		CRU_USER_WARN("VS Code was not found (install it, or put 'code' on PATH) - opening {} with the default program", name);
		if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
			CRU_USER_WARN("Could not open {}", name);
#else
		CRU_WARN("[AssetBrowser] Opening scripts in VS Code is only implemented on Windows ({})", file.string());
#endif
	}
}

// ------------------------------------------------------------------ panel lifetime

void Crucible::AssetBrowser::Init() {
	m_panelName = "Asset Browser";

	// Bottom strip under the Scene view and Hierarchy (Unity default), 30% of the height.
	m_docked = true;
	m_dockArea = DockArea::Bottom;
	m_dockRatio = 0.30f;
	m_makeFocus = true;   // be the visible tab in the bottom area (Console is behind it)

	// Core's shared AssetManager, already initialised by Core::Init(). A private copy here used
	// to run its own reconcile (writing .meta files a second time) and went stale whenever this
	// panel created a file, so the engine could not see what the browser showed.
	m_ready = m_assets.IsInitialized() || m_assets.Init();
	if (m_ready)
		RebuildFolderTree();
	else
		CRU_WARN("[AssetBrowser] AssetManager::Init() failed: Assets folder not found");
}

bool Crucible::AssetBrowser::IsRefreshAllowed() const {
	Core* core = EditorManager::GetInstance().GetCore();
	return core == nullptr || (!core->IsInPlaySession() && !core->HasPendingPlayRequest());
}

void Crucible::AssetBrowser::Refresh() {
	if (!m_ready || !IsRefreshAllowed()) return;
	m_assets.Refresh();
	RebuildFolderTree();
	m_listingDirty = true;

	// Hot reload: loaded assets whose file changed on disk are loaded again, and failed loads
	// may try again (an image that was missing has been compiled since, ...).
	if (ResourceManager* resources = EditorManager::GetInstance().GetResources()) {
		resources->RefreshCatalog();
		resources->ReloadChanged();
	}

	// A .cprefab changed on disk (a git pull, another scene's Apply): bring the open scene's
	// instances of it up to date, keeping their overrides. One undo step; nothing if none changed.
	if (Core* core = EditorManager::GetInstance().GetCore()) { PrefabAuthoring::UpdateChangedInstances(core->GetWorld()); }
}

void Crucible::AssetBrowser::Reveal(std::string const& virtualPath) {
	if (virtualPath.empty()) return;
	NavigateTo(ParentOf(virtualPath));   // applied after drawing; also clears the search
	m_selected = virtualPath;
	m_syncTree = true;
	ImGui::SetWindowFocus(m_panelName.c_str());
}

void Crucible::AssetBrowser::Update(float) {
	// The editor's window got the focus back - say after saving an image or a .mat in another
	// program: refresh, so the change shows up (hot reload) without pressing F5.
	GLFWwindow* window = static_cast<GLFWwindow*>(ImGui::GetMainViewport()->PlatformHandle);
	bool const appFocused = window != nullptr && glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;
	if (appFocused && !m_appWasFocused && m_ready && IsRefreshAllowed()) { Refresh(); }
	m_appWasFocused = appFocused;

	// Something else took the Inspector (an entity was picked in the Hierarchy): the clip
	// preview is no longer on screen, so stop it and let the clip go. Checked before Begin so
	// it still happens while this panel is collapsed.
	if (m_audioInInspector) {
		auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
		if (!inspector || !inspector->HasCustomTarget()) {
			if (m_audio) m_audio->UnloadPreview();
			m_audioInInspector = false;
		}
	}

	if (!ImGui::Begin(m_panelName.c_str(), nullptr, m_flags)) {
		ImGui::End();
		return;
	}

	if (!m_ready) {
		ImGui::TextDisabled("Assets folder not found.");
		ImGui::BeginDisabled(!IsRefreshAllowed());
		if (ImGui::Button("Retry")) {
			m_ready = m_assets.Init();
			if (m_ready) Refresh();
		}
		ImGui::EndDisabled();
		ImGui::End();
		return;
	}

	if (m_listingDirty)
		RebuildListing();

	DrawToolbar();
	ImGui::Separator();

	float const footerHeight = ImGui::GetFrameHeightWithSpacing();

	// Left: folder tree (drag its right edge to resize).
	ImGui::BeginChild("##FolderTree", ImVec2(m_treeWidth, -footerHeight),
		ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
	DrawFolderTree(m_root);
	m_syncTree = false;
	ImGui::EndChild();

	ImGui::SameLine();

	// Right: tile grid.
	ImGui::BeginChild("##AssetGrid", ImVec2(0.0f, -footerHeight));
	DrawGrid();
	ImGui::EndChild();
	// A Hierarchy entity dropped anywhere on the grid becomes a prefab in the folder being
	// viewed (not while searching: the grid then shows several folders). A folder tile's own
	// target is smaller, so ImGui prefers it when the drop lands on a folder.
	if (m_search[0] == '\0') AcceptEntityDrop(m_currentFolder);

	DrawFooter();

	// Shortcuts while any part of the browser is focused (and the search box is not being typed in).
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
		if (IsRefreshAllowed() && ImGui::IsKeyPressed(ImGuiKey_F5, false))
			Refresh();
		if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && !m_currentFolder.empty() && m_search[0] == '\0')
			NavigateTo(ParentOf(m_currentFolder));
		if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && !m_selected.empty() && m_renaming.empty())
			BeginRename(m_selected);
		// Shortcut, not IsKeyPressed: the Hierarchy registers Delete with RouteGlobal (delete the
		// selected entities), and only a focus-route Shortcut claims the key ahead of it -
		// otherwise one press would delete the scene file AND the selected entities.
		if (!m_selected.empty() && m_renaming.empty() && ImGui::Shortcut(ImGuiKey_Delete))
			BeginDelete(m_selected);
	}

	DrawDeleteConfirm();
	DrawCreateScriptModal();

	ImGui::End();

	// After drawing, like navigation below: renaming refreshes the index and the item list.
	ApplyPendingRename();
	ApplyPendingDelete();
	ApplyPendingPrefab();

	// Apply navigation after drawing, so the item list never changes mid-loop.
	if (m_navigateTo) {
		m_currentFolder = *m_navigateTo;
		m_navigateTo.reset();
		m_renaming.clear();
		m_search[0] = '\0';
		m_listingDirty = true;
		m_syncTree = true;
	}
}

// ------------------------------------------------------------------ data

void Crucible::AssetBrowser::RebuildFolderTree() {
	m_root = FolderNode{ "Assets", "", {} };
	BuildFolderNode(m_root);
	m_syncTree = true;
}

void Crucible::AssetBrowser::BuildFolderNode(FolderNode& node) {
	std::error_code ec;
	fs::path const dir = AbsolutePathOf(node.virtualPath);

	for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
		!ec && it != end; it.increment(ec))
	{
		if (!it->is_directory(ec)) continue;
		std::string name = it->path().filename().string();
		if (name.empty() || name[0] == '.') continue;   // hidden folders

		FolderNode child;
		child.virtualPath = node.virtualPath.empty() ? name : node.virtualPath + "/" + name;
		child.name = std::move(name);
		BuildFolderNode(child);
		node.children.push_back(std::move(child));
	}

	std::sort(node.children.begin(), node.children.end(), [](FolderNode const& a, FolderNode const& b) {
		return ToLowerCopy(a.name) < ToLowerCopy(b.name);
	});
}

void Crucible::AssetBrowser::RebuildListing() {
	m_listingDirty = false;
	m_items.clear();

	if (m_search[0] != '\0') {
		// Search mode: match file names across the whole index, ignoring folders.
		std::string const needle = ToLowerCopy(m_search);
		for (AssetEntry const& entry : m_assets.GetAll()) {
			if (entry.compiled) continue;   // in Resources, not Assets: the browser shows Assets only
			std::size_t const slash = entry.virtualPath.find_last_of('/');
			std::string name = (slash == std::string::npos) ? entry.virtualPath : entry.virtualPath.substr(slash + 1);
			if (ToLowerCopy(name).find(needle) == std::string::npos) continue;
			if (IsHiddenCompanionFile(name)) continue;

			Item item;
			item.name = std::move(name);
			item.virtualPath = entry.virtualPath;
			item.type = entry.type;
			item.sizeBytes = entry.sizeBytes;
			item.badge = MakeBadge(entry.extension, entry.type);
			m_items.push_back(std::move(item));
		}
	}
	else {
		// Folder mode: sub-folders from disk, files from the index.
		std::error_code ec;
		fs::path dir = AbsolutePathOf(m_currentFolder);
		if (!fs::is_directory(dir, ec)) {   // folder deleted on disk: fall back to the root
			m_currentFolder.clear();
			dir = AbsolutePathOf(m_currentFolder);
		}

		for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
			!ec && it != end; it.increment(ec))
		{
			std::string name = it->path().filename().string();
			if (name.empty() || name[0] == '.') continue;

			Item item;
			item.virtualPath = m_currentFolder.empty() ? name : m_currentFolder + "/" + name;

			if (it->is_directory(ec)) {
				item.isFolder = true;
			}
			else {
				if (AssetRegistry::IsMetaFile(it->path())) continue;   // .meta files are an implementation detail
				if (IsHiddenCompanionFile(name)) continue;             // edited through their asset (e.g. the Font Importer)

				std::string const ext = ToLowerCopy(it->path().extension().string());
				if (AssetEntry const* entry = m_assets.Find(item.virtualPath)) {
					item.type = entry->type;
					item.sizeBytes = entry->sizeBytes;
				}
				else {   // on disk but not indexed yet (added since the last Refresh)
					item.type = AssetManager::ClassifyExtension(ext);
					item.sizeBytes = it->file_size(ec);
				}
				item.badge = MakeBadge(ext, item.type);
			}

			item.name = std::move(name);
			m_items.push_back(std::move(item));
		}
	}

	// Folders first, then alphabetical (case-insensitive).
	std::sort(m_items.begin(), m_items.end(), [](Item const& a, Item const& b) {
		if (a.isFolder != b.isFolder) return a.isFolder;
		return ToLowerCopy(a.name) < ToLowerCopy(b.name);
	});
}

std::string Crucible::AssetBrowser::AbsolutePathOf(std::string const& virtualPath) const {
	fs::path p{ m_assets.GetAssetRoot() };
	if (!virtualPath.empty()) p /= fs::path{ virtualPath };
	return p.make_preferred().string();
}

void Crucible::AssetBrowser::NavigateTo(std::string const& folder) {
	m_navigateTo = folder;
}

void Crucible::AssetBrowser::CreateFolder() {
	if (!IsRefreshAllowed()) return; // Creating a folder also refreshes the index.
	std::string const base = m_currentFolder.empty() ? "New Folder" : m_currentFolder + "/New Folder";
	std::string candidate = base;
	std::error_code ec;
	for (int i = 1; fs::exists(AbsolutePathOf(candidate), ec) && i < 1000; ++i)
		candidate = base + " " + std::to_string(i);

	if (fs::create_directory(AbsolutePathOf(candidate), ec)) {
		Refresh();
		m_selected = candidate;
	}
	else {
		CRU_WARN("[AssetBrowser] Could not create folder '{}': {}", candidate, ec.message());
	}
}

// ------------------------------------------------------------------ create prefab

void Crucible::AssetBrowser::AcceptEntityDrop(std::string const& folder) {
	// No highlight at all in Play Mode, the same rule as Hierarchy > Create Prefab.
	if (!PrefabAuthoring::CanCreate() || !ImGui::BeginDragDropTarget()) return;
	if (ImGuiPayload const* payload = ImGui::AcceptDragDropPayload(EditorWidgets::kEntityPayload)) {
		Entity dragged{};
		std::memcpy(&dragged, payload->Data, sizeof(Entity));
		m_pendingPrefab = PendingPrefab{ dragged, folder };
	}
	ImGui::EndDragDropTarget();
}

void Crucible::AssetBrowser::ApplyPendingPrefab() {
	if (!m_pendingPrefab) return;
	PendingPrefab const pending = std::move(*m_pendingPrefab);
	m_pendingPrefab.reset();

	Core* core = EditorManager::GetInstance().GetCore();
	if (core == nullptr) return;
	World& world = core->GetWorld();

	// Refreshes this browser itself, so the new files are listed next frame.
	std::vector<std::string> const created = PrefabAuthoring::CreatePrefabs(
		world, PrefabAuthoring::DraggedEntities(world, pending.dragged), pending.folder);
	if (!created.empty()) m_selected = created.front();
}

// ------------------------------------------------------------------ drawing

void Crucible::AssetBrowser::DrawToolbar() {
	ImGuiStyle const& style = ImGui::GetStyle();
	float const startX = ImGui::GetCursorPosX();
	float const fullWidth = ImGui::GetContentRegionAvail().x;
	bool const searching = m_search[0] != '\0';

	bool const canRefresh = IsRefreshAllowed();
	ImGui::BeginDisabled(!canRefresh);
	if (ImGui::Button("Refresh")) Refresh();
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("%s", canRefresh ? "Rescan the Assets folder (F5)" : "Stop Play Mode to refresh assets");

	ImGui::SameLine();
	ImGui::BeginDisabled(m_currentFolder.empty() || searching);
	if (ImGui::ArrowButton("##Up", ImGuiDir_Up)) NavigateTo(ParentOf(m_currentFolder));
	ImGui::EndDisabled();

	// Breadcrumb: Assets > Textures > Characters (each part is clickable).
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
	if (searching) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("Search results (%d)", static_cast<int>(m_items.size()));
	}
	else {
		if (ImGui::Button("Assets")) NavigateTo("");
		std::size_t start = 0;
		int depth = 0;
		while (start < m_currentFolder.size()) {
			std::size_t const slash = m_currentFolder.find('/', start);
			std::size_t const end = (slash == std::string::npos) ? m_currentFolder.size() : slash;

			ImGui::SameLine(0.0f, 2.0f);
			ImGui::TextDisabled(">");
			ImGui::SameLine(0.0f, 2.0f);

			ImGui::PushID(depth++);
			if (ImGui::Button(m_currentFolder.substr(start, end - start).c_str()))
				NavigateTo(m_currentFolder.substr(0, end));
			ImGui::PopID();

			start = end + 1;
		}
	}
	ImGui::PopStyleColor();

	// Right-aligned: tile size slider and search box.
	constexpr float sliderWidth = 110.0f;
	constexpr float searchWidth = 180.0f;
	float const rightX = startX + fullWidth - (sliderWidth + style.ItemSpacing.x + searchWidth);
	ImGui::SameLine();
	if (rightX > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(rightX);

	ImGui::SetNextItemWidth(sliderWidth);
	ImGui::SliderFloat("##TileSize", &m_tileSize, 48.0f, 128.0f, "Size %.0f");

	ImGui::SameLine();
	ImGui::SetNextItemWidth(searchWidth);
	if (ImGui::InputTextWithHint("##Search", "Search assets...", m_search, sizeof(m_search)))
		m_listingDirty = true;
}

void Crucible::AssetBrowser::DrawFolderTree(FolderNode const& node) {
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
		| ImGuiTreeNodeFlags_SpanAvailWidth;
	if (node.children.empty())        flags |= ImGuiTreeNodeFlags_Leaf;
	if (node.virtualPath.empty())     flags |= ImGuiTreeNodeFlags_DefaultOpen;
	if (m_search[0] == '\0' && node.virtualPath == m_currentFolder) flags |= ImGuiTreeNodeFlags_Selected;

	// After navigating from the grid / breadcrumb, expand the tree down to the current folder.
	if (m_syncTree && node.virtualPath != m_currentFolder && IsAncestorOrSelf(node.virtualPath, m_currentFolder))
		ImGui::SetNextItemOpen(true);

	bool const open = ImGui::TreeNodeEx(node.virtualPath.empty() ? "##AssetsRoot" : node.virtualPath.c_str(),
		flags, "%s", node.name.c_str());

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
		NavigateTo(node.virtualPath);

	if (open) {
		for (FolderNode const& child : node.children)
			DrawFolderTree(child);
		ImGui::TreePop();
	}
}

void Crucible::AssetBrowser::DrawGrid() {
	// A rename whose tile is no longer listed (search changed, file vanished) is dropped.
	// Checked here, against this frame's fresh listing, not after drawing: Create Scene starts
	// a rename on a file the listing only picks up next frame.
	if (!m_renaming.empty() && std::none_of(m_items.begin(), m_items.end(),
		[this](Item const& item) { return item.virtualPath == m_renaming; }))
		m_renaming.clear();

	if (m_items.empty())
		ImGui::TextDisabled(m_search[0] != '\0' ? "No assets match the search." : "This folder is empty.");

	ImGuiStyle const& style = ImGui::GetStyle();
	float const cellWidth = m_tileSize + style.ItemSpacing.x;
	int const columns = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + style.ItemSpacing.x) / cellWidth));

	for (std::size_t i = 0; i < m_items.size(); ++i) {
		if (i % static_cast<std::size_t>(columns) != 0) ImGui::SameLine();
		DrawTile(m_items[i]);
	}

	// Click on empty space clears the selection.
	if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
		m_selected.clear();

	// Right-click on empty space.
	if (ImGui::BeginPopupContextWindow("##GridContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
		bool const searching = m_search[0] != '\0';
		if (ImGui::MenuItem("New Folder", nullptr, false, !searching && IsRefreshAllowed())) CreateFolder();
		if (ImGui::MenuItem("Create Animator Controller", nullptr, false, !searching && IsRefreshAllowed())) {
			if (auto animator = EditorManager::GetInstance().GetPanel<AnimatorPanel>(ImguiPanels::ANIMATOR)) animator->RequestNew();
		}
		if (IsInScenesFolder(m_currentFolder) && ImGui::MenuItem("Create Scene", nullptr, false, !searching && IsRefreshAllowed())) CreateScene();
		if (ImGui::MenuItem("Create State Graph", nullptr, false, !searching && IsRefreshAllowed())) CreateGraphAsset(true);
		ImGui::SetItemTooltip("An enemy's state machine (Idle, Patrol, Chase...). Pick it on a StateGraphAgent.");
		if (ImGui::MenuItem("Create Behavior Graph", nullptr, false, !searching && IsRefreshAllowed())) CreateGraphAsset(false);
		ImGui::SetItemTooltip("A behaviour tree. Double-click it to edit in the Behavior Graph window.");
		// Always listed so it can be found, but only enabled where the build will compile it.
		bool const inScripts = IsInScriptsFolder(m_currentFolder);
		if (ImGui::MenuItem("Create C# Script", nullptr, false, inScripts && !searching && IsRefreshAllowed())) BeginCreateScript();
		if (!inScripts) ImGui::SetItemTooltip("Scripts are only compiled from the Scripts folder.\nOpen Assets/Scripts (or a folder in it) first.");
		if (ImGui::MenuItem("Show in Explorer")) ShowInExplorer(AbsolutePathOf(m_currentFolder), false);
		ImGui::Separator();
		if (ImGui::MenuItem("Refresh", "F5", false, IsRefreshAllowed())) Refresh();
		ImGui::EndPopup();
	}
}

void Crucible::AssetBrowser::DrawTile(Item const& item) {
	ImGui::PushID(item.virtualPath.c_str());
	// A group, so the rename field drawn over the label below still leaves the row layout
	// exactly as one tile-sized item (SameLine places the next tile after the group).
	ImGui::BeginGroup();

	bool const renamingThis = !m_renaming.empty() && m_renaming == item.virtualPath;

	float const size = m_tileSize;
	float const labelHeight = ImGui::GetTextLineHeight();
	ImVec2 const tileSize(size, size + labelHeight * m_labelLines + 6.0f);   // room for a two-line name
	ImVec2 const p0 = ImGui::GetCursorScreenPos();
	ImVec2 const p1(p0.x + tileSize.x, p0.y + tileSize.y);

	// ---- interaction (all on the one invisible button covering the tile) ----
	// InvisibleButton returns true on release, and only when the press also started on this tile.
	if (renamingThis) ImGui::SetNextItemAllowOverlap();   // let the rename field on top take the mouse
	bool const released = ImGui::InvisibleButton("##Tile", tileSize);
	bool const hovered = ImGui::IsItemHovered();

	// Dropping a Hierarchy entity on a folder saves the prefab in that folder. Right after the
	// button, while it is still the last item.
	if (item.isFolder) AcceptEntityDrop(item.virtualPath);

	// Unity's rule: a click selects, a drag does not. The press only counts as a click if the
	// mouse never moved past ImGui's drag threshold - the same threshold BeginDragDropSource
	// below uses to start a drag - so dragging an asset onto the Inspector's picker leaves the
	// Inspector showing the entity instead of swapping it for the asset. The farthest distance
	// is kept until the next press, so it is still readable on the release frame.
	ImGuiIO const& io = ImGui::GetIO();
	bool const dragged = io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] >= io.MouseDragThreshold * io.MouseDragThreshold;
	if (released && !dragged) {
		m_selected = item.virtualPath;
		ShowInInspector(item);
	}

	// Right-click selects straight away, so the context menu is about this tile.
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
		m_selected = item.virtualPath;
		ShowInInspector(item);
	}

	if (hovered && !renamingThis && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		OpenItem(item);

	DrawItemContextMenu(item);

	if (!item.isFolder && !renamingThis && ImGui::BeginDragDropSource()) {
		ImGui::SetDragDropPayload(EditorWidgets::kAssetPayload, item.virtualPath.c_str(), item.virtualPath.size() + 1);
		ImGui::TextUnformatted(item.name.c_str());
		ImGui::TextDisabled("%s", ToString(item.type));
		ImGui::EndDragDropSource();
	}
	else if (ImGui::BeginItemTooltip()) {
		ImGui::TextUnformatted(item.name.c_str());
		ImGui::TextDisabled("%s", item.virtualPath.c_str());
		if (!item.isFolder)
			ImGui::TextDisabled("%s, %s", ToString(item.type), FormatBytes(item.sizeBytes).c_str());
		ImGui::EndTooltip();
	}

	// ---- visuals ----
	ImDrawList* dl = ImGui::GetWindowDrawList();
	bool const selected = (m_selected == item.virtualPath);
	if (selected || hovered)
		dl->AddRectFilled(p0, p1, ImGui::GetColorU32(selected ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered), 4.0f);

	float const pad = size * 0.14f;
	ImVec2 const i0(p0.x + pad, p0.y + pad);
	ImVec2 const i1(p0.x + size - pad, p0.y + size - pad);
	float const w = i1.x - i0.x;
	float const h = i1.y - i0.y;

	if (item.isFolder) {
		ImU32 const folderColor = IM_COL32(222, 178, 82, 255);
		dl->AddRectFilled(ImVec2(i0.x, i0.y + h * 0.10f), ImVec2(i0.x + w * 0.45f, i0.y + h * 0.30f), folderColor, 3.0f);  // tab
		dl->AddRectFilled(ImVec2(i0.x, i0.y + h * 0.22f), ImVec2(i1.x, i1.y - h * 0.05f), folderColor, 3.0f);              // body
	}
	else {
		// TODO: textures could show a real thumbnail here once the renderer can hand ImGui a texture
		// (ImGui_ImplVulkan_AddTexture) - draw it with dl->AddImage(i0, i1) instead of the badge.
		dl->AddRectFilled(i0, i1, TypeColor(item.type), 4.0f);
		// The whole extension, scaled down to fit the box if it is long ("CFONT", "WAV" stays full
		// size), and clipped to the box as a last resort at very small tile sizes.
		ImVec2 const fullSize = ImGui::CalcTextSize(item.badge.c_str());
		float const room = w - 6.0f;
		float const fit = (fullSize.x > room && fullSize.x > 0.0f) ? std::max(room / fullSize.x, 0.55f) : 1.0f;
		float const fontSize = ImGui::GetFontSize() * fit;
		ImVec2 const badgeSize(fullSize.x * fit, fullSize.y * fit);
		ImVec4 const clip(i0.x, i0.y, i1.x, i1.y);
		dl->AddText(ImGui::GetFont(), fontSize, ImVec2(i0.x + (w - badgeSize.x) * 0.5f, i0.y + (h - badgeSize.y) * 0.5f),
			IM_COL32(255, 255, 255, 235), item.badge.c_str(), nullptr, 0.0f, &clip);
	}

	if (renamingThis) {
		DrawRenameField(ImVec2(p0.x, p0.y + size), size);
	}
	else {
		// Files show their name without the extension - the badge on the tile already says "TTF",
		// "PNG"... - so the room goes to the part that tells files apart. Tooltip / footer: full name.
		std::string labelText = item.name;
		if (!item.isFolder) {
			std::size_t const dot = labelText.find_last_of('.');
			if (dot != std::string::npos && dot > 0) labelText.erase(dot);
		}
		std::vector<std::string> const lines = TileLabel(labelText, size - 4.0f);
		for (std::size_t line = 0; line < lines.size(); ++line) {
			float const lineWidth = ImGui::CalcTextSize(lines[line].c_str()).x;
			dl->AddText(ImVec2(p0.x + (size - lineWidth) * 0.5f, p0.y + size + 2.0f + labelHeight * static_cast<float>(line)),
				ImGui::GetColorU32(ImGuiCol_Text), lines[line].c_str());
		}
	}

	ImGui::EndGroup();
	ImGui::PopID();
}

void Crucible::AssetBrowser::DrawItemContextMenu(Item const& item) {
	if (!ImGui::BeginPopupContextItem("##ItemContext"))
		return;

	if ((item.isFolder || item.type == AssetType::Scene || item.type == AssetType::Script) && ImGui::MenuItem("Open"))
		OpenItem(item);

	if (!item.isFolder && item.type == AssetType::Font) {
		if (ImGui::MenuItem("Open in Font Importer"))
			OpenItem(item);
		ImGui::Separator();
	}

	// Greyed out rather than hidden when there is no audio system, so it is obvious the
	// feature exists and something is missing.
	// The right-click already selected it, so the preview clip is this one; these are the
	// Inspector's Play/Stop button, one menu away.
	if (item.type == AssetType::Audio) {
		if (ImGui::MenuItem("Play", nullptr, false, m_audio != nullptr) && m_audio->LoadPreview(item.virtualPath)) m_audio->PlayPreview();
		if (ImGui::MenuItem("Stop", nullptr, false, m_audio != nullptr)) m_audio->StopPreview();
		ImGui::Separator();
	}

	if (m_search[0] != '\0' && ImGui::MenuItem("Go to Folder"))
		NavigateTo(ParentOf(item.virtualPath));

	if (ImGui::MenuItem("Rename", "F2", false, IsRefreshAllowed()))
		BeginRename(item.virtualPath);

	if (ImGui::MenuItem("Delete", "Del", false, IsRefreshAllowed()))
		BeginDelete(item.virtualPath);

	if (ImGui::MenuItem("Show in Explorer"))
		ShowInExplorer(AbsolutePathOf(item.virtualPath), true);

	ImGui::Separator();

	if (ImGui::MenuItem("Copy Path"))
		ImGui::SetClipboardText(item.virtualPath.c_str());

	if (!item.isFolder) {
		AssetEntry const* entry = m_assets.Find(item.virtualPath);
		if (ImGui::MenuItem("Copy GUID", nullptr, false, entry != nullptr))
			ImGui::SetClipboardText(AssetRegistry::GuidToString(entry->guid).c_str());
	}

	ImGui::EndPopup();
}

void Crucible::AssetBrowser::ShowInInspector(Item const& item) {
	auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
	if (!inspector) return;

	if (!item.isFolder && item.type == AssetType::Audio && m_audio != nullptr) {
		// A clip that will not load still takes over the Inspector, and says so. Returning
		// early instead left the PREVIOUS clip's preview up - its name, "0.00 s" and a dead
		// Play button, because LoadPreview had already unloaded it - so the wrong file
		// looked broken.
		const bool loaded = m_audio->LoadPreview(item.virtualPath);   // on failure the log says why
		if (loaded && m_autoPlay) m_audio->PlayPreview();

		// Captures `this`: the Inspector only calls it while drawing, and every panel is
		// destroyed together in EditorManager::Shutdown, so it never outlives the browser.
		inspector->SetCustomTarget([this, name = item.name, loaded] { DrawAudioPreview(name, loaded); });
		m_audioInInspector = true;
	}
	else if (m_audioInInspector) {
		// Another asset was picked: the clip preview leaves the Inspector, as in Unity.
		inspector->ClearTarget();
		if (m_audio) m_audio->UnloadPreview();
		m_audioInInspector = false;
	}
}

// Unity's AudioClip preview: what it is, how long, and a Play/Stop button.
void Crucible::AssetBrowser::DrawAudioPreview(std::string const& name, bool loaded) {
	ImGui::TextUnformatted(name.c_str());

	if (!loaded) {
		ImGui::TextDisabled("Could not load this clip - the log says why.");
		return;
	}

	ImGui::TextDisabled("Audio clip, %.2f s", m_audio ? m_audio->GetPreviewLength() : 0.0f);
	ImGui::Separator();

	if (m_audio == nullptr) {
		ImGui::TextDisabled("No audio system - previews are off.");
		return;
	}

	// One button that is Play or Stop depending on what is happening now, so it flips back
	// to Play by itself when the clip ends.
	if (m_audio->IsPreviewPlaying()) {
		if (ImGui::Button("Stop", ImVec2(80.0f, 0.0f))) m_audio->StopPreview();
	}
	else {
		if (ImGui::Button("Play", ImVec2(80.0f, 0.0f))) m_audio->PlayPreview();
	}

	ImGui::SameLine();
	ImGui::Checkbox("Auto Play", &m_autoPlay);
}

void Crucible::AssetBrowser::DrawFooter() {
	ImGui::AlignTextToFramePadding();
	if (m_selected.empty()) {
		ImGui::TextDisabled("%d items", static_cast<int>(m_items.size()));
		return;
	}

	ImGui::TextUnformatted(m_selected.c_str());
	if (AssetEntry const* entry = m_assets.Find(m_selected)) {
		ImGui::SameLine();
		ImGui::TextDisabled("  %s, %s", ToString(entry->type), FormatBytes(entry->sizeBytes).c_str());
	}
}

// ------------------------------------------------------------------ open (double-click)

void Crucible::AssetBrowser::OpenItem(Item const& item) {
	if (item.isFolder) {
		NavigateTo(item.virtualPath);
		return;
	}

	switch (item.type) {
	case AssetType::AnimatorController:
		if (auto animator = EditorManager::GetInstance().GetPanel<AnimatorPanel>(ImguiPanels::ANIMATOR)) {
			if (const auto* entry = m_assets.Find(item.virtualPath)) animator->RequestOpen(entry->guid.m_Value);
		}
		return;
	case AssetType::BehaviorGraph:
		if (auto behavior = EditorManager::GetInstance().GetPanel<BehaviorGraphPanel>(ImguiPanels::BEHAVIORGRAPH)) {
			if (const auto* entry = m_assets.Find(item.virtualPath)) behavior->RequestOpen(entry->guid.m_Value);
		}
		return;
	case AssetType::StateGraph:
		if (auto states = EditorManager::GetInstance().GetPanel<StateGraphPanel>(ImguiPanels::STATEGRAPH)) {
			if (const auto* entry = m_assets.Find(item.virtualPath)) states->RequestOpen(entry->guid.m_Value);
		}
		return;
	case AssetType::Scene: {
		fs::path const file = AbsolutePathOf(item.virtualPath);
		if (m_scenes.HasPath() && SamePath(m_scenes.CurrentPath(), file)) {
			CRU_USER_INFO("'{}' is already the open scene", item.name);
			return;
		}
		// Through the Menu, so it is exactly File > Open: refused during play mode, and the
		// "unsaved changes?" prompt first when the current scene is dirty.
		if (auto menu = EditorManager::GetInstance().GetPanel<Menu>(ImguiPanels::MENU))
			menu->RequestOpenScene(file);
		else
			m_scenes.RequestLoad(file);
		return;
	}
	case AssetType::Script:
		OpenInVSCode(AbsolutePathOf(item.virtualPath));
		return;
	case AssetType::Font:
		// .ttf/.otf: its import settings; .cfont: the font it was compiled from.
		if (auto fonts = EditorManager::GetInstance().GetPanel<FontImporter>(ImguiPanels::FONTIMPORTER))
			fonts->Open(item.virtualPath);
		return;
	case AssetType::Texture:
		// An image: its import settings, compile, and the BC format comparison.
		if (auto textures = EditorManager::GetInstance().GetPanel<TextureImporter>(ImguiPanels::TEXTUREIMPORTER))
			textures->Open(item.virtualPath);
		return;
	default:
		CRU_INFO("[AssetBrowser] Open {} ({}): no editor for this type yet", item.virtualPath, ToString(item.type));
		return;
	}
}

// ------------------------------------------------------------------ create scene

bool Crucible::AssetBrowser::IsInScenesFolder(std::string const& folder) const {
	std::string const f = ToLowerCopy(folder);
	return f == "scenes" || f.rfind("scenes/", 0) == 0;
}

void Crucible::AssetBrowser::CreateScene() {
	if (!IsRefreshAllowed() || !IsInScenesFolder(m_currentFolder)) return;

	std::string const base = m_currentFolder + "/New Scene";
	std::string stem = base;
	std::error_code ec;
	for (int i = 1; fs::exists(AbsolutePathOf(stem + ".scene"), ec) && i < 1000; ++i)
		stem = base + " " + std::to_string(i);
	std::string const virtualPath = stem + ".scene";

	// Through the SceneManager, so a scene made here starts with the same default objects
	// as File > New Scene. It only writes the file; the open scene is left alone.
	if (!m_scenes.CreateSceneFile(fs::path(AbsolutePathOf(virtualPath)))) {
		CRU_USER_WARN("Could not create scene '{}'", virtualPath);
		return;
	}

	Refresh();
	CRU_USER_INFO("Created scene {}", virtualPath);
	BeginRename(virtualPath);   // Unity: type its name straight away
}

// ------------------------------------------------------------------ create state / behavior graph

void Crucible::AssetBrowser::CreateGraphAsset(bool stateGraph) {
	if (!IsRefreshAllowed()) return;

	// "New State Graph", "New State Graph 1", ... - the first name not taken in this folder.
	std::string const extension = stateGraph ? ".stategraph" : ".behaviorgraph";
	std::string const name      = stateGraph ? "New State Graph" : "New Behavior Graph";
	std::string const base      = m_currentFolder.empty() ? name : m_currentFolder + "/" + name;
	std::string stem = base;
	std::error_code ec;
	for (int i = 1; fs::exists(AbsolutePathOf(stem + extension), ec) && i < 1000; ++i)
		stem = base + " " + std::to_string(i);
	std::string const virtualPath = stem + extension;

	// Through ResourceManager::SaveTextAsset: the same safe write the graph editor saves with.
	ResourceManager* resources = EditorManager::GetInstance().GetResources();
	std::string error;
	std::string const contents = stateGraph ? GraphAssets::NewStateGraphJson() : GraphAssets::NewBehaviorGraphJson();
	if (!resources || !resources->SaveTextAsset(virtualPath, contents, true, &error)) {
		CRU_USER_WARN("Could not create '{}': {}", virtualPath, error);
		return;
	}

	Refresh();
	CRU_USER_INFO("Created {}", virtualPath);
	BeginRename(virtualPath);   // Unity: type its name straight away
}

// ------------------------------------------------------------------ create C# script

bool Crucible::AssetBrowser::IsInScriptsFolder(std::string const& folder) const {
	std::string const f = ToLowerCopy(folder);
	return f == "scripts" || f.rfind("scripts/", 0) == 0;
}

void Crucible::AssetBrowser::BeginCreateScript() {
	if (!IsRefreshAllowed() || !IsInScriptsFolder(m_currentFolder)) return;
	// Asked for in a modal rather than renamed afterwards (as Create Scene does): the class
	// name is written inside the file, so it has to be known before the file is created.
	m_createScriptFolder = m_currentFolder;
	m_createScriptName[0] = '\0';
	m_createScriptError.clear();
	m_openCreateScript = true;
}

void Crucible::AssetBrowser::DrawCreateScriptModal() {
	// Opened here, at the panel's ID level, like the delete confirm, so OpenPopup and
	// BeginPopupModal see the same ID.
	char const* const kTitle = "Create C# Script";
	bool const justOpened = m_openCreateScript;
	if (m_openCreateScript) {
		ImGui::OpenPopup(kTitle);
		m_openCreateScript = false;
	}

	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	ImGui::Text("New script in Assets/%s", m_createScriptFolder.c_str());
	ImGui::Spacing();
	if (justOpened) ImGui::SetKeyboardFocusHere();   // type the name straight away
	bool const enter = ImGui::InputTextWithHint("Class name", "e.g. PlayerController", m_createScriptName,
		sizeof(m_createScriptName), ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::InputText("Namespace", m_createScriptNamespace, sizeof(m_createScriptNamespace));

	if (!m_createScriptError.empty()) {
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", m_createScriptError.c_str());
	}

	ImGui::Spacing();
	bool close = false;
	bool const create = ImGui::Button("Create", ImVec2(120.0f, 0.0f)) || enter;
	if (create) {
		if (!IsRefreshAllowed()) {
			m_createScriptError = "Cannot create scripts in play mode - press Stop first.";
		}
		else {
			fs::path const assetRoot{ m_assets.GetAssetRoot() };
			fs::path output;
			// Same template, name checks and never-overwrite rule as Inspector > Add Component.
			if (ScriptAuthoring::CreateIn(assetRoot.parent_path(), fs::path(AbsolutePathOf(m_createScriptFolder)),
					m_createScriptName, m_createScriptNamespace, output, m_createScriptError)) {
				std::error_code ec;
				std::string const virtualPath = fs::relative(output, assetRoot, ec).generic_string();
				Refresh();
				m_selected = virtualPath;
				CRU_USER_INFO("Created script {}. It compiles by itself; attach it through Add Component once the Console says scripts reloaded.", virtualPath);
				close = true;
			}
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		close = true;

	if (close) {
		m_createScriptError.clear();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

// ------------------------------------------------------------------ delete

void Crucible::AssetBrowser::BeginDelete(std::string const& virtualPath) {
	if (!IsRefreshAllowed()) {
		CRU_USER_WARN("Cannot delete while in play mode - press Stop first");
		return;
	}
	if (virtualPath.empty()) return;   // the Assets folder itself

	std::error_code ec;
	fs::path const path = AbsolutePathOf(virtualPath);
	if (!fs::exists(path, ec)) {
		CRU_USER_WARN("Cannot delete '{}': it no longer exists", virtualPath);
		return;
	}

	// Gathered once, when the modal opens: what will go, and what still uses it.
	m_deleteInfo = DeleteInfo{};
	m_deleteInfo.isFolder = fs::is_directory(path, ec);
	m_deleteInfo.usedBy = FindReferences(virtualPath, m_deleteInfo.isFolder, m_deleteInfo.fileCount);

	m_deleteTarget = virtualPath;
	m_selected = virtualPath;
	m_openDeleteConfirm = true;
}

std::vector<std::string> Crucible::AssetBrowser::FindReferences(std::string const& virtualPath, bool isFolder, std::size_t& fileCount) const {
	fs::path const root{ m_assets.GetAssetRoot() };
	fs::path const target = AbsolutePathOf(virtualPath);
	fileCount = 0;

	// What the files being deleted are known by: the guid as a number (how scenes and prefabs
	// store it) and as hex (how .meta and the Copy GUID menu write it), and the quoted virtual
	// path (how materials name their textures).
	std::vector<std::string> needles;
	auto addFile = [&](std::string const& vp) {
		++fileCount;
		needles.push_back("\"" + vp + "\"");
		AssetEntry const* entry = m_assets.Find(vp);
		if (entry && entry->guid.m_Value != 0) {
			needles.push_back(std::to_string(entry->guid.m_Value));
			needles.push_back(AssetRegistry::GuidToString(entry->guid));
		}
	};

	std::error_code walk, ec;
	if (!isFolder) {
		addFile(virtualPath);
	}
	else {
		for (fs::recursive_directory_iterator it(target, walk), end; !walk && it != end; it.increment(walk)) {
			if (!it->is_regular_file(ec) || AssetRegistry::IsMetaFile(it->path())) continue;
			addFile(virtualPath + "/" + fs::relative(it->path(), target, ec).generic_string());
		}
	}
	if (needles.empty()) return {};

	// Every file that can point at another asset, except the ones being deleted. A handful of
	// small text files, so reading them whole each time the modal opens is fine.
	std::vector<std::string> usedBy;
	walk.clear();
	for (fs::recursive_directory_iterator it(root, walk), end; !walk && it != end; it.increment(walk)) {
		fs::path const& p = it->path();
		if (!it->is_regular_file(ec) || !CanReferenceAssets(p) || IsInside(p, target)) continue;
		if (it->file_size(ec) > (16u << 20)) continue;   // nothing that refers to assets is this big

		std::ifstream in(p, std::ios::binary);
		std::string const text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
		bool const uses = std::any_of(needles.begin(), needles.end(),
			[&](std::string const& needle) { return text.find(needle) != std::string::npos; });
		if (uses) usedBy.push_back(fs::relative(p, root, ec).generic_string());
	}
	std::sort(usedBy.begin(), usedBy.end());
	return usedBy;
}

void Crucible::AssetBrowser::DrawDeleteConfirm() {
	// Opened here, at the panel's ID level, rather than from the tile's context menu, so
	// OpenPopup and BeginPopupModal see the same ID.
	char const* const kTitle = "Delete Asset?";
	if (m_openDeleteConfirm) {
		ImGui::OpenPopup(kTitle);
		m_openDeleteConfirm = false;
	}

	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	ImVec4 const warn(1.0f, 0.75f, 0.3f, 1.0f);
	DeleteInfo const& info = m_deleteInfo;

	if (info.isFolder) {
		ImGui::Text("Delete the folder '%s'?", m_deleteTarget.c_str());
		if (info.fileCount > 0)
			ImGui::TextDisabled("Everything in it goes too: %zu file%s.", info.fileCount, info.fileCount == 1 ? "" : "s");
	}
	else {
		ImGui::Text("Delete '%s'?", m_deleteTarget.c_str());
	}

	// The open scene is this file, or somewhere inside this folder.
	bool const isOpenScene = m_scenes.HasPath() && IsInside(m_scenes.CurrentPath(), AbsolutePathOf(m_deleteTarget));
	if (isOpenScene) {
		ImGui::Spacing();
		ImGui::TextColored(warn, "%s", info.isFolder ? "The scene open in the editor is in this folder." : "This scene is open in the editor.");
		ImGui::TextDisabled("It stays open, but unsaved: saving will ask for a new name.");
	}

	if (!info.usedBy.empty()) {
		ImGui::Spacing();
		ImGui::TextColored(warn, "Still used by %zu file%s:", info.usedBy.size(), info.usedBy.size() == 1 ? "" : "s");
		constexpr std::size_t kShown = 8;
		for (std::size_t i = 0; i < info.usedBy.size() && i < kShown; ++i)
			ImGui::BulletText("%s", info.usedBy[i].c_str());
		if (info.usedBy.size() > kShown)
			ImGui::TextDisabled("...and %zu more", info.usedBy.size() - kShown);
		ImGui::TextDisabled("Their references will point at nothing once it is gone.");
	}

	ImGui::Spacing();
#ifdef _WIN32
	ImGui::TextDisabled("%s", info.isFolder ? "The folder goes to the Recycle Bin." : "The file and its .meta go to the Recycle Bin.");
#else
	ImGui::TextDisabled("This cannot be undone.");
#endif

	ImGui::Spacing();
	bool close = false;
	if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f))) {
		m_pendingDelete = m_deleteTarget;
		close = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		close = true;

	if (close) {
		m_deleteTarget.clear();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void Crucible::AssetBrowser::ApplyPendingDelete() {
	if (!m_pendingDelete) return;
	std::string const target = std::move(*m_pendingDelete);
	m_pendingDelete.reset();

	// Play may have started while the modal was up.
	if (!IsRefreshAllowed()) {
		CRU_USER_WARN("Cannot delete while in play mode - press Stop first");
		return;
	}

	// Checked before deleting: afterwards the file is gone and the paths cannot be compared
	// by what is on disk.
	bool const wasOpen = m_scenes.HasPath() && IsInside(m_scenes.CurrentPath(), AbsolutePathOf(target));
	bool const wasFolder = m_deleteInfo.isFolder;

	if (!DeleteOnDisk(target)) return;

	// The open scene lost its file: keep it open, but make the next save ask for a name.
	if (wasOpen) m_scenes.DetachCurrentFile();

	// Deleted the folder being browsed, or one of its parents: show what is left.
	if (m_currentFolder == target || m_currentFolder.rfind(target + "/", 0) == 0) {
		m_currentFolder = ParentOf(target);
		m_syncTree = true;
	}
	if (m_selected == target || m_selected.rfind(target + "/", 0) == 0) m_selected.clear();

	Refresh();   // drops it from the index and the grid
	CRU_USER_INFO("Deleted {}{}", wasFolder ? "folder " : (IsSceneFile(target) ? "scene " : ""), target);
}

bool Crucible::AssetBrowser::DeleteOnDisk(std::string const& virtualPath) {
	std::error_code ec;
	fs::path const path = AbsolutePathOf(virtualPath);
	bool const isFolder = fs::is_directory(path, ec);
	if (!isFolder && !fs::is_regular_file(path, ec)) {
		CRU_USER_WARN("Cannot delete '{}': it no longer exists", virtualPath);
		return false;
	}

	std::string why;
	if (!RemoveFile(path, why)) {
		CRU_USER_WARN("Cannot delete '{}': {}", virtualPath, why);
		return false;
	}

	// The .meta goes too, or the asset's guid would be left behind with nothing to point at.
	// (A folder's files take their .meta files with them.)
	fs::path const meta = AssetRegistry::MetaPathFor(path);
	if (fs::exists(meta, ec) && !RemoveFile(meta, why))
		CRU_USER_WARN("Deleted '{}', but its .meta could not be removed: {}", virtualPath, why);

	// Companion files edited through this asset (a font's .fontsettings) are hidden from the
	// browser, so nobody could delete them later: they go with it, .meta and all.
	if (!isFolder) {
		for (std::string_view const suffix : kCompanionSuffixes) {
			fs::path companion = path;
			companion += std::string(suffix);
			if (!fs::is_regular_file(companion, ec)) continue;
			if (!RemoveFile(companion, why)) {
				CRU_USER_WARN("Deleted '{}', but '{}' could not be removed: {}", virtualPath, companion.filename().string(), why);
				continue;
			}
			fs::path const companionMeta = AssetRegistry::MetaPathFor(companion);
			if (fs::exists(companionMeta, ec)) RemoveFile(companionMeta, why);
		}
	}
	return true;
}

// ------------------------------------------------------------------ rename

void Crucible::AssetBrowser::BeginRename(std::string const& virtualPath) {
	if (!IsRefreshAllowed()) {
		CRU_USER_WARN("Cannot rename while in play mode - press Stop first");
		return;
	}

	// Files edit the name without the extension (renaming player.png edits "player"), so the
	// asset's type can't change by accident. Folders edit the whole name.
	std::error_code ec;
	fs::path const p(virtualPath);
	bool const isFolder = fs::is_directory(AbsolutePathOf(virtualPath), ec);
	std::string const editable = isFolder ? p.filename().string() : p.stem().string();

	std::snprintf(m_renameBuffer, sizeof(m_renameBuffer), "%s", editable.c_str());
	m_renaming = virtualPath;
	m_selected = virtualPath;
	m_renameFocus = true;
	m_renameIdleFrames = 0;
}

void Crucible::AssetBrowser::DrawRenameField(ImVec2 labelPos, float width) {
	ImGui::SetCursorScreenPos(labelPos);
	ImGui::SetNextItemWidth(width);
	if (m_renameFocus) {
		ImGui::SetKeyboardFocusHere();
		m_renameFocus = false;
	}

	bool const enter = ImGui::InputText("##Rename", m_renameBuffer, sizeof(m_renameBuffer),
		ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

	if (enter) {
		m_pendingRename.emplace(m_renaming, m_renameBuffer);
		m_renaming.clear();
	}
	else if (ImGui::IsItemDeactivated()) {
		// Esc cancels; clicking anywhere else keeps the new name (Unity does the same).
		if (!ImGui::IsKeyPressed(ImGuiKey_Escape, false))
			m_pendingRename.emplace(m_renaming, m_renameBuffer);
		m_renaming.clear();
	}
	else if (!ImGui::IsItemActive() && ++m_renameIdleFrames > 2) {
		m_renaming.clear();   // the field never got focus: don't leave it stranded open
	}
}

void Crucible::AssetBrowser::ApplyPendingRename() {
	if (!m_pendingRename) return;
	auto const [from, typed] = std::move(*m_pendingRename);
	m_pendingRename.reset();

	std::string to;
	if (!RenameOnDisk(from, Trim(typed), to)) return;

	Refresh();
	m_selected = to;

	// Renamed the folder being browsed, or one of its parents: follow it.
	if (m_currentFolder == from)
		m_currentFolder = to;
	else if (m_currentFolder.rfind(from + "/", 0) == 0)
		m_currentFolder = to + m_currentFolder.substr(from.size());
	m_listingDirty = true;
	m_syncTree = true;

	CRU_USER_INFO("Renamed {} -> {}", from, to);
}

bool Crucible::AssetBrowser::RenameOnDisk(std::string const& from, std::string const& newName, std::string& outVirtualPath) {
	std::error_code ec;
	fs::path const src = AbsolutePathOf(from);
	bool const isFolder = fs::is_directory(src, ec);

	std::string const extension = isFolder ? std::string{} : fs::path(from).extension().string();
	std::string const finalName = newName + extension;
	std::string const parent = ParentOf(from);
	std::string const to = parent.empty() ? finalName : parent + "/" + finalName;

	if (to == from) return false;   // unchanged: nothing to do, no message
	// An empty name would still be a valid-looking file name (".png"), so check it on its own.
	if (char const* why = newName.empty() ? "the name is empty" : InvalidNameReason(finalName)) {
		CRU_USER_WARN("Cannot rename '{}' to '{}': {}", from, finalName, why);
		return false;
	}

	fs::path const dst = AbsolutePathOf(to);
	// fs::exists is case-insensitive on Windows, so "a.png" -> "A.png" finds itself: allowed.
	if (fs::exists(dst, ec) && !SamePath(src, dst)) {
		CRU_USER_WARN("Cannot rename '{}': '{}' already exists", from, finalName);
		return false;
	}

	fs::rename(src, dst, ec);
	if (ec) {
		CRU_USER_WARN("Cannot rename '{}': {}", from, ec.message());
		return false;
	}

	// A file's .meta moves with it, so the asset keeps its guid directly - nothing depends on
	// the content-hash rename detection. (A folder's .meta files are inside it: they moved.)
	if (!isFolder) {
		fs::path const srcMeta = AssetRegistry::MetaPathFor(src);
		if (fs::exists(srcMeta, ec)) {
			fs::rename(srcMeta, AssetRegistry::MetaPathFor(dst), ec);
			if (ec) {
				std::error_code undo;
				fs::rename(dst, src, undo);   // put the asset back rather than split it from its guid
				CRU_USER_WARN("Cannot rename '{}': its .meta could not be moved ({})", from, ec.message());
				return false;
			}
		}
	}

	// Keep each moved .meta's "path=" hint truthful. Only a hint, but teammates read it in git.
	auto const rewriteHint = [](fs::path const& metaPath, std::string const& virtualPath) {
		AssetMeta meta;
		if (AssetRegistry::ReadMeta(metaPath, meta))
			AssetRegistry::WriteMeta(metaPath, meta, virtualPath);
	};
	if (isFolder) {
		for (fs::recursive_directory_iterator it(dst, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
			if (ec || !AssetRegistry::IsMetaFile(it->path())) continue;
			fs::path const asset = fs::path(it->path()).replace_extension();   // drop ".meta"
			rewriteHint(it->path(), to + "/" + asset.lexically_relative(dst).generic_string());
		}
	}
	else {
		rewriteHint(AssetRegistry::MetaPathFor(dst), to);
	}

	// A scene's display name lives inside the file. Keep it equal to the file name, the way
	// Unity names scenes - otherwise the renamed file would still open as its old name.
	if (!isFolder && ToLowerCopy(extension) == ".scene") {
		JsonWrapper doc;
		if (doc.ReadFromFile(dst)) {
			doc.Set("name", fs::path(finalName).stem().string());
			doc.SaveToFile(dst);
		}
	}

	// The open scene's file - or the folder holding it - moved: keep saving to the right place.
	if (m_scenes.HasPath() && IsInside(m_scenes.CurrentPath(), src)) {
		// Keep the components below the renamed part exactly as they were (their original
		// case): skip as many components as 'src' has, then append the rest to 'dst'. For a
		// file rename nothing is left over and the result is 'dst' itself.
		fs::path const current = fs::path(m_scenes.CurrentPath()).lexically_normal().make_preferred();
		fs::path const oldBase = fs::path(src).lexically_normal().make_preferred();
		auto rest = current.begin();
		for (auto part = oldBase.begin(); part != oldBase.end() && rest != current.end(); ++part) ++rest;
		fs::path moved = dst;
		for (; rest != current.end(); ++rest) moved /= *rest;
		m_scenes.RelocateCurrentFile(moved);
	}

	outVirtualPath = to;
	return true;
}
