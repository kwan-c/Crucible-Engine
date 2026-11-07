/*!
* @file			ImGuiPanel_AssetBrowser_Import.cpp
*
* @brief		The Asset Browser's file handling, apart from drawing tiles:
*				  - files dropped from File Explorer: routed to the folder of their type, with
*				    popups for files that already exist and files that are not supported;
*				  - moving an asset by dragging it inside the browser, within its type's folder;
*				  - compiling a .glb as soon as it appears in Assets, so it can be used right away.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/Panels/ImGuiPanel_AssetBrowser.h"
#include "ImGui/EditorWidgets.h"
#include "AssetPipe/AssetRegistry.h"
#include "Diagnostics/Logger.h"
#include "Scene/SceneManager.h"
#include "ModelBuild.h"   // Tools/Common

#include <GLFW/glfw3.h>
#include <imgui_internal.h>   // FindWindowByName, ImGuiWindow, ImGuiDockNode: where the panel is when its tab is hidden

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {

	Crucible::AssetBrowser* s_dropTarget = nullptr;   // GLFW callbacks take no user data of their own

	std::string Lower(std::string text) {
		std::transform(text.begin(), text.end(), text.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}

	std::string ParentOf(std::string const& virtualPath) {
		std::size_t const slash = virtualPath.find_last_of('/');
		return (slash == std::string::npos) ? std::string{} : virtualPath.substr(0, slash);
	}

	std::string FileNameOf(std::string const& virtualPath) {
		std::size_t const slash = virtualPath.find_last_of('/');
		return (slash == std::string::npos) ? virtualPath : virtualPath.substr(slash + 1);
	}

	std::string Join(std::string const& folder, std::string const& name) {
		return folder.empty() ? name : folder + "/" + name;
	}

	// True if `folder` is `root` or anywhere below it. Folder names ignore case, as Windows does.
	bool IsInsideRoot(std::string const& folder, std::string_view root) {
		std::string const f = Lower(folder), r = Lower(std::string(root));
		return f == r || (f.size() > r.size() && f.compare(0, r.size(), r) == 0 && f[r.size()] == '/');
	}

	// The top-level Assets folders a file extension belongs in; the first is where a new file
	// goes. Empty: the type has no folder of its own.
	std::vector<std::string_view> RootsFor(std::string const& extension) {
		using Crucible::AssetType;
		if (extension == ".slang") return { "Shaders" };   // not an indexed type, but a real source file
		switch (Crucible::AssetManager::ClassifyExtension(extension)) {
		case AssetType::Texture:            return { "Textures" };
		case AssetType::Model:              return { "Models" };
		case AssetType::Shader:             return { "Shaders" };
		case AssetType::Audio:              return { "Audio" };
		case AssetType::Scene:              return { "Scenes" };
		case AssetType::Prefab:             return { "Prefabs" };
		case AssetType::Font:               return { "Fonts" };
		case AssetType::Material:           return { "Materials" };
		case AssetType::Script:             return { "Scripts" };
		case AssetType::Text:               return { "Texts", "Config" };
		case AssetType::AnimatorController: return { "Animation" };
		case AssetType::StateGraph:         return { "StateGraphs" };
		case AssetType::BehaviorGraph:      return { "StateGraphs" };
		default:                            return {};
		}
	}

	// Why a dropped file cannot be imported, or nullptr if it can.
	char const* UnsupportedReason(std::string const& extension) {
		if (extension == ".meta") return "the engine writes .meta files itself";
		if (extension == ".fontsettings" || extension == ".texsettings") return "import settings are made by the importers";
		if (extension == ".gltf" || extension == ".fbx" || extension == ".obj" || extension == ".dae")
			return "only .glb models can be compiled - export it from your 3D program as .glb";
		if (extension == ".dds") return "DDS images cannot be compiled - use .png, .jpg, .tga, .bmp or .hdr";
		if (Crucible::AssetManager::IsCompiledExtension(extension)) return "this is a compiled file - drop its source file instead";
		if (RootsFor(extension).empty()) return "this file type is not supported";
		return nullptr;
	}

	// Copied beside the destination first, then renamed: a failed copy never damages what was there.
	// The temporary name starts with a dot, so the asset scan skips it.
	bool CopyInto(fs::path const& source, fs::path const& destination, std::string& error) {
		std::error_code ec;
		fs::create_directories(destination.parent_path(), ec);
		if (ec) { error = ec.message(); return false; }
		fs::path const temporary = destination.parent_path() / ("." + destination.filename().string() + ".import.tmp");
		fs::copy_file(source, temporary, fs::copy_options::overwrite_existing, ec);
		if (!ec) fs::rename(temporary, destination, ec);
		if (ec) {
			error = ec.message();
			std::error_code ignored;
			fs::remove(temporary, ignored);
			return false;
		}
		return true;
	}
}

// ------------------------------------------------------------------ dropped from File Explorer

Crucible::AssetBrowser::~AssetBrowser() {
	// The GLFW callback stays set (the window may already be gone here); it checks this pointer.
	if (s_dropTarget == this) s_dropTarget = nullptr;
}

void Crucible::AssetBrowser::InstallDropCallback() {
	if (m_dropCallbackInstalled) return;
	GLFWwindow* window = static_cast<GLFWwindow*>(ImGui::GetMainViewport()->PlatformHandle);
	if (window == nullptr) return;

	s_dropTarget = this;
	glfwSetDropCallback(window, [](GLFWwindow* from, int count, char const** paths) {
		if (s_dropTarget == nullptr) return;
		// Only recorded here: this runs inside glfwPollEvents, in the middle of no ImGui frame.
		for (int i = 0; i < count; ++i)
			s_dropTarget->m_droppedPaths.emplace_back(std::u8string(reinterpret_cast<char8_t const*>(paths[i])));
		double x = 0.0, y = 0.0;
		glfwGetCursorPos(from, &x, &y);
		s_dropTarget->m_dropPosition = ImVec2(static_cast<float>(x), static_cast<float>(y));
	});
	m_dropCallbackInstalled = true;
}

void Crucible::AssetBrowser::ProcessDroppedFiles() {
	InstallDropCallback();
	if (m_droppedPaths.empty()) return;
	std::vector<fs::path> const paths = std::move(m_droppedPaths);
	m_droppedPaths.clear();

	// The panel's area, even while another tab in its dock area (the Console) is in front.
	ImGuiWindow* const panel = ImGui::FindWindowByName(m_panelName.c_str());
	if (panel == nullptr) return;
	bool const docked = panel->DockIsActive && panel->DockNode != nullptr;
	ImRect const area = docked ? panel->DockNode->Rect() : panel->Rect();
	if (!area.Contains(m_dropPosition)) {
		CRU_USER_INFO("Drop files on the Asset Browser to import them");
		return;
	}

	bool const hiddenTab = docked && !panel->DockTabIsVisible;
	if (hiddenTab) ImGui::SetWindowFocus(m_panelName.c_str());

	if (!IsRefreshAllowed()) {
		m_importProblems.push_back({ "Import", "stop Play Mode before adding files" });
		return;
	}
	PlanImport(paths, hiddenTab);
}

std::string Crucible::AssetBrowser::ImportFolderFor(std::string const& extension) const {
	auto const roots = RootsFor(extension);
	for (std::string_view const root : roots)
		if (IsInsideRoot(m_currentFolder, root)) return m_currentFolder;   // Assets/Textures/Hero keeps a texture
	return std::string(roots.front());
}

void Crucible::AssetBrowser::PlanImport(std::vector<fs::path> const& paths, bool jumpToFolder) {
	std::error_code ec;
	std::string jumpFolder;

	for (fs::path const& path : paths) {
		std::string name;
		try { name = path.filename().string(); }
		catch (std::exception const&) {   // a character this system's code page cannot write
			m_importProblems.push_back({ "File with an unusual name", "its name has characters the engine cannot use - rename it first" });
			continue;
		}

		if (fs::is_directory(path, ec)) {
			m_importProblems.push_back({ name, "folders cannot be dropped - select the files inside it instead" });
			continue;
		}
		if (!fs::is_regular_file(path, ec)) {
			m_importProblems.push_back({ name, "the file could not be found" });
			continue;
		}

		std::string const extension = Lower(path.extension().string());
		if (char const* reason = UnsupportedReason(extension)) {
			m_importProblems.push_back({ name, reason });
			continue;
		}

		std::string const folder = ImportFolderFor(extension);
		std::string const destination = Join(folder, name);
		fs::path const target = AbsolutePathOf(destination);

		if (fs::exists(target, ec) && fs::equivalent(path, target, ec)) {
			m_importProblems.push_back({ name, "it is already in Assets/" + folder });
			continue;
		}
		if (jumpFolder.empty()) jumpFolder = folder;

		std::string const key = Lower(destination);
		if (fs::exists(target, ec) || m_importPlanned.count(key) != 0) {
			if (m_scenes.HasPath() && fs::equivalent(m_scenes.CurrentPath(), target, ec)) {
				m_importProblems.push_back({ name, "that scene is open in the editor - open another scene first" });
				continue;
			}
			Conflict conflict;
			conflict.source = path;
			conflict.destination = destination;
			m_conflicts.push_back(std::move(conflict));
		}
		else {
			ImportJob job;
			job.source = path;
			job.destination = destination;
			m_importJobs.push_back(std::move(job));
			m_importPlanned.insert(key);
		}
	}

	if (jumpToFolder && !jumpFolder.empty()) NavigateTo(jumpFolder);
	if (m_conflicts.empty()) m_finishImport = true;
}

std::string Crucible::AssetBrowser::UniqueDestination(std::string const& destination) const {
	fs::path const original(destination);
	std::string const folder = ParentOf(destination);
	std::string const stem = original.stem().string();
	std::string const extension = original.extension().string();
	std::error_code ec;
	for (int i = 1; i < 10000; ++i) {
		std::string const candidate = Join(folder, stem + " " + std::to_string(i) + extension);
		if (!fs::exists(AbsolutePathOf(candidate), ec) && m_importPlanned.count(Lower(candidate)) == 0) return candidate;
	}
	return destination;
}

void Crucible::AssetBrowser::ResolveConflict(Conflict conflict, ConflictChoice choice) {
	if (choice == ConflictChoice::Skip) return;
	ImportJob job;
	job.source = std::move(conflict.source);
	job.moveFrom = std::move(conflict.moveFrom);
	job.replace = choice == ConflictChoice::Replace;
	job.destination = job.replace ? std::move(conflict.destination) : UniqueDestination(conflict.destination);
	m_importPlanned.insert(Lower(job.destination));
	m_importJobs.push_back(std::move(job));
}

void Crucible::AssetBrowser::DrawConflictModal() {
	char const* const kTitle = "File Already Exists";
	if (!m_conflicts.empty() && !ImGui::IsPopupOpen(kTitle)) {
		m_conflictApplyToAll = false;
		ImGui::OpenPopup(kTitle);
	}

	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return;
	if (m_conflicts.empty()) {   // nothing to ask: never leave an empty modal blocking the editor
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		return;
	}

	Conflict const& conflict = m_conflicts.front();
	std::string const folder = ParentOf(conflict.destination);
	ImGui::Text("'%s' already exists in Assets%s%s.", FileNameOf(conflict.destination).c_str(), folder.empty() ? "" : "/", folder.c_str());
	ImGui::Spacing();
	if (conflict.moveFrom.empty()) {
		ImGui::TextDisabled("Replace: the new file takes its place and keeps its GUID, so everything using it updates.");
	}
	else {
		ImGui::TextDisabled("Replace: the file already there goes to the Recycle Bin.");
		if (!conflict.usedBy.empty())
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%zu file(s) use it, and their references will point at nothing.", conflict.usedBy.size());
	}
	ImGui::TextDisabled("Keep Both: the new file gets a number after its name.");

	std::size_t const others = m_conflicts.size() - 1;
	if (others > 0) {
		ImGui::Spacing();
		ImGui::Checkbox(("Apply to all " + std::to_string(others) + " other file" + (others == 1 ? "" : "s")).c_str(), &m_conflictApplyToAll);
	}

	ImGui::Spacing();
	std::optional<ConflictChoice> choice;
	if (ImGui::Button("Replace", ImVec2(110.0f, 0.0f)))   choice = ConflictChoice::Replace;
	ImGui::SameLine();
	if (ImGui::Button("Keep Both", ImVec2(110.0f, 0.0f))) choice = ConflictChoice::KeepBoth;
	ImGui::SameLine();
	if (ImGui::Button("Skip", ImVec2(110.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) choice = ConflictChoice::Skip;

	if (choice) {
		std::size_t const count = m_conflictApplyToAll ? m_conflicts.size() : 1;
		for (std::size_t i = 0; i < count; ++i) {
			ResolveConflict(std::move(m_conflicts.front()), *choice);
			m_conflicts.pop_front();
		}
		if (m_conflicts.empty()) {
			m_finishImport = true;
			ImGui::CloseCurrentPopup();
		}
	}
	ImGui::EndPopup();
}

void Crucible::AssetBrowser::DrawImportProblemsModal() {
	char const* const kTitle = "Could Not Add Files";
	// After the "already exists" questions, so one modal never covers the other.
	if (!m_importProblems.empty() && m_conflicts.empty() && !ImGui::IsPopupOpen(kTitle))
		ImGui::OpenPopup(kTitle);

	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSizeConstraints(ImVec2(360.0f, 0.0f), ImVec2(720.0f, 480.0f));
	if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	for (ImportProblem const& problem : m_importProblems) {
		ImGui::TextUnformatted(problem.file.c_str());
		ImGui::Indent();
		ImGui::TextDisabled("%s", problem.reason.c_str());
		ImGui::Unindent();
	}
	ImGui::Spacing();
	if (ImGui::Button("OK", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		m_importProblems.clear();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void Crucible::AssetBrowser::FinishImport() {
	if (!m_finishImport) return;
	m_finishImport = false;
	m_importPlanned.clear();
	std::vector<ImportJob> const jobs = std::move(m_importJobs);
	m_importJobs.clear();
	if (jobs.empty()) return;

	if (!IsRefreshAllowed()) {   // Play may have started while a popup was up
		m_importProblems.push_back({ "Import", "stop Play Mode before adding or moving files" });
		return;
	}

	std::string lastDone;
	for (ImportJob const& job : jobs) {
		std::string const name = FileNameOf(job.destination);
		std::string error;

		if (!job.moveFrom.empty()) {
			// The file being replaced goes first; the Recycle Bin keeps it if the move then fails.
			if (job.replace && !DeleteOnDisk(job.destination)) continue;
			if (!MoveOnDisk(job.moveFrom, job.destination, error)) {
				m_importProblems.push_back({ name, "it could not be moved: " + error });
				continue;
			}
			CRU_USER_INFO("Moved {} -> {}", job.moveFrom, job.destination);
		}
		else {
			if (!CopyInto(job.source, AbsolutePathOf(job.destination), error)) {
				m_importProblems.push_back({ name, "it could not be copied: " + error });
				continue;
			}
			if (job.replace) ForgetCompiledCopy(job.destination);
			CRU_USER_INFO("{} {}", job.replace ? "Replaced" : "Imported", job.destination);
		}
		lastDone = job.destination;
	}

	if (lastDone.empty()) return;
	Refresh();   // indexes the files, hot-reloads what changed, and queues new models for compiling
	m_selection.clear();
	// Otherwise the footer keeps naming a file that has just left this folder.
	if (ParentOf(lastDone) == m_currentFolder) m_selected = lastDone;
	else m_selected.clear();
}

void Crucible::AssetBrowser::ForgetCompiledCopy(std::string const& virtualPath) {
	std::string const extension = Lower(fs::path(virtualPath).extension().string());
	fs::path const root = m_assets.GetResourceRoot();
	if (root.empty()) return;

	if (extension == ".ttf" || extension == ".otf") {
		// The importer's Compile button is the only thing that bakes a font's atlas.
		CRU_USER_WARN("{} changed - open it in the Font Importer and press Compile to use the new file", virtualPath);
		return;
	}
	if (AssetManager::ClassifyExtension(extension) != AssetType::Texture) return;

	// A compiled image wins over its source, so the old pixels would keep showing. Without it the
	// editor reads the new image directly, and the next build compiles it.
	std::error_code ec;
	for (char const* compiled : { ".bill", ".sky" }) {
		fs::path file = root / fs::path(virtualPath);
		file.replace_extension(compiled);
		fs::remove(file, ec);
		fs::remove(AssetRegistry::MetaPathFor(file), ec);
	}
}

// ------------------------------------------------------------------ moving inside the browser

std::string Crucible::AssetBrowser::MoveRefusal(std::string const& from, std::string const& folder) const {
	if (!IsRefreshAllowed()) return "Stop Play Mode to move assets";
	auto const roots = RootsFor(Lower(fs::path(from).extension().string()));
	if (roots.empty()) return "This file type has no asset folder, so it stays where it is";
	for (std::string_view const root : roots)
		if (IsInsideRoot(folder, root)) return {};
	return "This file can only move inside Assets/" + std::string(roots.front());
}

void Crucible::AssetBrowser::AcceptAssetMove(std::string const& folder) {
	ImGuiPayload const* const dragged = ImGui::GetDragDropPayload();
	if (dragged == nullptr || !dragged->IsDataType(EditorWidgets::kAssetPayload)) return;
	std::string const from(static_cast<char const*>(dragged->Data));
	std::vector<std::string> const files = FilesToMove(from);

	std::size_t movable = 0, alreadyThere = 0;
	std::string refusal;
	for (std::string const& file : files) {
		if (ParentOf(file) == folder) { ++alreadyThere; continue; }
		std::string const why = MoveRefusal(file, folder);
		if (why.empty()) ++movable;
		else if (refusal.empty()) refusal = why;
	}
	if (alreadyThere == files.size()) return;

	bool const hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	if (movable == 0) {
		if (hovered) ImGui::SetTooltip("%s", refusal.c_str());
		return;   // not a target at all, so ImGui shows no highlight and the drop does nothing
	}
	if (hovered && !refusal.empty())   // a mixed selection: say so before the drop, not only after
		ImGui::SetTooltip("Moves %zu of %zu files. The others stay where they are.", movable, files.size());

	if (ImGui::BeginDragDropTarget()) {
		if (ImGui::AcceptDragDropPayload(EditorWidgets::kAssetPayload))
			m_pendingMove = std::make_pair(files, folder);
		ImGui::EndDragDropTarget();
	}
}

std::vector<std::string> Crucible::AssetBrowser::FilesToMove(std::string const& dragged) const {
	if (IsMultiSelect() && IsSelected(dragged)) return m_selection;
	return { dragged };   // a drag starting on an unselected file moves only that file
}

void Crucible::AssetBrowser::ApplyPendingMove() {
	if (!m_pendingMove) return;
	auto const [files, folder] = std::move(*m_pendingMove);
	m_pendingMove.reset();

	std::error_code ec;
	for (std::string const& from : files) {
		if (ParentOf(from) == folder) continue;
		std::string const name = FileNameOf(from);

		if (std::string const refusal = MoveRefusal(from, folder); !refusal.empty()) {
			m_importProblems.push_back({ name, refusal });
			continue;
		}

		std::string const destination = Join(folder, name);
		fs::path const target = AbsolutePathOf(destination);
		// Planned too: selected in search results, two files can share a name.
		bool const taken = fs::exists(target, ec) || m_importPlanned.count(Lower(destination)) != 0;
		if (!taken) {
			ImportJob job;
			job.moveFrom = from;
			job.destination = destination;
			m_importJobs.push_back(std::move(job));
			m_importPlanned.insert(Lower(destination));
			continue;
		}
		if (m_scenes.HasPath() && fs::exists(target, ec) && fs::equivalent(m_scenes.CurrentPath(), target, ec)) {
			m_importProblems.push_back({ name, "the scene it would replace is open in the editor - open another scene first" });
			continue;
		}

		Conflict conflict;
		conflict.moveFrom = from;
		conflict.destination = destination;
		std::size_t ignored = 0;
		conflict.usedBy = FindReferences(destination, false, ignored);
		m_conflicts.push_back(std::move(conflict));
	}

	if (m_conflicts.empty()) {
		m_finishImport = true;
		FinishImport();
	}
}

// ------------------------------------------------------------------ selecting several files

bool Crucible::AssetBrowser::IsMultiSelect() const {
	return m_selection.size() > 1 && std::find(m_selection.begin(), m_selection.end(), m_selected) != m_selection.end();
}

bool Crucible::AssetBrowser::IsSelected(std::string const& virtualPath) const {
	if (virtualPath == m_selected) return true;
	return IsMultiSelect() && std::find(m_selection.begin(), m_selection.end(), virtualPath) != m_selection.end();
}

void Crucible::AssetBrowser::ToggleInSelection(std::string const& virtualPath) {
	// Starting from one selected file: it joins the selection, unless it is a folder.
	if (!IsMultiSelect()) {
		m_selection.clear();
		auto const current = std::find_if(m_items.begin(), m_items.end(),
			[this](Item const& item) { return item.virtualPath == m_selected; });
		if (current != m_items.end() && !current->isFolder) m_selection.push_back(m_selected);
	}

	m_selectionAnchor = virtualPath;
	auto const found = std::find(m_selection.begin(), m_selection.end(), virtualPath);
	if (found == m_selection.end()) {
		m_selection.push_back(virtualPath);
		m_selected = virtualPath;
		return;
	}
	m_selection.erase(found);
	if (virtualPath == m_selected || m_selection.empty())
		m_selected = m_selection.empty() ? std::string{} : m_selection.back();
}

void Crucible::AssetBrowser::SelectRangeTo(std::string const& virtualPath) {
	auto const indexOf = [this](std::string const& path) {
		return std::find_if(m_items.begin(), m_items.end(), [&](Item const& item) { return item.virtualPath == path; });
	};
	auto const target = indexOf(virtualPath);
	auto anchor = indexOf(m_selectionAnchor.empty() ? m_selected : m_selectionAnchor);
	if (anchor == m_items.end()) anchor = target;
	if (target == m_items.end()) return;

	auto first = anchor, last = target;
	if (std::distance(m_items.begin(), first) > std::distance(m_items.begin(), last)) std::swap(first, last);

	m_selection.clear();
	for (auto it = first; it != last + 1; ++it)
		if (!it->isFolder) m_selection.push_back(it->virtualPath);   // folders cannot be moved this way
	m_selected = virtualPath;
	if (m_selectionAnchor.empty()) m_selectionAnchor = anchor->virtualPath;
}

// ------------------------------------------------------------------ compiling models

void Crucible::AssetBrowser::QueueModelCompiles() {
	if (m_assets.GetResourceRoot().empty()) return;
	fs::path const resources = m_assets.GetResourceRoot();

	std::unordered_set<std::uint64_t> changed;
	for (xresource::instance_guid const guid : m_assets.GetChangedOnLastRefresh()) changed.insert(guid.m_Value);

	for (AssetEntry const& entry : m_assets.GetAll()) {
		if (entry.compiled || entry.type != AssetType::Model) continue;
		std::uint64_t const guid = entry.guid.m_Value;

		// A failure is remembered per version of the file, so it is reported once, not on every refresh.
		if (auto const failed = m_modelFailures.find(guid); failed != m_modelFailures.end() && failed->second == entry.contentHash) continue;

		if (entry.extension != ".glb") {
			m_modelFailures[guid] = entry.contentHash;
			m_importProblems.push_back({ entry.virtualPath, "only .glb models can be compiled, and the next build will stop on this file - remove it or export a .glb" });
			continue;
		}

		AssetEntry const* const current = m_assets.FindByGuid(entry.guid);
		bool const compiled = current != nullptr && current->extension == ".cmodel";
		if (compiled && changed.count(guid) == 0) continue;
		// Its new .cmodel is also reported as changed: this version of the .glb is already done.
		if (auto const done = m_modelCompiled.find(guid); compiled && done != m_modelCompiled.end() && done->second == entry.contentHash) continue;
		if (m_runningModelGuid == guid || std::any_of(m_modelQueue.begin(), m_modelQueue.end(),
			[guid](ModelJob const& job) { return job.guid == guid; })) continue;

		ModelJob job;
		job.guid = guid;
		job.contentHash = entry.contentHash;
		job.source = entry.absolutePath;
		job.virtualPath = entry.virtualPath;
		if (compiled) job.compiledPath = current->virtualPath;   // a stable path that survives a source rename, as in the build
		else job.compiledPath = fs::path(entry.virtualPath).replace_extension(".cmodel").generic_string();
		job.target = resources / fs::path(job.compiledPath);

		// A .cmodel left at this path by a model since deleted is overwritten. If its model still
		// exists, two sources want one output, which the build refuses as well.
		AssetMeta existing;
		std::error_code ec;
		if (fs::exists(job.target, ec) && AssetRegistry::ReadMeta(AssetRegistry::MetaPathFor(job.target), existing) && existing.guid.m_Value != guid) {
			bool const stillOwned = std::any_of(m_assets.GetAll().begin(), m_assets.GetAll().end(),
				[&](AssetEntry const& other) { return !other.compiled && other.guid.m_Value == existing.guid.m_Value; });
			if (stillOwned) {
				m_modelFailures[guid] = entry.contentHash;
				m_importProblems.push_back({ entry.virtualPath, "another model already compiles to " + job.compiledPath + " - rename this file" });
				continue;
			}
		}
		m_modelQueue.push_back(std::move(job));
	}
}

void Crucible::AssetBrowser::UpdateModelCompiles() {
	if (m_modelJob.valid() && m_modelJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		ModelCompileResult const result = m_modelJob.get();
		ModelJob const job = std::move(m_runningModel);
		m_runningModelGuid = 0;

		if (!result.ok) {
			m_modelFailures[job.guid] = job.contentHash;
			CRU_USER_ERROR("[Models] {} failed to compile: {}", job.virtualPath, result.error);
			m_importProblems.push_back({ job.virtualPath, "it could not be compiled: " + result.error });
		}
		else if (m_modelCompiled[job.guid] = job.contentHash; !result.unchanged) {
			m_modelFailures.erase(job.guid);
			CRU_USER_INFO("[Models] Compiled {} -> {}: {} in {:.2f} s", job.virtualPath, job.compiledPath, result.summary, result.seconds);
			m_modelRefreshPending = true;
		}
	}

	if (!m_modelJob.valid() && !m_modelQueue.empty()) {
		m_runningModel = std::move(m_modelQueue.front());
		m_modelQueue.pop_front();
		m_runningModelGuid = m_runningModel.guid;

		ModelJob const job = m_runningModel;
		CRU_USER_INFO("[Models] Compiling {}...", job.virtualPath);
		m_modelJob = std::async(std::launch::async, [job]() {
			ModelCompileResult result;
			auto const start = std::chrono::steady_clock::now();
			try {
				auto const bytes = ModelBuild::ReadFile(job.source);
				std::uint64_t const sourceHash = ModelBuild::SourceHash(bytes);
				if (ModelBuild::IsUpToDate(job.target, job.guid, sourceHash)) {
					result.unchanged = true;   // what stops compile -> refresh -> compile going round for ever
				}
				else {
					auto const compiled = ModelBuild::Compile(bytes, job.guid, sourceHash);
					ModelBuild::Write(job.target, compiled.bytes, job.guid, job.compiledPath);
					result.summary = ModelBuild::Summary(compiled.model);
				}
				result.ok = true;
			}
			catch (std::exception const& e) {
				result.error = e.what();
			}
			result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
			return result;
		});
	}

	// Indexes the new .cmodel, and the hot reload puts it on every entity using this model.
	if (m_modelRefreshPending && IsRefreshAllowed()) {
		m_modelRefreshPending = false;
		Refresh();
	}
}
