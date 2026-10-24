/*!
* @file			PrefabAuthoring.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			23 October 2026
*
* @brief		Editor-side prefab creation and placement (see PrefabAuthoring.h).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "Prefabs/PrefabAuthoring.h"

#include "ImGui/ImGuiManager.h"
#include "ImGui/EditorWidgets.h"
#include "ImGui/Panels/ImGuiPanel_AssetBrowser.h"
#include "ImGui/Panels/ImGuiPanel_Inspector.h"
#include "AssetPipe/Assetmanager.h"
#include "Core.h"
#include "Diagnostics/Logger.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"
#include "Scene/Prefab.h"
#include "Scene/SceneManager.h"
#include "Undo/EntityCommands.h"
#include "Undo/UndoHistory.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <memory>
#include <system_error>

namespace fs = std::filesystem;

namespace {

	// The entity's name, or "Entity <index>" if it has none (the Hierarchy's row label).
	std::string NameOf(Crucible::World const& world, Crucible::Entity entity) {
		Crucible::EntityName const* name = world.Read<Crucible::EntityName>(entity);
		if (name && !name->name.empty()) { return name->name; }
		return "Entity " + std::to_string(entity.index);
	}

	// An entity name turned into something Windows accepts as a file name.
	std::string SafeFileStem(std::string name) {
		for (char& c : name) {
			unsigned char const u = static_cast<unsigned char>(c);
			if (u < 32 || std::strchr("<>:\"/\\|?*", c) != nullptr) { c = '_'; }
		}
		// Windows silently drops trailing dots and spaces, so "Crate." would be saved as "Crate".
		while (!name.empty() && (name.back() == '.' || name.back() == ' ')) { name.pop_back(); }
		std::size_t const first = name.find_first_not_of(' ');
		name = (first == std::string::npos) ? std::string{} : name.substr(first);
		return name.empty() ? std::string{ "Prefab" } : name;
	}

	std::string JoinVirtual(std::string const& folder, std::string const& file) {
		return folder.empty() ? file : folder + "/" + file;
	}
}

namespace Crucible::PrefabAuthoring {

	// ======================================================================= creating

	bool CanCreate() {
		Core* core = EditorManager::GetInstance().GetCore();
		return core != nullptr && !core->IsInPlaySession() && !core->HasPendingPlayRequest();
	}

	std::vector<Entity> DraggedEntities(World& world, Entity dragged) {
		if (!world.IsAlive(dragged)) { return {}; }
		auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
		if (inspector && inspector->IsSelected(dragged)) { return inspector->GetSelection(); }
		return { dragged };
	}

	std::vector<std::string> CreatePrefabs(World& world, std::vector<Entity> const& entities, std::string const& folder) {
		std::vector<std::string> written;

		if (!CanCreate()) {
			CRU_WARN("[Prefab] Stop Play Mode to create prefabs");
			return written;
		}

		// Top-level only: a selected child is already inside its selected ancestor's prefab.
		std::vector<Entity> roots;
		for (Entity const e : entities) {
			if (!world.IsAlive(e)) { continue; }
			bool const covered = std::any_of(entities.begin(), entities.end(), [&](Entity const other) {
				return !(other == e) && world.IsAlive(other) && world.IsAncestorOf(other, e);
			});
			if (!covered && std::find(roots.begin(), roots.end(), e) == roots.end()) { roots.push_back(e); }
		}
		if (roots.empty()) { return written; }

		AssetManager& assets = EditorManager::GetInstance().GetCore()->GetAssets();
		fs::path dir{ assets.GetAssetRoot() };
		if (!folder.empty()) { dir /= fs::path{ folder }; }

		std::error_code ec;
		fs::create_directories(dir, ec);
		if (ec) {
			CRU_ERROR("[Prefab] Could not create folder '{}': {}", dir.string(), ec.message());
			return written;
		}

		for (Entity const root : roots) {
			std::string const stem = SafeFileStem(NameOf(world, root));

			// Never overwrite: "Crate.cprefab", then "Crate (1).cprefab", ...
			std::string fileName = stem + kPrefabExtension;
			for (int i = 1; fs::exists(dir / fileName, ec) && i < 1000; ++i) {
				fileName = stem + " (" + std::to_string(i) + ")" + kPrefabExtension;
			}

			if (Prefab::SaveFrom(world, root, dir / fileName, stem)) {
				written.push_back(JoinVirtual(folder, fileName));
			}
			else {
				CRU_ERROR("[Prefab] Could not save '{}'", JoinVirtual(folder, fileName));
			}
		}

		if (!written.empty()) {
			// Same pattern as the Animator / Font Importer: the browser's Refresh also rescans the
			// asset index, which writes the new files' .meta.
			if (auto browser = EditorManager::GetInstance().GetPanel<AssetBrowser>(ImguiPanels::ASSETBROWSER)) { browser->Refresh(); }
			else { assets.Refresh(); }
		}
		return written;
	}

	// ======================================================================= placing

	bool IsPrefabFile(std::string const& virtualPath) {
		std::string ext = fs::path(virtualPath).extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return ext == kPrefabExtension;
	}

	bool IsDraggingPrefab() {
		ImGuiPayload const* payload = ImGui::GetDragDropPayload();
		return payload != nullptr && payload->IsDataType(EditorWidgets::kAssetPayload)
			&& payload->Data != nullptr
			&& IsPrefabFile(static_cast<char const*>(payload->Data));
	}

	Entity PlacePrefab(World& world, std::string const& virtualPath, Entity parent, std::optional<Math::vec3> position) {
		Core* core = EditorManager::GetInstance().GetCore();
		if (core == nullptr) { return kNullEntity; }

		if (!IsPrefabFile(virtualPath)) {
			CRU_USER_WARN("'{}' is not a prefab ({} file)", virtualPath, kPrefabExtension);
			return kNullEntity;
		}

		// Loaded fresh each time: the file may have been re-saved since the last placement.
		Prefab prefab;
		if (!prefab.Load(core->GetAssets().Resolve(virtualPath))) {
			CRU_USER_WARN("Could not place '{}': the prefab failed to load (see the log)", virtualPath);
			return kNullEntity;
		}

		// Finish any Inspector edit first, so it lands in the history before this step.
		auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);
		if (inspector) { inspector->CommitPendingEdit(); }

		if (!(parent == kNullEntity) && !world.IsAlive(parent)) { parent = kNullEntity; }

		Entity const root = prefab.Instantiate(world, parent);
		if (!world.IsAlive(root)) { return kNullEntity; }

		if (position) {
			if (Transform* transform = world.Write<Transform>(root)) { transform->positions = *position; }
		}

		if (inspector) { inspector->SetSelection({ root }); }

		// Undo = delete the new subtree, redo = rebuild it: the same "delete run backwards" the
		// Hierarchy uses for Paste and Duplicate. Captured after the position is set, so redo
		// puts it back in the same place.
		std::string const label = "Place " + prefab.Name();
		UndoHistory& history = EditorManager::GetInstance().GetHistory();
		history.Push(std::make_unique<InvertedCommand>(DeleteEntityCommand::Capture(history, root, label), label));

		core->GetScenes().MarkDirty();
		CRU_USER_INFO("{}: {} entit{}", label, prefab.EntityCount(), prefab.EntityCount() == 1 ? "y" : "ies");
		return root;
	}
}
