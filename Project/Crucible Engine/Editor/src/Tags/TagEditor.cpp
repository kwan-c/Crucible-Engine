/*************************************************************************
* \file:         TagEditor.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          13 November 2026
* \version       1.1
* \brief
*                Implementation of the editor's tag UI - see TagEditor.h.
*
* \history
*    -v1.0       Inspector tag field, New Tag dialog, Tag Manager,
*                TagListCommand
*    -v1.1       project-wide Delete (Replace With / Reset to Untagged) and
*                Rename through TagReferences, with a scan before each;
*                Untagged shown as protected; TagListCommand without
*                entity steps
*
**************************************************************************/
#include "Tags/TagEditor.h"
#include "Tags/TagReferences.h"

#include "ImGui/ImGuiManager.h"
#include "ImGui/EditorTheme.h"
#include "ImGui/Panels/ImGuiPanel_AssetBrowser.h"
#include "ImGui/Panels/ImGuiPanel_Inspector.h"
#include "AssetPipe/AssetPaths.h"
#include "AssetPipe/Assetmanager.h"
#include "Core.h"
#include "Diagnostics/Logger.h"
#include "ECS/World.h"
#include "ECS/TagRegistry.h"
#include "ECS/Components/Components.h"
#include "Resources/ResourceManager.h"
#include "Scene/SceneManager.h"
#include "misc/cpp/imgui_stdlib.h"   // ImGui::InputText(std::string*)

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace Crucible
{
	namespace
	{
		namespace fs = std::filesystem;

		constexpr char const* kNewTagPopup = "New Tag##TagEditor";
		constexpr char const* kRenamePopup = "Rename Tag##TagEditor";
		constexpr char const* kDeletePopup = "Delete Tag##TagEditor";

		// The Inspector's "Add New Tag..." dialog (one Inspector, so one dialog).
		struct NewTagDialog
		{
			std::string name;
			bool        assign = true;
		};
		NewTagDialog g_newTag;

		// What the Delete dialog offers for a tag in use.
		enum class DeleteChoice : int { Replace = 0, Reset = 1 };

		// The Tag Manager window and its two dialogs.
		struct ManagerState
		{
			bool        open = false;
			bool        focus = false;          // bring it to the front next frame
			std::string addName;
			std::string status;                 // the last thing that happened, shown at the bottom
			std::vector<std::string> statusDetails;

			bool                    renameRequested = false;
			std::string             renameFrom, renameTo;
			TagReferences::Report   renameReport;  // scanned when the dialog opened

			bool                    deleteRequested = false;
			std::string             deleteTag;
			TagReferences::Report   deleteReport;  // scanned when Delete was pressed
			int                     deleteChoice = static_cast<int>(DeleteChoice::Replace);
			std::string             replaceWith;

			// The last project-wide operation that failed, shown in its dialog.
			std::string              failure;
			std::vector<std::string> failureDetails;
		};
		ManagerState g_manager;

		using TagUsers = std::map<std::string, std::vector<Entity>, std::less<>>;

		bool InPlay()
		{
			Core* core = EditorManager::GetInstance().GetCore();
			return core && core->IsInPlaySession();
		}

		// An Inspector edit still open becomes its own undo step before ours is pushed.
		void CommitInspectorEdit()
		{
			if (auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR)) { inspector->CommitPendingEdit(); }
		}

		void SelectEntities(std::vector<Entity> const& entities)
		{
			if (entities.empty()) { return; }
			if (auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR)) {
				inspector->SetSelection(entities, entities.back());
			}
		}

		// Every entity of the open scene, grouped by tag (one pass).
		TagUsers EntitiesByTag(World& world)
		{
			TagUsers users;
			world.CreateQuery<const EntityTag>().ForEach([&](Entity entity, EntityTag const& tag) { users[tag.tag].push_back(entity); });
			return users;
		}

		std::size_t CountOf(TagUsers const& users, std::string_view tag)
		{
			auto const it = users.find(tag);
			return it == users.end() ? 0 : it->second.size();
		}

		// Why name can't be a new tag, or nullptr if it can.
		char const* NewNameProblem(TagRegistry const& registry, std::string const& name)
		{
			if (name == kUntaggedTag) { return ToString(TagError::Reserved); }
			TagError const check = TagRegistry::ValidateName(name);
			if (check != TagError::Ok) { return ToString(check); }
			if (registry.IsDefined(name)) { return ToString(TagError::Duplicate); }
			return nullptr;
		}

		// A defined tag that differs from name only by letter case ("" if none): tags are
		// case-sensitive, so "enemy" next to "Enemy" is allowed but usually a typo.
		std::string CaseTwin(std::vector<std::string> const& tags, std::string const& name)
		{
			auto const lower = [](std::string text) {
				std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return text;
			};
			std::string const wanted = lower(name);
			for (std::string const& tag : tags) {
				if (tag != name && lower(tag) == wanted) { return tag; }
			}
			return {};
		}

		fs::path OpenSceneFile(SceneManager const& scenes)
		{
			return scenes.HasPath() ? scenes.CurrentPath() : fs::path{};
		}

		TagReferences::Report ScanProject(World& world, SceneManager const& scenes, std::string const& tag)
		{
			return TagReferences::Scan(world, tag, AssetRoot(), OpenSceneFile(scenes), TagRegistry::ProjectFile());
		}

		// Backups go next to the build output (git-ignored; clean.bat deletes them with it).
		fs::path BackupRoot()
		{
			fs::path const& assets = AssetRoot();
			return assets.empty() ? fs::path{} : assets.parent_path() / "Build" / "TagBackups";
		}

		void Wrapped(char const* text, bool disabled = false)
		{
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 34.0f);
			if (disabled) { ImGui::TextDisabled("%s", text); }
			else { ImGui::TextUnformatted(text); }
			ImGui::PopTextWrapPos();
		}

		std::string Plural(std::size_t count, char const* one, char const* many)
		{
			return std::to_string(count) + " " + (count == 1 ? one : many);
		}

		// ---- report sections, shared by both dialogs -------------------------------------

		void DrawUsage(TagReferences::Report const& report)
		{
			using TagReferences::FileUsage;
			if (report.liveEntities > 0) { ImGui::BulletText("Open scene: %s (unsaved edits included)", Plural(report.liveEntities, "entity", "entities").c_str()); }
			std::size_t const scenes = report.SceneFiles(), prefabs = report.PrefabFiles();
			ImGui::BulletText("Scenes: %zu    Prefabs: %zu    Entities: %zu", scenes, prefabs, report.Entities());
			if (!report.files.empty() && ImGui::TreeNode("Files")) {
				for (FileUsage const& file : report.files) {
					ImGui::BulletText("%s%s  (%s)", file.display.c_str(), file.openScene ? "  [open scene's file]" : "",
						Plural(file.entities, "entity", "entities").c_str());
				}
				ImGui::TreePop();
			}
		}

		void DrawSettingsBlock(TagReferences::Report const& report)
		{
			if (report.settings.empty()) { return; }
			ImGui::TextColored(EditorTheme::GetPalette().warning, "Saved tag settings name this tag (e.g. navigation obstacle tags):");
			for (TagReferences::SettingUsage const& setting : report.settings) {
				ImGui::BulletText("%s: %s", setting.file.c_str(), setting.key.c_str());
			}
			Wrapped("They aren't changed automatically, so this is blocked. Change those settings first.");
		}

		void DrawProblems(TagReferences::Report const& report)
		{
			if (report.Complete()) { return; }
			ImGui::TextColored(EditorTheme::GetPalette().error, "The project scan is incomplete - nothing can be changed:");
			for (std::string const& problem : report.problems) { ImGui::BulletText("%s", problem.c_str()); }
			Wrapped("Fix or remove those files, then try again.");
		}

		void DrawScripts(TagReferences::Report const& report)
		{
			if (report.scripts.empty()) {
				Wrapped("No script uses this name as a string literal. Scripts that build tag names at run time "
				        "can't be checked.", true);
				return;
			}
			ImGui::TextColored(EditorTheme::GetPalette().warning, "Scripts mention \"%s\" (not changed - update them by hand):", report.tag.c_str());
			if (ImGui::TreeNode("Scripts", "%zu line(s)", report.scripts.size())) {
				for (TagReferences::ScriptUsage const& use : report.scripts) {
					ImGui::BulletText("%s:%zu  %s", use.file.c_str(), use.line, use.text.c_str());
				}
				ImGui::TreePop();
			}
			Wrapped("Tag names built at run time can't be found.", true);
		}

		void DrawNavigationNote(TagReferences::Report const& report)
		{
			if (report.navigationScenes.empty()) { return; }
			std::string scenes;
			for (std::string const& scene : report.navigationScenes) { scenes += (scenes.empty() ? "" : ", ") + scene; }
			std::string const note = "Navigation: if the navigation grid filters obstacles by tag, changing tags changes "
				"what counts as an obstacle - rebake it (FlowFieldSurface > Bake, or Play) in: " + scenes;
			Wrapped(note.c_str(), true);
		}

		void DrawFailure()
		{
			if (g_manager.failure.empty()) { return; }
			ImGui::Separator();
			ImGui::TextColored(EditorTheme::GetPalette().error, "%s", g_manager.failure.c_str());
			for (std::string const& detail : g_manager.failureDetails) { ImGui::BulletText("%s", detail.c_str()); }
		}

		// ---- the changes -------------------------------------------------------------------

		// Removes an unused tag from the list (an undo step).
		void DeleteUnusedTag(std::string const& tag)
		{
			CommitInspectorEdit();
			TagRegistry& registry = TagRegistry::Get();
			int const index = registry.IndexOf(tag);
			TagError const result = registry.Remove(tag);
			if (result != TagError::Ok) {
				g_manager.status = "Could not delete '" + tag + "': " + ToString(result);
				return;
			}
			registry.SaveProjectFile();
			EditorManager::GetInstance().GetHistory().Push(std::make_unique<TagListCommand>(
				TagListCommand::Kind::Remove, tag, std::string{}, static_cast<std::size_t>(std::max(index, 1)), "Delete Tag '" + tag + "'"));
			g_manager.status = "Deleted '" + tag + "' (unused)";
			g_manager.statusDetails.clear();
		}

		// Renames an unused tag in the list (an undo step).
		void RenameUnusedTag(std::string const& from, std::string const& to)
		{
			CommitInspectorEdit();
			TagRegistry& registry = TagRegistry::Get();
			TagError const result = registry.Rename(from, to);
			if (result != TagError::Ok) {
				g_manager.status = "Could not rename '" + from + "': " + ToString(result);
				return;
			}
			registry.SaveProjectFile();
			EditorManager::GetInstance().GetHistory().Push(std::make_unique<TagListCommand>(
				TagListCommand::Kind::Rename, from, to, 0, "Rename Tag '" + from + "' to '" + to + "'"));
			g_manager.status = "Renamed '" + from + "' to '" + to + "' (unused)";
			g_manager.statusDetails.clear();
		}

		// Renames or deletes a tag in every scene, prefab and the open scene (TagReferences::Retag),
		// then brings the editor up to date. True on success; on failure g_manager.failure says why.
		bool RetagProject(World& world, SceneManager& scenes, std::string const& from, std::string const& to,
		                  TagReferences::Operation operation)
		{
			CommitInspectorEdit();
			bool const wasDirty = scenes.IsDirty();

			TagReferences::Options options;
			options.assetsRoot    = AssetRoot();
			options.openSceneFile = OpenSceneFile(scenes);
			options.registryFile  = TagRegistry::ProjectFile();
			options.backupRoot    = BackupRoot();
			TagReferences::Result const result = TagReferences::Retag(world, TagRegistry::Get(), options, from, to, operation);
			if (!result.ok) {
				g_manager.failure = result.message;
				g_manager.failureDetails = result.details;
				CRU_USER_ERROR("[Tags] {}", result.message);
				return false;
			}

			// Steps recorded before this name the old tag (and the files changed underneath them):
			// replaying them could bring back a deleted tag, so the history starts again.
			EditorManager::GetInstance().GetHistory().Clear();

			// Prefabs rewritten on disk: the shared cached copies, then the asset index (.meta hashes).
			if (Core* core = EditorManager::GetInstance().GetCore()) {
				for (fs::path const& file : result.changedFiles) {
					if (file.extension() != ".cprefab") { continue; }
					std::string virtualPath = PathToUtf8(file.lexically_relative(AssetRoot()));
					std::replace(virtualPath.begin(), virtualPath.end(), '\\', '/');
					std::uint64_t const id = core->GetAssets().GetGuid(virtualPath).m_Value;
					if (id != 0) { core->GetResources().ReloadPrefab(id); }
				}
			}
			if (!result.changedFiles.empty()) {
				if (auto browser = EditorManager::GetInstance().GetPanel<AssetBrowser>(ImguiPanels::ASSETBROWSER)) { browser->Refresh(); }
			}

			// The open scene's FILE was rewritten too, so a scene that had no unsaved edits still
			// matches its file. An unsaved new scene only changed in memory: it needs saving.
			if (result.liveEntitiesChanged > 0 && (wasDirty || !scenes.HasPath())) { scenes.MarkDirty(); }

			g_manager.status = result.message + (result.backupFolder.empty() ? std::string{}
				: " Originals backed up in " + PathToUtf8(result.backupFolder) + ".");
			g_manager.statusDetails = result.details;
			g_manager.failure.clear();
			g_manager.failureDetails.clear();
			CRU_USER_INFO("[Tags] {}", g_manager.status);
			return true;
		}

		// ---- the manager's dialogs ---------------------------------------------------------

		void DrawRenameDialog(World& world, SceneManager& scenes)
		{
			if (g_manager.renameRequested) {
				g_manager.renameRequested = false;
				ImGui::OpenPopup(kRenamePopup);
			}
			if (!ImGui::BeginPopupModal(kRenamePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) { return; }

			TagReferences::Report const& report = g_manager.renameReport;
			ImGui::Text("Rename tag '%s'", g_manager.renameFrom.c_str());
			ImGui::Separator();
			if (ImGui::IsWindowAppearing()) { ImGui::SetKeyboardFocusHere(); }
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
			bool const enter = ImGui::InputText("##RenameTo", &g_manager.renameTo, ImGuiInputTextFlags_EnterReturnsTrue);
			TagRegistry const& registry = TagRegistry::Get();
			char const* problem = g_manager.renameTo == g_manager.renameFrom ? "type a new name" : NewNameProblem(registry, g_manager.renameTo);
			if (problem) { ImGui::TextColored(EditorTheme::GetPalette().warning, "%s", problem); }

			bool const blocked = !report.Complete() || !report.settings.empty();
			if (report.InUse()) {
				ImGui::Spacing();
				ImGui::TextUnformatted("This tag is used by:");
				DrawUsage(report);
				Wrapped("Rename updates every one of them: the scene and prefab files are rewritten (originals backed up "
				        "in Build/TagBackups), and the open scene's entities change in memory - its other unsaved edits "
				        "are not saved. This can't be undone with Ctrl+Z; the undo history is cleared.", true);
			}
			else if (report.Complete()) {
				Wrapped("No scene, prefab or entity uses it. Ctrl+Z undoes the rename.", true);
			}
			DrawSettingsBlock(report);
			DrawProblems(report);
			DrawScripts(report);
			if (report.InUse()) { DrawNavigationNote(report); }
			DrawFailure();

			ImGui::Spacing();
			bool const disabled = problem != nullptr || blocked;
			ImGui::BeginDisabled(disabled);
			char const* const label = report.InUse() ? "Rename Everywhere" : "Rename";
			bool const rename = ImGui::Button(label, ImVec2(160.0f, 0.0f)) || (enter && !disabled);
			ImGui::EndDisabled();
			ImGui::SameLine();
			bool const cancel = ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape);
			if (rename && !disabled) {
				bool const done = report.InUse()
					? RetagProject(world, scenes, g_manager.renameFrom, g_manager.renameTo, TagReferences::Operation::Rename)
					: (RenameUnusedTag(g_manager.renameFrom, g_manager.renameTo), true);
				if (done) { ImGui::CloseCurrentPopup(); }
			}
			else if (cancel) {
				g_manager.failure.clear();   // nothing was changed
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		void DrawDeleteDialog(World& world, SceneManager& scenes)
		{
			if (g_manager.deleteRequested) {
				g_manager.deleteRequested = false;
				ImGui::OpenPopup(kDeletePopup);
			}
			if (!ImGui::BeginPopupModal(kDeletePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) { return; }

			TagReferences::Report const& report = g_manager.deleteReport;
			std::string const& tag = g_manager.deleteTag;
			ImGui::Text("Delete Tag: %s", tag.c_str());
			ImGui::Separator();

			bool const blocked = !report.Complete() || !report.settings.empty();
			bool confirm = false;
			char const* confirmLabel = "Delete";
			std::string replacement = std::string(kUntaggedTag);

			if (report.InUse()) {
				ImGui::TextUnformatted("This tag is currently used by:");
				DrawUsage(report);
				DrawSettingsBlock(report);
				DrawProblems(report);
				DrawScripts(report);
				DrawNavigationNote(report);

				if (!blocked) {
					ImGui::SeparatorText("Choose an action");
					ImGui::RadioButton("Replace with", &g_manager.deleteChoice, static_cast<int>(DeleteChoice::Replace));
					ImGui::SameLine();
					std::vector<std::string> const tags = TagRegistry::Get().Tags();
					std::vector<std::string> choices;
					for (std::string const& other : tags) {
						if (other != tag && other != kUntaggedTag) { choices.push_back(other); }
					}
					if (std::find(choices.begin(), choices.end(), g_manager.replaceWith) == choices.end()) {
						g_manager.replaceWith = choices.empty() ? std::string{} : choices.front();
					}
					ImGui::BeginDisabled(g_manager.deleteChoice != static_cast<int>(DeleteChoice::Replace) || choices.empty());
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
					if (ImGui::BeginCombo("##ReplaceWith", choices.empty() ? "(no other tag)" : g_manager.replaceWith.c_str())) {
						for (std::string const& other : choices) {
							if (ImGui::Selectable(other.c_str(), other == g_manager.replaceWith)) { g_manager.replaceWith = other; }
						}
						ImGui::EndCombo();
					}
					ImGui::EndDisabled();
					ImGui::RadioButton("Reset to Untagged", &g_manager.deleteChoice, static_cast<int>(DeleteChoice::Reset));

					bool const replace = g_manager.deleteChoice == static_cast<int>(DeleteChoice::Replace);
					replacement = replace ? g_manager.replaceWith : std::string(kUntaggedTag);
					std::string const outcome = "Every use of '" + tag + "' becomes '" + (replacement.empty() ? "?" : replacement) +
						"', then '" + tag + "' is removed. Scene and prefab files are rewritten (originals backed up in "
						"Build/TagBackups); the open scene's entities change in memory and its other unsaved edits are not "
						"saved. This can't be undone with Ctrl+Z; the undo history is cleared.";
					Wrapped(outcome.c_str(), true);
					confirmLabel = replace ? "Replace and Delete" : "Reset to Untagged and Delete";
					confirm = !replacement.empty();
				}
			}
			else {
				// Not used by any entity or setting - only scripts mention it.
				DrawProblems(report);
				Wrapped("No scene, prefab or entity uses this tag.");
				DrawScripts(report);
				confirm = report.Complete();
			}
			DrawFailure();

			ImGui::Spacing();
			ImGui::BeginDisabled(!confirm || blocked);
			bool const go = ImGui::Button(confirmLabel, ImVec2(220.0f, 0.0f));
			ImGui::EndDisabled();
			ImGui::SameLine();
			bool const cancel = ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape);
			if (go && confirm && !blocked) {
				bool const done = report.InUse()
					? RetagProject(world, scenes, tag, replacement, TagReferences::Operation::Delete)
					: (DeleteUnusedTag(tag), true);
				if (done) { ImGui::CloseCurrentPopup(); }
			}
			else if (cancel) {
				g_manager.failure.clear();   // nothing was changed
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	// =======================================================================
	//  TagListCommand
	// =======================================================================

	void TagListCommand::Apply(bool forward)
	{
		TagRegistry& registry = TagRegistry::Get();
		TagError result = TagError::Ok;
		switch (m_kind) {
			case Kind::Add:
			case Kind::Remove: {
				bool const insert = (m_kind == Kind::Add) == forward;   // Add redone, or Remove undone
				result = insert ? registry.Insert(m_index, m_tag) : registry.Remove(m_tag);
				break;
			}
			case Kind::Rename:
				result = forward ? registry.Rename(m_tag, m_renamedTo) : registry.Rename(m_renamedTo, m_tag);
				break;
		}
		if (result == TagError::Ok) { registry.SaveProjectFile(); }
		else { CRU_USER_WARN("[Tags] {} '{}': the tag list has changed since ({})", forward ? "Redo" : "Undo", m_label, ToString(result)); }
	}

	void TagListCommand::Undo(UndoHistory&) { Apply(false); }
	void TagListCommand::Redo(UndoHistory&) { Apply(true); }

	// =======================================================================
	//  Inspector field
	// =======================================================================

	TagEditor::TagPick TagEditor::DrawTagField(World const& world, Entity entity)
	{
		TagPick pick;
		if (!world.IsAlive(entity)) { return pick; }

		TagRegistry& registry = TagRegistry::Get();
		std::vector<std::string> const tags = registry.Tags();   // read every frame: list changes show at once
		std::string const current(world.GetEntityTag(entity));
		bool const defined = std::find(tags.begin(), tags.end(), current) != tags.end();

		// Lined up with the Name field above it.
		float const fieldX = ImGui::GetCursorPosX() + ImGui::CalcTextSize("Name").x + ImGui::GetStyle().ItemSpacing.x;
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Tag");
		ImGui::SameLine(fieldX);
		ImGui::SetNextItemWidth(-FLT_MIN);

		bool openNewTag = false;
		std::string const preview = defined ? current : current + "  (undefined)";
		if (ImGui::BeginCombo("##EntityTag", preview.c_str())) {
			for (std::string const& tag : tags) {
				bool const selected = tag == current;
				if (ImGui::Selectable(tag.c_str(), selected) && !selected) {
					pick.assign = true;
					pick.tag = tag;
				}
				if (selected) { ImGui::SetItemDefaultFocus(); }
			}
			ImGui::Separator();
			if (ImGui::Selectable("Add New Tag...")) { openNewTag = true; }
			if (ImGui::Selectable("Manage Tags...")) { OpenManager(); }
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("A label other systems filter by (e.g. Obstacle for navigation).\n"
		                      "The list is the project's: Assets/Config/Tags.json.");

		if (!defined) {
			ImGui::TextColored(EditorTheme::GetPalette().warning, "'%s' is not a project tag", current.c_str());
			ImGui::SetItemTooltip("It was deleted or renamed, or comes from another branch's Tags.json.\n"
			                      "The entity keeps it until you pick another tag - or add it back in Manage Tags...");
		}

		// ---- Add New Tag... ----
		if (openNewTag) {
			g_newTag = NewTagDialog{};
			ImGui::OpenPopup(kNewTagPopup);
		}
		if (ImGui::BeginPopupModal(kNewTagPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted("New project tag");
			if (ImGui::IsWindowAppearing()) { ImGui::SetKeyboardFocusHere(); }
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
			bool const enter = ImGui::InputTextWithHint("##NewTagName", "Tag name", &g_newTag.name, ImGuiInputTextFlags_EnterReturnsTrue);
			char const* const problem = NewNameProblem(registry, g_newTag.name);
			if (problem && !g_newTag.name.empty()) { ImGui::TextColored(EditorTheme::GetPalette().warning, "%s", problem); }
			else if (std::string const twin = CaseTwin(tags, g_newTag.name); !problem && !twin.empty()) {
				ImGui::TextDisabled("Note: '%s' already exists - tags are case-sensitive.", twin.c_str());
			}
			ImGui::Checkbox("Give it to this entity", &g_newTag.assign);

			ImGui::BeginDisabled(problem != nullptr);
			bool const create = ImGui::Button("Create", ImVec2(120.0f, 0.0f)) || (enter && problem == nullptr);
			ImGui::EndDisabled();
			ImGui::SameLine();
			bool const cancel = ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape);
			if (create && problem == nullptr) {
				pick.create = true;
				pick.assign = g_newTag.assign;
				pick.tag = g_newTag.name;
				ImGui::CloseCurrentPopup();
			}
			else if (cancel) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		return pick;
	}

	bool TagEditor::CreateTag(std::string const& tag, std::string* error)
	{
		TagRegistry& registry = TagRegistry::Get();
		TagError const result = registry.Add(tag);
		if (result != TagError::Ok) {
			if (error) { *error = ToString(result); }
			return false;
		}
		registry.SaveProjectFile();   // logs a failure; the tag still works for this session
		int const index = registry.IndexOf(tag);
		EditorManager::GetInstance().GetHistory().Push(std::make_unique<TagListCommand>(
			TagListCommand::Kind::Add, tag, std::string{}, static_cast<std::size_t>(std::max(index, 1)), "Create Tag '" + tag + "'"));
		return true;
	}

	// =======================================================================
	//  Tag Manager
	// =======================================================================

	void TagEditor::OpenManager()
	{
		g_manager.open = true;
		g_manager.focus = true;
	}

	void TagEditor::DrawManager(World& world, SceneManager& scenes)
	{
		if (!g_manager.open) { return; }
		ImGui::SetNextWindowSize(ImVec2(500.0f, 540.0f), ImGuiCond_FirstUseEver);
		if (g_manager.focus) {
			ImGui::SetNextWindowFocus();
			g_manager.focus = false;
		}
		if (!ImGui::Begin("Tag Manager", &g_manager.open)) {
			ImGui::End();
			return;
		}

		TagRegistry& registry = TagRegistry::Get();
		std::vector<std::string> const tags = registry.Tags();
		TagUsers const users = EntitiesByTag(world);
		bool const playing = InPlay();
		ImVec4 const warning = EditorTheme::GetPalette().warning;

		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextDisabled("The project's tags, shared by every scene and prefab (Assets/Config/Tags.json). "
		                    "Case-sensitive. Pick an entity's tag in the Inspector, under its name.");
		ImGui::PopTextWrapPos();
		if (ImGui::Button("Reload from file")) {
			std::string error;
			g_manager.status = registry.LoadProjectFile(&error) ? "Reloaded Tags.json" : "Tags.json could not be read: " + error;
			g_manager.statusDetails.clear();
		}
		ImGui::SetItemTooltip("Reads Tags.json again (e.g. after a git pull). Entities are not changed.");

		// ---- create ----
		ImGui::SeparatorText("New tag");
		ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Add").x - ImGui::GetStyle().FramePadding.x * 2.0f - ImGui::GetStyle().ItemSpacing.x);
		bool const enter = ImGui::InputTextWithHint("##AddTag", "Tag name", &g_manager.addName, ImGuiInputTextFlags_EnterReturnsTrue);
		char const* const problem = NewNameProblem(registry, g_manager.addName);
		ImGui::SameLine();
		ImGui::BeginDisabled(problem != nullptr);
		bool const add = ImGui::Button("Add") || (enter && problem == nullptr);
		ImGui::EndDisabled();
		if (problem && !g_manager.addName.empty()) { ImGui::TextColored(warning, "%s", problem); }
		else if (std::string const twin = CaseTwin(tags, g_manager.addName); !problem && !twin.empty()) {
			ImGui::TextDisabled("Note: '%s' already exists - tags are case-sensitive.", twin.c_str());
		}
		if (add && problem == nullptr) {
			CommitInspectorEdit();
			std::string error;
			if (CreateTag(g_manager.addName, &error)) {
				g_manager.status = "Created '" + g_manager.addName + "'";
				g_manager.addName.clear();
			}
			else {
				g_manager.status = "Could not create '" + g_manager.addName + "': " + error;
			}
			g_manager.statusDetails.clear();
		}

		// ---- the list ----
		ImGui::SeparatorText("Tags");
		ImGuiTableFlags const flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit;
		if (ImGui::BeginTable("##ProjectTags", 3, flags)) {
			ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("In this scene");
			ImGui::TableSetupColumn("##Actions");
			ImGui::TableHeadersRow();
			for (std::size_t i = 0; i < tags.size(); ++i) {
				std::string const& tag = tags[i];
				std::size_t const count = CountOf(users, tag);
				ImGui::PushID(static_cast<int>(i));
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(tag.c_str());

				ImGui::TableNextColumn();
				ImGui::BeginDisabled(count == 0);
				if (ImGui::SmallButton((std::to_string(count) + "##Select").c_str())) { SelectEntities(users.find(tag)->second); }
				ImGui::EndDisabled();
				if (count > 0) { ImGui::SetItemTooltip("Select the %zu entities with this tag", count); }

				ImGui::TableNextColumn();
				if (tag == kUntaggedTag) {
					ImGui::TextDisabled("Protected");
					ImGui::SetItemTooltip("The default tag: it can't be renamed or deleted.");
				}
				else {
					ImGui::BeginDisabled(playing);
					if (ImGui::SmallButton("Rename")) {
						g_manager.renameFrom = tag;
						g_manager.renameTo = tag;
						g_manager.renameReport = ScanProject(world, scenes, tag);
						g_manager.failure.clear();
						g_manager.renameRequested = true;
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("Delete")) {
						TagReferences::Report report = ScanProject(world, scenes, tag);
						if (report.Complete() && !report.InUse() && report.scripts.empty()) {
							DeleteUnusedTag(tag);   // used nowhere: nothing to confirm (Ctrl+Z brings it back)
						}
						else {
							g_manager.deleteTag = tag;
							g_manager.deleteReport = std::move(report);
							g_manager.deleteChoice = static_cast<int>(DeleteChoice::Replace);
							g_manager.failure.clear();
							g_manager.deleteRequested = true;
						}
					}
					ImGui::EndDisabled();
					if (playing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
						ImGui::SetTooltip("Stop first: Stop restores the scene and would undo the change in it.");
					}
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		// ---- tags used here that the project doesn't define ----
		bool headed = false;
		for (auto const& [tag, entities] : users) {
			if (registry.IsDefined(tag)) { continue; }
			if (!headed) {
				ImGui::SeparatorText("Undefined tags in this scene");
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextDisabled("Entities keep a tag that was deleted or renamed elsewhere. Add it back, or give them another tag.");
				ImGui::PopTextWrapPos();
				headed = true;
			}
			ImGui::PushID(tag.c_str());
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(warning, "%s", tag.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("(%zu)", entities.size());
			ImGui::SameLine();
			if (ImGui::SmallButton("Select")) { SelectEntities(entities); }
			ImGui::SameLine();
			char const* const invalid = NewNameProblem(registry, tag);
			ImGui::BeginDisabled(invalid != nullptr);
			if (ImGui::SmallButton("Add to project")) {
				CommitInspectorEdit();
				std::string error;
				g_manager.status = CreateTag(tag, &error) ? "Created '" + tag + "'" : "Could not create '" + tag + "': " + error;
				g_manager.statusDetails.clear();
			}
			ImGui::EndDisabled();
			if (invalid) { ImGui::SetItemTooltip("Not a valid tag name: %s", invalid); }
			ImGui::PopID();
		}

		if (!g_manager.status.empty()) {
			ImGui::Separator();
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextDisabled("%s", g_manager.status.c_str());
			ImGui::PopTextWrapPos();
			for (std::string const& detail : g_manager.statusDetails) { ImGui::BulletText("%s", detail.c_str()); }
		}

		DrawRenameDialog(world, scenes);
		DrawDeleteDialog(world, scenes);
		ImGui::End();
	}
}
