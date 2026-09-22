/*!
* @file			ImGuiPanel_AssetBrowser.cpp
* @brief		The editor's Asset Browser (Unity's "Project" window).
*/

#include "ImGui/Panels/ImGuiPanel_AssetBrowser.h"
#include "ImGui/EditorWidgets.h"
#include "AssetPipe/AssetRegistry.h"
#include "Diagnostics/Logger.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
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

	// Short label drawn on a file tile: the extension ("PNG"), or the type name if there is none.
	std::string MakeBadge(std::string const& extension, Crucible::AssetType type)
	{
		std::string badge;
		for (char c : extension) {
			if (c == '.') continue;
			badge.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
			if (badge.size() == 4) break;
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

	// Shortens 'text' with "..." so it fits in maxWidth pixels.
	std::string Ellipsize(std::string const& text, float maxWidth)
	{
		if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;
		std::string s = text;
		while (!s.empty()) {
			s.pop_back();
			std::string const candidate = s + "...";
			if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth) return candidate;
		}
		return "...";
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
}

// ------------------------------------------------------------------ panel lifetime

void Crucible::AssetBrowser::Init() {
	m_panelName = "Asset Browser";

	// Bottom strip under the Scene view and Hierarchy (Unity default), 30% of the height.
	m_docked = true;
	m_dockArea = DockArea::Bottom;
	m_dockRatio = 0.30f;
	m_makeFocus = true;   // be the visible tab in the bottom area (Console is behind it)

	// NOTE: the browser keeps its own AssetManager for now, because Core only creates a
	// temporary one in Core::Init(). Once Core owns an AssetManager (e.g. Core::GetAssets()),
	// use that instead so the editor and the engine share one index.
	m_ready = m_assets.Init();
	if (m_ready)
		RebuildFolderTree();
	else
		CRU_WARN("[AssetBrowser] AssetManager::Init() failed: Assets folder not found");
}

void Crucible::AssetBrowser::Refresh() {
	if (!m_ready) return;
	m_assets.Refresh();
	RebuildFolderTree();
	m_listingDirty = true;
}

void Crucible::AssetBrowser::Update() {
	if (!ImGui::Begin(m_panelName.c_str(), nullptr, m_flags)) {
		ImGui::End();
		return;
	}

	if (!m_ready) {
		ImGui::TextDisabled("Assets folder not found.");
		if (ImGui::Button("Retry")) {
			m_ready = m_assets.Init();
			if (m_ready) Refresh();
		}
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

	DrawFooter();

	// Shortcuts while any part of the browser is focused (and the search box is not being typed in).
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
		if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
			Refresh();
		if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && !m_currentFolder.empty() && m_search[0] == '\0')
			NavigateTo(ParentOf(m_currentFolder));
	}

	ImGui::End();

	// Apply navigation after drawing, so the item list never changes mid-loop.
	if (m_navigateTo) {
		m_currentFolder = *m_navigateTo;
		m_navigateTo.reset();
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
			std::size_t const slash = entry.virtualPath.find_last_of('/');
			std::string name = (slash == std::string::npos) ? entry.virtualPath : entry.virtualPath.substr(slash + 1);
			if (ToLowerCopy(name).find(needle) == std::string::npos) continue;

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

// ------------------------------------------------------------------ drawing

void Crucible::AssetBrowser::DrawToolbar() {
	ImGuiStyle const& style = ImGui::GetStyle();
	float const startX = ImGui::GetCursorPosX();
	float const fullWidth = ImGui::GetContentRegionAvail().x;
	bool const searching = m_search[0] != '\0';

	if (ImGui::Button("Refresh")) Refresh();
	ImGui::SetItemTooltip("Rescan the Assets folder (F5)");

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
		if (ImGui::MenuItem("New Folder", nullptr, false, !searching)) CreateFolder();
		if (ImGui::MenuItem("Show in Explorer")) ShowInExplorer(AbsolutePathOf(m_currentFolder), false);
		ImGui::Separator();
		if (ImGui::MenuItem("Refresh", "F5")) Refresh();
		ImGui::EndPopup();
	}
}

void Crucible::AssetBrowser::DrawTile(Item const& item) {
	ImGui::PushID(item.virtualPath.c_str());

	float const size = m_tileSize;
	float const labelHeight = ImGui::GetTextLineHeight();
	ImVec2 const tileSize(size, size + labelHeight + 6.0f);
	ImVec2 const p0 = ImGui::GetCursorScreenPos();
	ImVec2 const p1(p0.x + tileSize.x, p0.y + tileSize.y);

	// ---- interaction (all on the one invisible button covering the tile) ----
	ImGui::InvisibleButton("##Tile", tileSize);
	bool const hovered = ImGui::IsItemHovered();

	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
		m_selected = item.virtualPath;

	if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		if (item.isFolder)
			NavigateTo(item.virtualPath);
		else   // TODO: open the right editor per type (scene -> load scene, material -> material editor, ...)
			CRU_INFO("[AssetBrowser] Open {} ({}): no editor for this type yet", item.virtualPath, ToString(item.type));
	}

	DrawItemContextMenu(item);

	if (!item.isFolder && ImGui::BeginDragDropSource()) {
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
		ImVec2 const badgeSize = ImGui::CalcTextSize(item.badge.c_str());
		dl->AddText(ImVec2(i0.x + (w - badgeSize.x) * 0.5f, i0.y + (h - badgeSize.y) * 0.5f),
			IM_COL32(255, 255, 255, 235), item.badge.c_str());
	}

	std::string const label = Ellipsize(item.name, size - 4.0f);
	float const labelWidth = ImGui::CalcTextSize(label.c_str()).x;
	dl->AddText(ImVec2(p0.x + (size - labelWidth) * 0.5f, p0.y + size + 2.0f), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());

	ImGui::PopID();
}

void Crucible::AssetBrowser::DrawItemContextMenu(Item const& item) {
	if (!ImGui::BeginPopupContextItem("##ItemContext"))
		return;

	if (item.isFolder && ImGui::MenuItem("Open"))
		NavigateTo(item.virtualPath);

	if (m_search[0] != '\0' && ImGui::MenuItem("Go to Folder"))
		NavigateTo(ParentOf(item.virtualPath));

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
