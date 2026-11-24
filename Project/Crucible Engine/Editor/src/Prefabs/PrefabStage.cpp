/*!
* @file			PrefabStage.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			24 November 2026
*
* @brief		Prefab Mode: opening a .cprefab in place of the scene, and coming back
*				(see PrefabStage.h).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "Prefabs/PrefabStage.h"

#include "Prefabs/PrefabAuthoring.h"
#include "ImGui/ImGuiManager.h"
#include "ImGui/Panels/ImGuiPanel_Inspector.h"
#include "ImGui/Panels/ImGuiPanel_SceneView.h"
#include "Graphics/DebugDraw.h"
#include "Graphics/RenderExtractSystem.h"
#include "AssetPipe/Assetmanager.h"
#include "Core.h"
#include "Diagnostics/Logger.h"
#include "ECS/World.h"
#include "Scene/Prefab.h"
#include "Scene/SceneManager.h"
#include "Undo/UndoHistory.h"
#include "ECS/Components/Components.h"

#include <imgui_internal.h>   // BeginViewportSideBar

#include <algorithm>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace Crucible::PrefabStage {

	namespace {

		struct Part {
			EntityKey     key{};         // its undo key (what the parts map is indexed by)
			std::uint64_t entryId = 0;   // the file entry instances link to
			std::uint64_t sceneId = 0;   // the id other parts' references hold
			std::uint64_t guid = 0;      // while another level is open: how to find it again
		};

		struct State {
			bool                     active = false;
			SceneManager::Snapshot   scene;          // the scene to come back to
			std::string              path;           // the prefab's virtual path
			std::string              name;
			std::string              prefabName;     // the name stored in the file
			std::uint64_t            asset = 0;      // its guid: the path can change while open
			std::string              diskText;       // the file as last read or written here
			bool                     overwriteArmed = false;   // warned that the file changed on disk
			EntityKey                rootKey{};
			std::uint64_t            rootGuid = 0;   // while another level is open
			std::unordered_map<std::uint64_t, Part> parts;   // by packed undo key
			std::size_t              outsideWarned = 0;   // objects outside the root last warned about
			bool                     framePending = false;   // frame the prefab once it has been placed
			bool                     hasCamera = false;
			SceneView::CameraView    camera;         // the Scene view's, to come back to
			bool                     gridBefore = false;   // the Grid gizmo's state, to come back to
		};
		State g_stage;                  // the level being edited
		std::deque<State> g_parents;    // the levels it was opened from, outermost first (never relocated: snapshots are big)

		// Prefab files changed since the scene was left (saves and nested Applies).
		std::vector<std::uint64_t> g_changed;

		// How far an exit in progress is going: the level to end on (0 = the scene); -1 = none.
		int g_exitTarget = -1;
		int Level() { return g_stage.active ? static_cast<int>(g_parents.size()) + 1 : 0; }

		bool g_transitionQueued = false;   // one open or exit waiting for the safe point

		// The Save / Discard / Cancel box.
		bool g_exitPrompt = false;
		bool g_exitPromptOpen = false;
		bool g_exitCancelled = false;
		char const* const kExitPopup = "Unsaved Prefab Changes";

		// Kept between prefabs for this editor session.
		bool   g_autoSave = false;
		bool   g_previewLight = true;
		bool   g_showGround = true;
		double g_dirtySince = -1.0;   // when the current unsaved edits were first seen
		constexpr double kAutoSaveDelay = 1.0;   // seconds

		void ApplyPreview() {
			RenderExtractSystem::SetPreviewSun(g_previewLight);
			DebugDraw::SetCategoryEnabled(DebugCategory::Grid, g_showGround);
		}

		// After an Exit that saved: the other scenes using the prefab, until the box is answered.
		struct OtherScenesOffer {
			std::vector<std::uint64_t> assets;
			std::string              names;   // the changed prefabs, "House, Window"
			std::vector<std::string> scenes;
			bool                     openPopup = false;
		};
		OtherScenesOffer g_offer;
		char const* const kOtherScenesPopup = "Update Other Scenes?";

		std::uint64_t Pack(Entity e) { return (static_cast<std::uint64_t>(e.index) << 32) | e.generation; }

		UndoHistory& History() { return EditorManager::GetInstance().GetHistory(); }

		bool ReadFile(fs::path const& file, std::string& out) {
			std::ifstream in(file, std::ios::binary);
			if (!in) { return false; }
			out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			return true;
		}

		// The id references to this entity use. A part restored by undo has a new scene id, but
		// the references saved in its siblings still hold the one it was opened with.
		std::uint64_t ReferenceIdOf(SceneManager& scenes, Entity entity) {
			auto const found = g_stage.parts.find(Pack(History().KeyOf(entity)));
			if (found != g_stage.parts.end() && found->second.sceneId != 0) { return found->second.sceneId; }
			return scenes.Current().IdOf(entity).m_Value;
		}

		void CommitInspectorEdit() {
			if (auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR)) {
				inspector->CommitPendingEdit();
			}
		}

		// Back to the scene. On failure the stage stays open, so nothing is lost and Exit can
		// be tried again.
		void ApplyExit(Core& core) {
			if (!g_stage.active) { return; }
			CommitInspectorEdit();

			SceneManager& scenes = core.GetScenes();

			// The Asset Browser may have renamed or deleted the scene's file meanwhile.
			fs::path const currentFile = scenes.CurrentPath();

			if (!scenes.RestoreSnapshot(g_stage.scene)) {
				CRU_USER_ERROR("Could not restore the previous level - staying in Prefab Mode");
				g_exitTarget = -1;
				return;
			}
			// Still locked here, so a detach leaves the restored level's unsaved flag alone; the
			// scene itself is marked unsaved below, once it is the scene again.
			bool const lostSceneFile = currentFile != g_stage.scene.path && currentFile.empty();
			if (currentFile != g_stage.scene.path) {
				if (currentFile.empty()) { scenes.DetachCurrentFile(); }
				else                     { scenes.RelocateCurrentFile(currentFile); }
			}

			RenderExtractSystem::SetPreviewSun(false);
			DebugDraw::SetCategoryEnabled(DebugCategory::Grid, g_stage.gridBefore);
			if (g_stage.hasCamera) {
				if (auto view = EditorManager::GetInstance().GetPanel<SceneView>(ImguiPanels::EDITORSCENE)) {
					view->SetCameraView(g_stage.camera);
				}
			}
			g_exitPrompt = false;
			g_dirtySince = -1.0;

			// Back on the level this one was opened from.
			if (!g_parents.empty()) {
				g_stage = std::move(g_parents.back());
				g_parents.pop_back();
				EditorManager::GetInstance().EndWorldSwap(true);

				// Its entities are new handles now: find each part again by scene guid. A part
				// that is currently deleted keeps its key, which its undo step will rebind.
				std::unordered_map<std::uint64_t, Part> parts;
				for (auto& [key, part] : g_stage.parts) {
					if (part.guid != 0) {
						Entity const live = scenes.Current().EntityOf(xresource::instance_guid{ part.guid });
						if (live.IsValid()) { part.key = History().KeyOf(live); }
					}
					part.guid = 0;
					parts[Pack(part.key)] = part;
				}
				g_stage.parts = std::move(parts);
				if (g_stage.rootGuid != 0) {
					Entity const live = scenes.Current().EntityOf(xresource::instance_guid{ g_stage.rootGuid });
					if (live.IsValid()) { g_stage.rootKey = History().KeyOf(live); }
					g_stage.rootGuid = 0;
				}
				ApplyPreview();

				// Prefabs holding what changed, except the levels still open: those update
				// in memory (below for this one), so their unsaved edits are never overwritten.
				std::vector<std::uint64_t> const open = OpenAssets();
				for (std::uint64_t const changed : std::vector<std::uint64_t>(g_changed)) {
					PrefabAuthoring::UpdatePrefabsContaining(changed, open);
				}
				PrefabAuthoring::UpdateChangedInstances(core.GetWorld());
				return;
			}

			scenes.SetSceneLocked(false);
			if (lostSceneFile) { scenes.MarkDirty(); }   // its next save must ask for a name
			g_stage = State{};

			EditorManager::GetInstance().EndWorldSwap(true);

			// Everything is closed now, so every prefab holding a changed one can follow - also
			// one whose own edits were discarded. Then the scene, which may use any of them.
			std::vector<std::uint64_t> changed = std::move(g_changed);
			g_changed.clear();
			for (std::uint64_t const asset : std::vector<std::uint64_t>(changed)) {
				for (std::uint64_t const rewritten : PrefabAuthoring::UpdatePrefabsContaining(asset, {})) {
					if (std::find(changed.begin(), changed.end(), rewritten) == changed.end()) { changed.push_back(rewritten); }
				}
			}
			PrefabAuthoring::UpdateChangedInstances(core.GetWorld());

			std::vector<std::string> others;
			for (std::uint64_t const asset : changed) {
				for (std::string& scene : PrefabAuthoring::ScenesUsing(asset)) {
					if (std::find(others.begin(), others.end(), scene) == others.end()) { others.push_back(std::move(scene)); }
				}
			}
			if (!others.empty()) {
				std::sort(others.begin(), others.end());
				// Every prefab that changed, not just the one left last: an inner level's save
				// or a rewritten outer prefab may be why these scenes are listed.
				std::string names;
				for (std::uint64_t const asset : changed) {
					std::string const path = PrefabAuthoring::PrefabPathOf(asset);
					if (path.empty()) { continue; }
					names += (names.empty() ? "" : ", ") + fs::path(path).stem().string();
				}
				g_offer = OtherScenesOffer{ std::move(changed), std::move(names), std::move(others), true };
			}
		}

		void ApplyOpen(Core& core, std::string const& virtualPath) {
			if (core.IsInPlaySession() || core.HasPendingPlayRequest()) {
				CRU_USER_WARN("Stop Play Mode to open a prefab");
				return;
			}

			// Everything that can fail cheaply happens before the world is touched.
			AssetEntry const* asset = core.GetAssets().Find(virtualPath);
			if (asset == nullptr || asset->guid.m_Value == 0) {
				CRU_USER_WARN("Could not open '{}': it is not in the asset index yet (refresh the Asset Browser)", virtualPath);
				return;
			}
			std::uint64_t const assetGuid = asset->guid.m_Value;
			std::vector<std::uint64_t> const alreadyOpen = OpenAssets();
			if (std::find(alreadyOpen.begin(), alreadyOpen.end(), assetGuid) != alreadyOpen.end()) {
				if (g_stage.asset != assetGuid) { CRU_USER_WARN("'{}' is already open in Prefab Mode: use the breadcrumb to go back to it", virtualPath); }
				return;
			}

			// A prefab nested in this one (at any depth) may have changed since the file was saved,
			// for example after a git pull. Bring the FILES up to date first, inner ones first, so
			// the prefab opens current and with nothing unsaved. Open levels are left alone.
			for (std::uint64_t const rewritten : PrefabAuthoring::BringNestedUpToDate(assetGuid, alreadyOpen)) {
				NoteChanged(rewritten);   // scenes using it follow when Prefab Mode is left
			}

			fs::path const file = core.GetAssets().Resolve(virtualPath);
			std::string text;
			if (!ReadFile(file, text)) {
				CRU_USER_ERROR("Could not open prefab '{}'", virtualPath);
				return;
			}

			// From text, so it has no asset guid and its copy is NOT a linked instance.
			Prefab prefab;
			if (!prefab.LoadFromString(text)) {
				CRU_USER_ERROR("Could not open prefab '{}': it failed to load (see the log)", virtualPath);
				return;
			}
			prefab.KeepNestedLinks(true);   // prefabs nested in it are instances of their own prefab here

			CommitInspectorEdit();   // an edit in progress belongs to the scene

			SceneManager& scenes = core.GetScenes();
			SceneManager::Snapshot snapshot;
			if (!scenes.CaptureSnapshot(snapshot)) {
				CRU_USER_ERROR("Could not open prefab '{}': the scene could not be captured", virtualPath);
				return;
			}

			EditorManager& editor = EditorManager::GetInstance();

			// From another prefab: put that level aside. The snapshot has just given all its
			// entities guids, which is how its parts are found again afterwards.
			bool const nested = g_stage.active;
			if (nested) {
				World const& current = core.GetWorld();
				for (auto& [key, part] : g_stage.parts) {
					Entity const live = History().Resolve(part.key);
					part.guid = current.IsAlive(live) ? scenes.Current().IdOf(live).m_Value : 0;
				}
				Entity const liveRoot = History().Resolve(g_stage.rootKey);
				g_stage.rootGuid = current.IsAlive(liveRoot) ? scenes.Current().IdOf(liveRoot).m_Value : 0;
				g_parents.push_back(std::move(g_stage));
				g_stage = State{};
			}
			editor.BeginWorldSwap(nested);

			g_stage.active = true;
			g_stage.scene  = std::move(snapshot);
			g_stage.path   = virtualPath;
			g_stage.name   = fs::path(virtualPath).stem().string();
			g_stage.prefabName = prefab.Name();
			g_stage.asset      = assetGuid;
			g_stage.diskText   = text;
			g_stage.gridBefore = DebugDraw::IsCategoryEnabled(DebugCategory::Grid);
			if (auto view = editor.GetPanel<SceneView>(ImguiPanels::EDITORSCENE)) {
				g_stage.hasCamera = view->GetCameraView(g_stage.camera);
			}
			ApplyPreview();

			scenes.SetSceneLocked(true);
			scenes.ClearWorldForStage();

			World& world = core.GetWorld();
			std::vector<Entity> entries;
			Entity root = kNullEntity;
			try {
				root = prefab.Instantiate(world, kNullEntity,
					[&scenes](Entity e) { return scenes.EnsureId(e).m_Value; }, &entries);
			}
			catch (std::exception const& error) {
				CRU_USER_ERROR("Could not open prefab '{}': {}", virtualPath, error.what());
				root = kNullEntity;
			}
			if (!world.IsAlive(root)) {
				ApplyExit(core);   // straight back to the scene
				return;
			}

			for (std::size_t i = 0; i < entries.size(); ++i) {
				EntityKey const key = History().KeyOf(entries[i]);
				g_stage.parts[Pack(key)] = Part{ key, prefab.IdAt(i), scenes.EnsureId(entries[i]).m_Value, 0 };
			}
			g_stage.rootKey = History().KeyOf(root);
			g_stage.framePending = true;   // its WorldTransforms are filled in this frame

			if (auto inspector = editor.GetPanel<Inspector>(ImguiPanels::INSPECTOR)) { inspector->SetSelection({ root }); }
			CRU_USER_INFO("Prefab Mode: {} ({} entit{})", g_stage.name, entries.size(), entries.size() == 1 ? "y" : "ies");

			// Normally nothing left to do (the file was brought up to date above); this catches a
			// nested prefab whose file could not be rewritten, or one belonging to an open level.
			PrefabAuthoring::UpdateChangedInstances(world);
		}
	}

	bool IsActive() { return g_stage.active; }

	std::vector<std::uint64_t> OpenAssets() {
		std::vector<std::uint64_t> open;
		for (State const& level : g_parents) { open.push_back(level.asset); }
		if (g_stage.active) { open.push_back(g_stage.asset); }
		return open;
	}

	bool WouldCreateLoop(std::uint64_t asset) {
		for (std::uint64_t const open : OpenAssets()) {
			if (asset == open || PrefabAuthoring::PrefabContains(asset, open)) { return true; }
		}
		return false;
	}

	void NoteChanged(std::uint64_t asset) {
		if (std::find(g_changed.begin(), g_changed.end(), asset) == g_changed.end()) { g_changed.push_back(asset); }
	}

	void RequestOpen(std::string const& virtualPath) {
		Core* core = EditorManager::GetInstance().GetCore();
		if (core == nullptr) { return; }
		if (core->IsInPlaySession() || core->HasPendingPlayRequest()) {
			CRU_USER_WARN("Stop Play Mode to open a prefab");
			return;
		}
		if (g_stage.active && g_stage.path == virtualPath) { return; }
		if (g_transitionQueued || g_exitPrompt || g_exitTarget >= 0) { return; }

		g_transitionQueued = true;
		core->GetScenes().RunAtSafePoint([core, virtualPath] {
			g_transitionQueued = false;
			ApplyOpen(*core, virtualPath);
		});
	}

	namespace {
		void QueueExit() {
			Core* core = EditorManager::GetInstance().GetCore();
			if (core == nullptr || !g_stage.active || g_transitionQueued) { return; }

			g_transitionQueued = true;
			core->GetScenes().RunAtSafePoint([core] {
				g_transitionQueued = false;
				ApplyExit(*core);
			});
		}

		// Leaves the current level, asking first if it has unsaved edits.
		void ExitStep() {
			Core* core = EditorManager::GetInstance().GetCore();
			if (core == nullptr || !g_stage.active || g_transitionQueued || g_exitPrompt) { return; }

			CommitInspectorEdit();   // an edit still being typed counts as unsaved
			if (core->GetScenes().IsDirty()) {
				g_exitPrompt = true;
				g_exitPromptOpen = true;
				return;
			}
			QueueExit();
		}

		void RequestExitTo(int level) {
			if (!g_stage.active || g_transitionQueued || g_exitPrompt || level >= Level()) { return; }
			g_exitTarget = level;
			ExitStep();
		}
	}

	void RequestExit()    { RequestExitTo(Level() - 1); }
	void RequestExitAll() { RequestExitTo(0); }

	bool ConsumeExitCancelled() {
		return std::exchange(g_exitCancelled, false);
	}

	bool Save() {
		Core* core = EditorManager::GetInstance().GetCore();
		if (core == nullptr || !g_stage.active || g_transitionQueued) { return false; }
		CommitInspectorEdit();

		SceneManager& scenes = core->GetScenes();
		World& world = core->GetWorld();

		// By guid: the Asset Browser may have renamed the file. A deleted one is not recreated.
		std::string const virtualPath = PrefabAuthoring::PrefabPathOf(g_stage.asset);
		if (virtualPath.empty()) {
			CRU_USER_ERROR("Could not save '{}': its file is no longer in the project", g_stage.name);
			return false;
		}
		fs::path const file = core->GetAssets().Resolve(virtualPath);

		Entity const root = Root();
		if (!world.IsAlive(root)) {
			CRU_USER_ERROR("Could not save '{}': its root was deleted (Ctrl+Z brings it back)", g_stage.name);
			return false;
		}

		std::string onDisk;
		if (!ReadFile(file, onDisk)) {
			CRU_USER_ERROR("Could not save '{}': could not read its file", virtualPath);
			return false;
		}
		if (onDisk != g_stage.diskText && !g_stage.overwriteArmed) {
			g_stage.overwriteArmed = true;
			CRU_USER_WARN("'{}' changed on disk since it was opened. Nothing was saved: save again to overwrite that change", virtualPath);
			return false;
		}

		// Only the root's tree is the prefab: say so if something else is in the stage.
		std::size_t outside = 0;
		world.CreateQuery<const Transform>().ForEach([&](Entity e, Transform const&) {
			if (world.GetParent(e) == kNullEntity && !(e == root)) { ++outside; }
		});
		bool const warnOutside = outside > 0 && outside != g_stage.outsideWarned;   // not on every auto-save
		g_stage.outsideWarned = outside;
		if (warnOutside) {
			CRU_USER_WARN("{} object{} outside the root of '{}' {} not saved: drag {} under the root to include {}", outside,
			              outside == 1 ? "" : "s", g_stage.name, outside == 1 ? "was" : "were",
			              outside == 1 ? "it" : "them", outside == 1 ? "it" : "them");
		}

		// What each nested instance overrides goes into the file with it.
		PrefabAuthoring::RefreshStoredOverrides(world);

		fs::path temp = file;
		temp += ".saving";
		std::error_code ec;

		std::vector<std::pair<Entity, std::uint64_t>> ids;
		bool const written = Prefab::SaveKeepingIds(world, root, temp,
			[](Entity e) { return EntryIdOf(e); }, g_stage.prefabName, &ids,
			[&scenes](Entity e) { return ReferenceIdOf(scenes, e); });

		Prefab check;
		std::string text;
		if (!written || !check.Load(temp) || !ReadFile(temp, text)) {
			fs::remove(temp, ec);
			CRU_USER_ERROR("Could not save '{}': the prefab file was left unchanged (see the log)", virtualPath);
			return false;
		}

		if (text != onDisk) {
			PrefabAuthoring::RememberPrefabVersion(g_stage.asset);   // the baseline for instances' overrides
			fs::rename(temp, file, ec);
			if (ec) {
				std::error_code ignored;
				fs::remove(temp, ignored);
				CRU_USER_ERROR("Could not save '{}': {}. The prefab file was left unchanged", virtualPath, ec.message());
				return false;
			}
			PrefabAuthoring::PrefabFileReplaced(g_stage.asset);
			NoteChanged(g_stage.asset);
		}
		else {
			fs::remove(temp, ec);
		}

		// Entities added since now have entries: the next save must keep those ids.
		for (auto const& [entity, id] : ids) {
			EntityKey const key = History().KeyOf(entity);
			Part& part = g_stage.parts[Pack(key)];
			part.key = key;
			if (part.sceneId == 0) { part.sceneId = scenes.Current().IdOf(entity).m_Value; }
			part.entryId = id;
		}
		g_stage.diskText = std::move(text);
		g_stage.overwriteArmed = false;
		scenes.MarkClean();

		CRU_USER_INFO("Saved prefab {} ({} entit{})", virtualPath, ids.size(), ids.size() == 1 ? "y" : "ies");
		return true;
	}

	std::string PlayBlockReason() {
		return g_stage.active ? "exit Prefab Mode first" : std::string{};
	}

	std::string const& OpenPath() { return g_stage.path; }

	Entity Root() {
		Core* core = EditorManager::GetInstance().GetCore();
		if (core == nullptr || !g_stage.active) { return kNullEntity; }
		Entity const root = History().Resolve(g_stage.rootKey);
		return core->GetWorld().IsAlive(root) ? root : kNullEntity;
	}

	std::uint64_t EntryIdOf(Entity entity) {
		if (!g_stage.active) { return 0; }
		auto const found = g_stage.parts.find(Pack(History().KeyOf(entity)));
		return found != g_stage.parts.end() ? found->second.entryId : 0;
	}

	namespace {
		void DrawOtherScenesOffer() {
			if (g_offer.assets.empty()) { return; }
			if (g_offer.openPopup) {
				ImGui::OpenPopup(kOtherScenesPopup);
				g_offer.openPopup = false;
			}
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			if (!ImGui::BeginPopupModal(kOtherScenesPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) { return; }

			ImGui::TextWrapped("Prefabs changed in Prefab Mode: %s", g_offer.names.c_str());
			ImGui::Text("These scenes use %s:", g_offer.assets.size() == 1 ? "it" : "them");
			for (std::string const& scene : g_offer.scenes) { ImGui::BulletText("%s", scene.c_str()); }
			ImGui::Spacing();
			ImGui::TextDisabled("Updating rewrites those files now, keeping their overrides (not undoable; git has the old versions).");
			ImGui::TextDisabled("Otherwise each one updates the next time it is opened.");
			ImGui::Spacing();

			bool close = false;
			if (ImGui::Button("Update Scenes", ImVec2(140.0f, 0.0f))) {
				std::size_t updated = 0;
				for (std::uint64_t const asset : g_offer.assets) { updated += PrefabAuthoring::UpdateOtherScenes(asset); }
				CRU_USER_INFO("{}: {} other scene{} updated", g_offer.names, updated, updated == 1 ? "" : "s");
				close = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Not Now", ImVec2(140.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { close = true; }
			if (close) {
				ImGui::CloseCurrentPopup();
				g_offer = OtherScenesOffer{};
			}
			ImGui::EndPopup();
		}
	}

	namespace {
		void DrawExitPrompt() {
			if (!g_exitPrompt) { return; }
			if (g_exitPromptOpen) {
				ImGui::OpenPopup(kExitPopup);
				g_exitPromptOpen = false;
			}
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			if (!ImGui::BeginPopupModal(kExitPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) { return; }

			ImGui::Text("Save your changes to %s?", g_stage.name.c_str());
			if (g_stage.overwriteArmed) {
				ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "The file changed on disk. Saving again overwrites that change.");
			}
			ImGui::Spacing();

			bool close = false;
			if (ImGui::Button("Save", ImVec2(110.0f, 0.0f)) && Save()) {   // a failed save keeps the box open
				close = true;
				QueueExit();
			}
			ImGui::SameLine();
			if (ImGui::Button("Discard", ImVec2(110.0f, 0.0f))) {
				close = true;
				QueueExit();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				close = true;
				g_exitCancelled = true;
				g_exitTarget = -1;   // stays on this level, however far the exit was going
			}
			if (close) {
				ImGui::CloseCurrentPopup();
				g_exitPrompt = false;
			}
			ImGui::EndPopup();
		}

		// Frames the whole prefab, from a fixed three-quarter angle, once it has been placed.
		void FrameIfPending(Core& core) {
			if (!g_stage.framePending) { return; }
			World& world = core.GetWorld();
			Entity const root = Root();
			WorldTransform const* placed = world.IsAlive(root) ? world.Read<WorldTransform>(root) : nullptr;
			if (world.IsAlive(root) && (placed == nullptr || !placed->valid)) { return; }   // next frame
			g_stage.framePending = false;
			if (!world.IsAlive(root)) { return; }

			auto view = EditorManager::GetInstance().GetPanel<SceneView>(ImguiPanels::EDITORSCENE);
			SceneView::CameraView camera;
			if (!view || !view->GetCameraView(camera)) { return; }
			camera.yaw   = -125.0f;
			camera.pitch = -25.0f;
			view->SetCameraView(camera);

			std::vector<Entity> tree;
			auto visit = [&](auto&& self, Entity e) -> void {
				tree.push_back(e);
				world.ForEachChild(e, [&](Entity child) { self(self, child); });
			};
			visit(visit, root);
			view->FrameEntities(tree);
		}

		// Saves about a second after an edit, never in the middle of a drag or while typing. A
		// failed save (or a file changed on disk) turns it off rather than retrying or overwriting.
		void AutoSaveIfDue(bool dirty) {
			if (!dirty) { g_dirtySince = -1.0; return; }
			double const now = ImGui::GetTime();
			if (g_dirtySince < 0.0) { g_dirtySince = now; }
			if (!g_autoSave || now - g_dirtySince < kAutoSaveDelay || g_exitPrompt || g_transitionQueued) { return; }
			if (ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left)
				|| ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) { return; }
			if (Root() == kNullEntity) { return; }   // root deleted: wait for the undo
			if (!Save()) {
				g_autoSave = false;
				CRU_USER_WARN("Auto Save was turned off because the prefab could not be saved");
			}
			g_dirtySince = -1.0;
		}
	}

	void DrawBar() {
		DrawOtherScenesOffer();
		DrawExitPrompt();
		if (!g_stage.active) {
			g_exitTarget = -1;
			return;
		}

		// An exit going through several levels: the next one, once this one has been answered.
		if (g_exitTarget >= 0 && !g_transitionQueued && !g_exitPrompt) {
			if (Level() > g_exitTarget) { ExitStep(); }
			else                        { g_exitTarget = -1; }
		}

		Core* core = EditorManager::GetInstance().GetCore();
		if (core != nullptr) { FrameIfPending(*core); }
		AutoSaveIfDue(core != nullptr && core->GetScenes().IsDirty());
		bool const dirty = core != nullptr && core->GetScenes().IsDirty();
		std::string const sceneName = g_stage.scene.path.empty() ? std::string("Untitled") : g_stage.scene.path.stem().string();

		float const height = ImGui::GetFrameHeight() + 8.0f;
		ImGuiWindowFlags const flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
			| ImGuiWindowFlags_NoSavedSettings;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.14f, 0.24f, 0.36f, 1.0f));
		if (ImGui::BeginViewportSideBar("##PrefabStageBar", ImGui::GetMainViewport(), ImGuiDir_Up, height, flags)) {
			if (ImGui::Button("<")) { RequestExit(); }
			ImGui::SetItemTooltip(g_parents.empty() ? "Back to the scene" : "Back to %s", g_parents.empty() ? "" : g_parents.back().name.c_str());
			ImGui::SameLine();

			// Breadcrumb: Scene > House > Window. Each earlier level is a button back to it.
			if (ImGui::Button((sceneName + "##PrefabCrumbScene").c_str())) { RequestExitAll(); }
			ImGui::SetItemTooltip("Back to the scene");
			ImGui::SameLine();
			for (std::size_t i = 0; i < g_parents.size(); ++i) {
				ImGui::AlignTextToFramePadding();
				ImGui::TextDisabled(">");
				ImGui::SameLine();
				ImGui::PushID(static_cast<int>(i));
				if (ImGui::Button(g_parents[i].name.c_str())) { RequestExitTo(static_cast<int>(i) + 1); }
				ImGui::SetItemTooltip("%s", g_parents[i].path.c_str());
				ImGui::PopID();
				ImGui::SameLine();
			}
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled(">");
			ImGui::SameLine();
			ImGui::Text("%s%s", g_stage.name.c_str(), dirty ? " *" : "");
			ImGui::SetItemTooltip("%s", g_stage.path.c_str());

			ImGui::SameLine(0.0f, 24.0f);
			ImGui::BeginDisabled(!dirty);
			if (ImGui::Button("Save")) { Save(); }
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Save the prefab (Ctrl+S)");
			ImGui::SameLine();
			ImGui::Checkbox("Auto Save", &g_autoSave);
			ImGui::SetItemTooltip("Save about a second after each change");

			ImGui::SameLine(0.0f, 24.0f);
			if (ImGui::Checkbox("Preview Light", &g_previewLight)) { ApplyPreview(); }
			ImGui::SetItemTooltip("Add a neutral sun when the prefab has no directional light (not saved)");
			ImGui::SameLine();
			if (ImGui::Checkbox("Ground", &g_showGround)) { ApplyPreview(); }
			ImGui::SetItemTooltip("Show the ground grid at y = 0 (not saved)");
		}
		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
	}
}
