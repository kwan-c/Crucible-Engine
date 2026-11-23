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
#include "AssetPipe/Assetmanager.h"
#include "Core.h"
#include "Diagnostics/Logger.h"
#include "ECS/World.h"
#include "Scene/Prefab.h"
#include "Scene/SceneManager.h"
#include "Undo/UndoHistory.h"
#include "ECS/Components/Components.h"

#include <imgui_internal.h>   // BeginViewportSideBar

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
			std::uint64_t entryId = 0;   // the file entry instances link to
			std::uint64_t sceneId = 0;   // the id other parts' references hold
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
			bool                     saved = false;  // written at least once since opening
			EntityKey                rootKey{};
			std::unordered_map<std::uint64_t, Part> parts;   // by packed undo key
		};
		State g_stage;

		bool g_transitionQueued = false;   // one open or exit waiting for the safe point

		// After an Exit that saved: the other scenes using the prefab, until the box is answered.
		struct OtherScenesOffer {
			std::uint64_t            asset = 0;
			std::string              name;
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
				CRU_USER_ERROR("Could not restore the scene - staying in Prefab Mode");
				return;
			}
			if (currentFile != g_stage.scene.path) {
				if (currentFile.empty()) { scenes.DetachCurrentFile(); }
				else                     { scenes.RelocateCurrentFile(currentFile); }
			}

			bool const saved = g_stage.saved;
			std::uint64_t const asset = g_stage.asset;
			std::string const name = g_stage.name;

			scenes.SetSceneLocked(false);
			g_stage = State{};

			EditorManager::GetInstance().EndWorldSwap(true);

			// The prefab may have been saved here, or changed on disk while the scene was away.
			PrefabAuthoring::UpdateChangedInstances(core.GetWorld());

			if (saved) {
				std::vector<std::string> others = PrefabAuthoring::ScenesUsing(asset);
				if (!others.empty()) { g_offer = OtherScenesOffer{ asset, name, std::move(others), true }; }
			}
		}

		void ApplyOpen(Core& core, std::string const& virtualPath) {
			if (g_stage.active) { return; }
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

			CommitInspectorEdit();   // an edit in progress belongs to the scene

			SceneManager& scenes = core.GetScenes();
			SceneManager::Snapshot snapshot;
			if (!scenes.CaptureSnapshot(snapshot)) {
				CRU_USER_ERROR("Could not open prefab '{}': the scene could not be captured", virtualPath);
				return;
			}

			EditorManager& editor = EditorManager::GetInstance();
			editor.BeginWorldSwap();

			g_stage.active = true;
			g_stage.scene  = std::move(snapshot);
			g_stage.path   = virtualPath;
			g_stage.name   = fs::path(virtualPath).stem().string();
			g_stage.prefabName = prefab.Name();
			g_stage.asset      = assetGuid;
			g_stage.diskText   = text;

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
				g_stage.parts[Pack(History().KeyOf(entries[i]))] = Part{ prefab.IdAt(i), scenes.EnsureId(entries[i]).m_Value };
			}
			g_stage.rootKey = History().KeyOf(root);

			if (auto inspector = editor.GetPanel<Inspector>(ImguiPanels::INSPECTOR)) { inspector->SetSelection({ root }); }
			CRU_USER_INFO("Prefab Mode: {} ({} entit{})", g_stage.name, entries.size(), entries.size() == 1 ? "y" : "ies");
		}
	}

	bool IsActive() { return g_stage.active; }

	void RequestOpen(std::string const& virtualPath) {
		Core* core = EditorManager::GetInstance().GetCore();
		if (core == nullptr) { return; }
		if (core->IsInPlaySession() || core->HasPendingPlayRequest()) {
			CRU_USER_WARN("Stop Play Mode to open a prefab");
			return;
		}
		if (g_stage.active) {
			if (g_stage.path != virtualPath) { CRU_USER_WARN("Exit Prefab Mode before opening another prefab"); }
			return;
		}
		if (g_transitionQueued) { return; }

		g_transitionQueued = true;
		core->GetScenes().RunAtSafePoint([core, virtualPath] {
			g_transitionQueued = false;
			ApplyOpen(*core, virtualPath);
		});
	}

	void RequestExit() {
		Core* core = EditorManager::GetInstance().GetCore();
		if (core == nullptr || !g_stage.active || g_transitionQueued) { return; }

		g_transitionQueued = true;
		core->GetScenes().RunAtSafePoint([core] {
			g_transitionQueued = false;
			ApplyExit(*core);
		});
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
		if (outside > 0) {
			CRU_USER_WARN("{} object{} outside the root of '{}' {} not saved: drag {} under the root to include {}", outside,
			              outside == 1 ? "" : "s", g_stage.name, outside == 1 ? "was" : "were",
			              outside == 1 ? "it" : "them", outside == 1 ? "it" : "them");
		}

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
			g_stage.saved = true;
		}
		else {
			fs::remove(temp, ec);
		}

		// Entities added since now have entries: the next save must keep those ids.
		for (auto const& [entity, id] : ids) {
			Part& part = g_stage.parts[Pack(History().KeyOf(entity))];
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
			if (g_offer.asset == 0) { return; }
			if (g_offer.openPopup) {
				ImGui::OpenPopup(kOtherScenesPopup);
				g_offer.openPopup = false;
			}
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			if (!ImGui::BeginPopupModal(kOtherScenesPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) { return; }

			ImGui::Text("%s was saved. These scenes also use it:", g_offer.name.c_str());
			for (std::string const& scene : g_offer.scenes) { ImGui::BulletText("%s", scene.c_str()); }
			ImGui::Spacing();
			ImGui::TextDisabled("Updating rewrites those files now, keeping their overrides (not undoable; git has the old versions).");
			ImGui::TextDisabled("Otherwise each one updates the next time it is opened.");
			ImGui::Spacing();

			bool close = false;
			if (ImGui::Button("Update Scenes", ImVec2(140.0f, 0.0f))) {
				std::size_t const updated = PrefabAuthoring::UpdateOtherScenes(g_offer.asset);
				CRU_USER_INFO("{}: {} other scene{} updated", g_offer.name, updated, updated == 1 ? "" : "s");
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

	void DrawBar() {
		DrawOtherScenesOffer();
		if (!g_stage.active) { return; }

		Core* core = EditorManager::GetInstance().GetCore();
		bool const dirty = core != nullptr && core->GetScenes().IsDirty();

		float const height = ImGui::GetFrameHeight() + 8.0f;
		ImGuiWindowFlags const flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
			| ImGuiWindowFlags_NoSavedSettings;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.14f, 0.24f, 0.36f, 1.0f));
		if (ImGui::BeginViewportSideBar("##PrefabStageBar", ImGui::GetMainViewport(), ImGuiDir_Up, height, flags)) {
			if (ImGui::Button("< Exit")) { RequestExit(); }
			ImGui::SetItemTooltip("Back to the scene");
			ImGui::SameLine();
			ImGui::BeginDisabled(!dirty);
			if (ImGui::Button("Save")) { Save(); }
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Save the prefab (Ctrl+S)");
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Prefab Mode: %s%s", g_stage.name.c_str(), dirty ? " *" : "");
			ImGui::SetItemTooltip("%s", g_stage.path.c_str());
			if (dirty) {
				ImGui::SameLine();
				ImGui::TextDisabled("Unsaved changes are discarded on Exit.");
			}
		}
		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
	}
}
