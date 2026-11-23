/*!
* @file			PrefabAuthoring.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			23 October 2026
*
* @brief		Editor-side prefabs: creating, placing, instance links and overrides (see
*				PrefabAuthoring.h).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "Prefabs/PrefabAuthoring.h"
#include "Prefabs/PrefabStage.h"

#include "ImGui/ImGuiManager.h"
#include "ImGui/EditorWidgets.h"
#include "ImGui/Panels/ImGuiPanel_AssetBrowser.h"
#include "ImGui/Panels/ImGuiPanel_Inspector.h"
#include "AssetPipe/Assetmanager.h"
#include "AssetPipe/AssetRegistry.h"
#include "Core.h"
#include "Diagnostics/Logger.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"
#include "Scene/EntityIO.h"
#include "Scene/EntityRefs.h"
#include "Scene/Prefab.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"
#include "Undo/EntityCommands.h"
#include "Undo/UndoHistory.h"
#include "Reflection/Serialization.h"   // ToJsonValue (comparing with the prefab), rapidjson
#include "Reflection/ReflectionRegistration.h"   // MetaDataTypes
#include "Json/JsonWrapper.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

// windows.h (if anything above pulls it in) defines GetObject as a macro, which would rename
// the JsonWrapper::GetObject calls below.
#ifdef GetObject
#undef GetObject
#endif

namespace fs = std::filesystem;

namespace {

	// Unity-like: instance rows in blue, rows whose .cprefab is gone in red.
	ImVec4 const kPrefabColour  { 0.55f, 0.75f, 1.00f, 1.0f };
	ImVec4 const kMissingColour { 1.00f, 0.45f, 0.45f, 1.0f };

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

	// "Crate" for "Prefabs/Crate.cprefab"; "Prefab" when the path is unknown.
	std::string PrefabNameOf(std::string const& virtualPath) {
		std::string const stem = fs::path(virtualPath).stem().string();
		return stem.empty() ? std::string{ "Prefab" } : stem;
	}

	// The root and every part of the same instance below it, parents first. Stops at entities
	// that aren't parts of this instance: ones added under it, and nested instances' roots.
	std::vector<Crucible::Entity> InstanceMembers(Crucible::World const& world, Crucible::Entity root, std::uint64_t prefab) {
		std::vector<Crucible::Entity> members;
		auto visit = [&](auto&& self, Crucible::Entity entity) -> void {
			members.push_back(entity);
			world.ForEachChild(entity, [&](Crucible::Entity child) {
				Crucible::PrefabInstance const* link = world.Read<Crucible::PrefabInstance>(child);
				if (link && !link->isRoot && link->prefabAsset == prefab) { self(self, child); }
			});
		};
		visit(visit, root);
		return members;
	}

	// Finish any Inspector edit first, so it lands in the history before the next step.
	void CommitInspectorEdit() {
		if (auto inspector = Crucible::EditorManager::GetInstance().GetPanel<Crucible::Inspector>(ImguiPanels::INSPECTOR)) {
			inspector->CommitPendingEdit();
		}
	}
}

namespace Crucible::PrefabAuthoring {

	// ======================================================================= creating

	bool CanCreate() {
		Core* core = EditorManager::GetInstance().GetCore();
		// In Prefab Mode the world holds a prefab, not the scene these operations work on.
		return core != nullptr && !core->IsInPlaySession() && !core->HasPendingPlayRequest() && !PrefabStage::IsActive();
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
			CRU_USER_WARN("Stop Play Mode to create prefabs");
			return written;
		}

		// Top-level only: a selected child is already inside its selected ancestor's prefab.
		std::vector<Entity> roots;
		for (Entity const e : entities) {
			if (!world.IsAlive(e)) { continue; }
			bool const covered = std::any_of(entities.begin(), entities.end(), [&](Entity const other) {
				return !(other == e) && world.IsAlive(other) && world.IsAncestorOf(other, e);
			});
			if (covered || std::find(roots.begin(), roots.end(), e) != roots.end()) { continue; }
			// Re-linking a piece of another instance would tear it out of that instance.
			if (IsLockedPart(world, e)) {
				CRU_USER_WARN("Cannot make a prefab from '{}': it is part of a prefab instance - unpack that prefab first", NameOf(world, e));
				continue;
			}
			roots.push_back(e);
		}
		if (roots.empty()) { return written; }

		AssetManager& assets = EditorManager::GetInstance().GetCore()->GetAssets();
		fs::path dir{ assets.GetAssetRoot() };
		if (!folder.empty()) { dir /= fs::path{ folder }; }

		std::error_code ec;
		fs::create_directories(dir, ec);
		if (ec) {
			CRU_USER_ERROR("Could not create folder '{}' for the prefab: {}", dir.string(), ec.message());
			return written;
		}

		// What each saved file needs linking back to: its root, and every entity with its entry id.
		struct Saved {
			Entity                                    root;
			std::string                               virtualPath;
			std::vector<std::pair<Entity, std::uint64_t>> ids;
		};
		std::vector<Saved> saved;

		// Entity references are saved as the prefab entry they point at (see Scene/EntityRefs.h).
		SceneManager& scenes = EditorManager::GetInstance().GetCore()->GetScenes();
		auto const sceneIdOf = [&scenes](Entity e) { return scenes.Current().IdOf(e).m_Value; };

		for (Entity const root : roots) {
			std::string const stem = SafeFileStem(NameOf(world, root));

			// Never overwrite: "Crate.cprefab", then "Crate (1).cprefab", ...
			std::string fileName = stem + kPrefabExtension;
			for (int i = 1; fs::exists(dir / fileName, ec) && i < 1000; ++i) {
				fileName = stem + " (" + std::to_string(i) + ")" + kPrefabExtension;
			}

			Saved entry{ root, JoinVirtual(folder, fileName), {} };
			if (Prefab::SaveFrom(world, root, dir / fileName, stem, &entry.ids, sceneIdOf)) {
				// Shown in the Console (CRU_USER_*: front-facing, unlike the engine's own log line).
				CRU_USER_INFO("Created prefab {} from '{}' ({} entit{})", entry.virtualPath, NameOf(world, root),
				              entry.ids.size(), entry.ids.size() == 1 ? "y" : "ies");
				written.push_back(entry.virtualPath);
				saved.push_back(std::move(entry));
			}
			else {
				CRU_USER_ERROR("Could not save prefab '{}' (see the Engine log)", JoinVirtual(folder, fileName));
			}
		}
		if (written.empty()) { return written; }

		// Same pattern as the Animator / Font Importer: the browser's Refresh also rescans the
		// asset index, which writes the new files' .meta - and so gives them the guid the
		// links below need.
		if (auto browser = EditorManager::GetInstance().GetPanel<AssetBrowser>(ImguiPanels::ASSETBROWSER)) { browser->Refresh(); }
		else { assets.Refresh(); }

		// Like Unity, the entities a prefab was made from become its first instance. Recorded
		// as one undo step: Ctrl+Z takes the links off again (the files stay).
		CommitInspectorEdit();
		UndoHistory& history = EditorManager::GetInstance().GetHistory();
		std::string const label = written.size() == 1 ? "Create Prefab " + PrefabNameOf(written.front())
		                                               : "Create " + std::to_string(written.size()) + " Prefabs";
		std::vector<std::unique_ptr<EditorCommand>> parts;
		for (Saved const& file : saved) {
			AssetEntry const* asset = assets.Find(file.virtualPath);
			if (asset == nullptr || asset->guid.m_Value == 0) {
				CRU_WARN("[Prefab] '{}' has no guid yet - its source entities were left unlinked", file.virtualPath);
				continue;
			}
			std::uint64_t const hash = AssetRegistry::HashFileContents(assets.Resolve(file.virtualPath));
			for (auto const& [entity, id] : file.ids) {
				if (!world.IsAlive(entity)) { continue; }
				std::string before = EntityState::Capture(world, entity);
				PrefabInstance link{ asset->guid.m_Value, id, entity == file.root };
				if (link.isRoot) { link.prefabHash = hash; }   // the version of the file it matches
				world.AddComponent<PrefabInstance>(entity, std::move(link));
				parts.push_back(std::make_unique<EntityStateCommand>(history.KeyOf(entity), std::move(before),
				                                                     EntityState::Capture(world, entity), label));
			}
		}
		if (!parts.empty()) {
			history.Push(GroupCommand::Make(std::move(parts), label));
			EditorManager::GetInstance().GetCore()->GetScenes().MarkDirty();
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

		// Nested prefabs aren't supported yet: saving would flatten this into plain entities.
		if (PrefabStage::IsActive()) {
			CRU_USER_WARN("Cannot place a prefab inside Prefab Mode yet");
			return kNullEntity;
		}

		if (!IsPrefabFile(virtualPath)) {
			CRU_USER_WARN("'{}' is not a prefab ({} file)", virtualPath, kPrefabExtension);
			return kNullEntity;
		}

		// Loaded by GUID through the ResourceManager, so the copy is linked to the prefab
		// (PrefabInstance). Reloaded first: the file may have been re-saved since the last placement.
		ResourceManager& resources = core->GetResources();
		AssetId const prefabId = resources.GetIdForPath(virtualPath);
		if (prefabId == 0) {
			CRU_USER_WARN("Could not place '{}': it is not in the asset index yet (refresh the Asset Browser)", virtualPath);
			return kNullEntity;
		}
		resources.ReloadPrefab(prefabId);
		std::shared_ptr<Prefab const> const loaded = resources.GetPrefab(prefabId);
		if (!loaded) {
			CRU_USER_WARN("Could not place '{}': the prefab failed to load (see the log)", virtualPath);
			return kNullEntity;
		}
		Prefab const& prefab = *loaded;

		CommitInspectorEdit();
		auto inspector = EditorManager::GetInstance().GetPanel<Inspector>(ImguiPanels::INSPECTOR);

		if (!(parent == kNullEntity) && !world.IsAlive(parent)) { parent = kNullEntity; }

		// Parts that reference each other point at the new copy's parts, not the original's.
		Entity const root = prefab.Instantiate(world, parent,
			[core](Entity e) { return core->GetScenes().EnsureId(e).m_Value; });
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

	// ======================================================================= instances

	std::string PrefabPathOf(std::uint64_t prefabAsset) {
		Core* core = EditorManager::GetInstance().GetCore();
		if (core == nullptr || prefabAsset == 0) { return {}; }
		AssetEntry const* asset = core->GetAssets().FindByGuid(xresource::instance_guid{ prefabAsset });
		return asset ? asset->virtualPath : std::string{};
	}

	bool IsInstanceRoot(World const& world, Entity entity) {
		PrefabInstance const* link = world.IsAlive(entity) ? world.Read<PrefabInstance>(entity) : nullptr;
		return link != nullptr && link->isRoot;
	}

	bool IsLockedPart(World const& world, Entity entity) {
		PrefabInstance const* link = world.IsAlive(entity) ? world.Read<PrefabInstance>(entity) : nullptr;
		return link != nullptr && !link->isRoot;
	}

	std::vector<Entity> WithoutLockedParts(World const& world, std::vector<Entity> const& entities,
	                                       char const* verb, std::optional<Entity> newParent) {
		std::vector<Entity> kept;
		std::string firstBlocked;
		std::size_t blocked = 0;
		for (Entity const e : entities) {
			bool const staysPut = newParent && world.GetParent(e) == *newParent;   // a reorder among its siblings
			if (IsLockedPart(world, e) && !staysPut) {
				if (blocked++ == 0) { firstBlocked = NameOf(world, e); }
				continue;
			}
			kept.push_back(e);
		}
		if (blocked > 0) {
			std::string const more = blocked > 1 ? " (and " + std::to_string(blocked - 1) + " more)" : std::string{};
			CRU_USER_WARN("Cannot {} '{}'{}: it is part of a prefab instance - right-click the instance's root > Unpack Prefab first",
			              verb, firstBlocked, more);
		}
		return kept;
	}

	void DetachCopiedPart(World& world, Entity copiedTop) {
		PrefabInstance const* link = world.IsAlive(copiedTop) ? world.Read<PrefabInstance>(copiedTop) : nullptr;
		if (link == nullptr || link->isRoot) { return; }   // not a piece of an instance: nothing to do
		for (Entity const e : InstanceMembers(world, copiedTop, link->prefabAsset)) {
			world.RemoveComponent<PrefabInstance>(e);
		}
	}

	bool Unpack(World& world, Entity root) {
		if (!IsInstanceRoot(world, root)) { return false; }
		std::uint64_t const prefab = world.Read<PrefabInstance>(root)->prefabAsset;

		CommitInspectorEdit();
		std::vector<Entity> const members = InstanceMembers(world, root, prefab);

		// One EntityStateCommand per entity, grouped: Ctrl+Z puts every link back.
		std::string const label = "Unpack " + PrefabNameOf(PrefabPathOf(prefab));
		UndoHistory& history = EditorManager::GetInstance().GetHistory();
		std::vector<std::unique_ptr<EditorCommand>> parts;
		for (Entity const e : members) {
			std::string before = EntityState::Capture(world, e);
			world.RemoveComponent<PrefabInstance>(e);
			parts.push_back(std::make_unique<EntityStateCommand>(history.KeyOf(e), std::move(before),
			                                                     EntityState::Capture(world, e), label));
		}
		history.Push(GroupCommand::Make(std::move(parts), label));

		if (Core* core = EditorManager::GetInstance().GetCore()) { core->GetScenes().MarkDirty(); }
		CRU_USER_INFO("{}: {} entit{} unlinked", label, members.size(), members.size() == 1 ? "y" : "ies");
		return true;
	}

	bool RowColour(World const& world, Entity entity, ImVec4& colour) {
		PrefabInstance const* link = world.IsAlive(entity) ? world.Read<PrefabInstance>(entity) : nullptr;
		if (link == nullptr) { return false; }
		colour = PrefabPathOf(link->prefabAsset).empty() ? kMissingColour : kPrefabColour;
		return true;
	}

	// ======================================================================= overrides

	namespace {

		// What an instance entity is compared against: its prefab (cached, reloaded when the file
		// changes on disk) and where its entry's components are in that file.
		struct CachedPrefab {
			std::shared_ptr<Prefab const> prefab;
			fs::file_time_type            stamp{};
			bool                          loaded = false;
		};
		std::unordered_map<std::uint64_t, CachedPrefab> g_prefabs;

		// Every version of each prefab loaded this session, by content hash (guid -> hash ->
		// file). An instance remembers the hash it was last updated against; when the file has
		// changed since, its overrides are worked out exactly against that old version if it
		// is still here, instead of relying on the list saved with the scene.
		std::unordered_map<std::uint64_t, std::unordered_map<std::uint64_t, std::shared_ptr<Prefab const>>> g_versions;

		Prefab const* PrefabVersion(std::uint64_t prefabAsset, std::uint64_t hash) {
			auto const byPrefab = g_versions.find(prefabAsset);
			if (byPrefab == g_versions.end()) { return nullptr; }
			auto const found = byPrefab->second.find(hash);
			return found != byPrefab->second.end() ? found->second.get() : nullptr;
		}

		Prefab const* LoadedPrefab(std::uint64_t prefabAsset) {
			Core* core = EditorManager::GetInstance().GetCore();
			std::string const path = PrefabPathOf(prefabAsset);
			if (core == nullptr || path.empty()) { return nullptr; }

			fs::path const file = core->GetAssets().Resolve(path);
			std::error_code ec;
			fs::file_time_type const stamp = fs::last_write_time(file, ec);
			if (ec) { return nullptr; }

			CachedPrefab& cached = g_prefabs[prefabAsset];
			if (!cached.loaded || cached.stamp != stamp) {
				// A file that fails to load is not retried until it changes again.
				// Read fresh through the ResourceManager (by GUID, bypassing its cache): this
				// compares versions of the file, so it must see the file as it is on disk now.
				std::shared_ptr<Prefab> fresh = core->GetResources().ReadPrefab(prefabAsset);
				cached.loaded = fresh != nullptr;
				cached.stamp  = stamp;
				cached.prefab = fresh;
				if (cached.loaded) { g_versions[prefabAsset].emplace(fresh->ContentHash(), fresh); }   // kept for later comparisons
			}
			return cached.loaded ? cached.prefab.get() : nullptr;
		}

		struct EntryRef {
			Prefab const* prefab = nullptr;
			std::string   components;    // "entities/2/components"
			bool          isRoot = false;
		};

		// ---- entity references (Scene/EntityRefs.h) --------------------------------------
		// In a scene, script Entity fields and PhysicsJoint connected bodies hold SCENE ids; in a
		// .cprefab they hold the ENTRY id of the part they point at. Everything that copies
		// components between an instance and its file translates them with these.

		// Whose scene ids: the open scene's, or (while UpdateOtherScenes rewrites another scene
		// file in a scratch world) that file's Scene. Set only by SceneIdsFrom below.
		Scene* g_idScene = nullptr;

		struct SceneIdsFrom {   // RAII: scene ids come from `scene` until this goes away
			explicit SceneIdsFrom(Scene& scene) : m_previous(std::exchange(g_idScene, &scene)) {}
			~SceneIdsFrom() { g_idScene = m_previous; }
			SceneIdsFrom(SceneIdsFrom const&) = delete;
			SceneIdsFrom& operator=(SceneIdsFrom const&) = delete;
			Scene* m_previous;
		};

		// An entity's scene id, or 0 if it has none yet.
		std::uint64_t SceneIdOf(Entity e) {
			if (g_idScene != nullptr) { return g_idScene->IdOf(e).m_Value; }
			Core* core = EditorManager::GetInstance().GetCore();
			return core != nullptr ? core->GetScenes().Current().IdOf(e).m_Value : 0;
		}

		// An entity's scene id, issuing one if it has none (so a reference to it can be stored).
		std::uint64_t EnsureSceneIdOf(Entity e) {
			if (g_idScene != nullptr) { return g_idScene->EnsureId(e).m_Value; }
			Core* core = EditorManager::GetInstance().GetCore();
			return core != nullptr ? core->GetScenes().EnsureId(e).m_Value : 0;
		}

		// The instance root above (or at) a part.
		Entity PartRoot(World const& world, Entity part) {
			for (Entity e = part; world.IsAlive(e); e = world.GetParent(e)) {
				PrefabInstance const* link = world.Read<PrefabInstance>(e);
				if (link == nullptr) { return kNullEntity; }
				if (link->isRoot) { return e; }
			}
			return kNullEntity;
		}

		// entry id (in `prefab`) -> the scene id of the matching part of the instance `part`
		// belongs to; ids that aren't entries of `prefab` come back unchanged. `ensure` issues
		// scene ids (writing into the instance); without it, read-only (for comparing).
		EntityRefs::Map EntryToInstance(World const& world, Entity part, Prefab const& prefab, bool ensure) {
			Entity const root = PartRoot(world, part);
			std::unordered_map<std::uint64_t, Entity> byEntry;   // entry id -> part
			if (world.IsAlive(root)) {
				std::uint64_t const asset = world.Read<PrefabInstance>(root)->prefabAsset;
				for (Entity const m : InstanceMembers(world, root, asset)) { byEntry[world.Read<PrefabInstance>(m)->fileId] = m; }
			}
			return prefab.EntryToSceneMap(
				[&prefab, byEntry](std::size_t index) {
					auto const found = byEntry.find(prefab.IdAt(index));
					return found != byEntry.end() ? found->second : kNullEntity;
				},
				[ensure](Entity e) { return ensure ? EnsureSceneIdOf(e) : SceneIdOf(e); });
		}

		// The entity's entry in `prefab` (a particular version of its file), or false.
		bool FindEntryIn(World const& world, Entity entity, Prefab const* prefab, EntryRef& out) {
			PrefabInstance const* link = world.IsAlive(entity) ? world.Read<PrefabInstance>(entity) : nullptr;
			if (link == nullptr || prefab == nullptr) { return false; }
			int const index = prefab->IndexOf(link->fileId);
			if (index < 0) { return false; }
			out.prefab     = prefab;
			out.components = prefab->ComponentsPath(static_cast<std::size_t>(index));
			out.isRoot     = link->isRoot;
			return true;
		}

		// ...in the file as it is on disk now.
		bool FindEntry(World const& world, Entity entity, EntryRef& out) {
			PrefabInstance const* link = world.IsAlive(entity) ? world.Read<PrefabInstance>(entity) : nullptr;
			return link != nullptr && FindEntryIn(world, entity, LoadedPrefab(link->prefabAsset), out);
		}

		// Values that belong to the instance, not the prefab (Unity's rule): where the root is,
		// and what it is called. Never overrides, never reverted, never applied.
		bool InstanceOwned(bool isRoot, std::string_view component, std::string_view property) {
			if (!isRoot) { return false; }
			if (component == ComponentTraits<Transform>::name) { return property == "positions" || property == "rotation"; }
			if (component == ComponentTraits<EntityName>::name) { return property == "name"; }
			return false;
		}

		// Components that take part: everything a scene file holds, minus the link itself.
		template <typename T>
		inline constexpr bool kCompared = !WorldManaged<T>::value && !std::is_same_v<T, WorldTransform>
			&& !std::is_same_v<T, PrefabInstance> && !RuntimeOnly<T>::value;   // runtime-only: never in a .cprefab

		// One component of one entity against the prefab entry.
		template <typename T>
		void DiffComponent(World& world, Entity entity, EntryRef const& entry, std::vector<Override>& out) {
			if constexpr (kCompared<T>) {
				std::string const name(ComponentTraits<T>::name);
				std::string const path = entry.components + "/" + name;
				JsonWrapper const& doc = entry.prefab->Document();
				bool const inPrefab = doc.Has(path);
				T const* mine = world.Read<T>(entity);

				if (!inPrefab && mine == nullptr) { return; }
				if constexpr (RemovableComponent<T>) {
					if (inPrefab && mine == nullptr) { out.push_back({ Override::Kind::RemovedComponent, entity, name, {} }); return; }
					if (!inPrefab)                   { out.push_back({ Override::Kind::AddedComponent,   entity, name, {} }); return; }
				}
				if (mine == nullptr) { return; }

				// Both sides through the same reflection the files are written with, so a value
				// loaded from the file compares equal to itself. A field the file lacks (an older
				// prefab) compares as its default - exactly what an instance of it would get.
				T theirs{};
				if (inPrefab) { doc.GetObject(path, theirs); }
				if constexpr (std::is_same_v<T, ScriptComponent> || std::is_same_v<T, PhysicsJoint>) {
					// The file names parts by entry id, the instance by scene id: compare like with like.
					if (inPrefab) { EntityRefs::Remap(theirs, EntryToInstance(world, entity, *entry.prefab, false)); }
				}
				rapidjson::Document scratch;
				rapidjson::Value a, b;
				Reflection::ToJsonValue(*mine, a, scratch.GetAllocator());
				Reflection::ToJsonValue(theirs, b, scratch.GetAllocator());
				if (a.IsObject() && b.IsObject()) {
					// MemberBegin, not GetObject(): windows.h defines a GetObject macro.
					for (auto member = a.MemberBegin(); member != a.MemberEnd(); ++member) {
						std::string const property = member->name.GetString();
						if (InstanceOwned(entry.isRoot, name, property)) { continue; }
						auto const other = b.FindMember(member->name);
						if (other == b.MemberEnd() || !(other->value == member->value)) {
							out.push_back({ Override::Kind::Property, entity, name, property });
						}
					}
				}

				if constexpr (ToggleableComponent<T>) {
					bool const theirsOn = doc.Get<bool>(path + "/" + kComponentEnabledKey, true);
					if (world.IsComponentEnabled<T>(entity) != theirsOn) {
						out.push_back({ Override::Kind::Property, entity, name, kComponentEnabledKey });
					}
				}
			}
		}

		// Puts one item back from the prefab entry. False if nothing could be done.
		template <typename T>
		bool RevertComponent(World& world, Override const& item, EntryRef const& entry) {
			if constexpr (!kCompared<T>) {
				return false;
			}
			else {
				std::string const path = entry.components + "/" + item.component;
				JsonWrapper const& doc = entry.prefab->Document();

				if (item.kind == Override::Kind::RemovedComponent) {
					if constexpr (RemovableComponent<T>) {
						if (world.HasComponent<T>(item.entity) || !doc.Has(path)) { return false; }
						T restored{};
						doc.GetObject(path, restored);
						EntityRefs::Remap(restored, EntryToInstance(world, item.entity, *entry.prefab, true));   // entry ids -> this instance's parts
						if constexpr (ToggleableComponent<T>) {
							world.AddComponent<T>(item.entity, std::move(restored), doc.Get<bool>(path + "/" + kComponentEnabledKey, true));
						}
						else {
							world.AddComponent<T>(item.entity, std::move(restored));
						}
						return true;
					}
					return false;
				}

				if (item.kind == Override::Kind::AddedComponent) {
					if constexpr (RemovableComponent<T>) {
						if (!world.HasComponent<T>(item.entity)) { return false; }
						world.RemoveComponent<T>(item.entity);
						return true;
					}
					return false;
				}

				// A property, or the whole component (property ""). Starts from the instance's
				// own value, so runtime-only fields (handles, caches) are kept.
				T const* mine = world.Read<T>(item.entity);
				if (mine == nullptr) { return false; }
				bool const wholeComponent = item.property.empty();

				if constexpr (ToggleableComponent<T>) {
					if (wholeComponent || item.property == kComponentEnabledKey) {
						world.SetComponentEnabled<T>(item.entity, doc.Get<bool>(path + "/" + kComponentEnabledKey, true));
						mine = world.Read<T>(item.entity);
						if (!wholeComponent) { return true; }
					}
				}
				if (item.property == kComponentEnabledKey) { return false; }   // not switchable

				T current = *mine;
				T theirs{};
				if (doc.Has(path)) { doc.GetObject(path, theirs); }
				EntityRefs::Remap(theirs, EntryToInstance(world, item.entity, *entry.prefab, true));   // entry ids -> this instance's parts
				for (auto const& prop : rttr::type::get<T>().get_properties()) {
					if (prop.get_metadata(MetaDataTypes::DO_NOT_SERIALIZE).to_bool()) { continue; }
					std::string const property = prop.get_name().to_string();
					if (!wholeComponent && property != item.property) { continue; }
					if (InstanceOwned(entry.isRoot, item.component, property)) { continue; }
					prop.set_value(current, prop.get_value(theirs));
				}
				if constexpr (std::is_same_v<T, EntityName>) {
					world.SetEntityName(item.entity, current.name);   // EntityName is only written through World
				}
				else if constexpr (std::is_same_v<T, EntityTag>) {
					world.AddComponent<EntityTag>(item.entity, std::move(current));   // the prefab's tag as saved (only written through World)
				}
				else {
					*world.Write<T>(item.entity) = std::move(current);
				}
				return true;
			}
		}

		bool RevertOne(World& world, Override const& item) {
			EntryRef entry;
			if (!FindEntry(world, item.entity, entry)) { return false; }
			bool done = false;
			bool matched = false;
			#define CRU_PREFAB_REVERT(T) \
				if (!matched && item.component == ComponentTraits<T>::name) { matched = true; done = RevertComponent<T>(world, item, entry); }
			CRUCIBLE_COMPONENT_LIST(CRU_PREFAB_REVERT)
			#undef CRU_PREFAB_REVERT
			return done;
		}

		// "Collider/m_size", "+Light", "-Collider": PrefabInstance::overrides entries.
		std::string Encode(Override const& item) {
			switch (item.kind) {
				case Override::Kind::AddedComponent:   return "+" + item.component;
				case Override::Kind::RemovedComponent: return "-" + item.component;
				default:                               return item.component + "/" + item.property;
			}
		}

		// The overrides of the entity being drawn in the Inspector, worked out once per frame.
		struct DrawState {
			World*                 world = nullptr;
			Entity                 entity{};
			std::string            component;      // set between BeginDrawComponent / EndDrawComponent
			int                    frame = -1;
			Entity                 frameEntity{};
			std::vector<Override>  overrides;      // frameEntity's, this frame
		};
		DrawState g_draw;

		std::vector<Override> const& DrawnOverrides(World& world, Entity entity) {
			int const frame = ImGui::GetFrameCount();
			if (g_draw.frame != frame || !(g_draw.frameEntity == entity)) {
				g_draw.frame       = frame;
				g_draw.frameEntity = entity;
				g_draw.overrides   = EntityOverrides(world, entity);
			}
			return g_draw.overrides;
		}

		// A revert or apply picked from inside the Inspector's component loop: run at the start of the
		// next DrawInspectorBar, before any component pointer is taken (a re-added or removed
		// component moves the entity to another archetype).
		struct PendingAction {
			bool                  apply = false;   // false = revert
			std::vector<Override> items;
			std::string           label;
		};
		std::optional<PendingAction> g_pending;

		// The 3-pixel bar at the left edge of the last item, Unity's override mark.
		void DrawOverrideBar() {
			ImVec2 const min = ImGui::GetItemRectMin();
			ImVec2 const max = ImGui::GetItemRectMax();
			float const x = ImGui::GetWindowPos().x + 1.0f;
			ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x, min.y), ImVec2(x + 3.0f, max.y), ImGui::GetColorU32(kPrefabColour));
		}

		// False while updating a scene FILE in a scratch world (UpdateOtherScenes): the changes are
		// made the same way, but nothing goes to the undo history - it belongs to the open scene.
		bool g_recording = true;
		struct NotRecorded {
			NotRecorded()  { g_recording = false; }
			~NotRecorded() { g_recording = true; }
			NotRecorded(NotRecorded const&) = delete;
			NotRecorded& operator=(NotRecorded const&) = delete;
		};

		// Moves an entity out to be a root, keeping its world position (and its own children), and
		// returns the undo command for it - null if it already is a root or the move failed.
		// The same move as dragging it onto the Hierarchy's empty space.
		std::unique_ptr<EditorCommand> DetachToRoot(World& world, Entity child, std::string const& label) {
			Entity const oldParent = world.GetParent(child);
			Transform const* local = world.Read<Transform>(child);
			if (oldParent == kNullEntity || local == nullptr) { return nullptr; }
			Transform const localBefore = *local;
			std::uint32_t const indexBefore = world.GetSiblingIndex(child);
			// true: keep the world position. A root has no parent scale to fail on.
			if (world.TrySetParent(child, kNullEntity, true) != ParentResult::Ok) {
				CRU_USER_WARN("Could not detach '{}' from the prefab instance", NameOf(world, child));
				return nullptr;
			}
			if (!g_recording) { return nullptr; }
			UndoHistory& history = EditorManager::GetInstance().GetHistory();
			ReparentCommand::Placement const before{ history.KeyOf(oldParent), indexBefore, localBefore };
			ReparentCommand::Placement const after{ kNullEntity, world.GetSiblingIndex(child), *world.Read<Transform>(child) };
			return std::make_unique<ReparentCommand>(history.KeyOf(child), before, after, label);
		}

		bool IsAddedChild(World const& world, Entity child, std::uint64_t prefab) {
			PrefabInstance const* link = world.Read<PrefabInstance>(child);
			return !(link && !link->isRoot && link->prefabAsset == prefab);
		}
	}

	namespace {
		std::vector<Override> DiffAgainst(World& world, Entity entity, EntryRef const& entry) {
			std::vector<Override> out;
			#define CRU_PREFAB_DIFF(T) DiffComponent<T>(world, entity, entry, out);
			CRUCIBLE_COMPONENT_LIST(CRU_PREFAB_DIFF)
			#undef CRU_PREFAB_DIFF
			return out;
		}

		// Against one particular version of the file (C2: the version an instance last matched).
		std::vector<Override> EntityOverridesAgainst(World& world, Entity entity, Prefab const& version) {
			EntryRef entry;
			return FindEntryIn(world, entity, &version, entry) ? DiffAgainst(world, entity, entry) : std::vector<Override>{};
		}
	}

	std::vector<Override> EntityOverrides(World& world, Entity entity) {
		EntryRef entry;
		return FindEntry(world, entity, entry) ? DiffAgainst(world, entity, entry) : std::vector<Override>{};
	}

	std::vector<Override> InstanceOverrides(World& world, Entity root) {
		std::vector<Override> out;
		if (!IsInstanceRoot(world, root)) { return out; }
		std::uint64_t const prefab = world.Read<PrefabInstance>(root)->prefabAsset;
		for (Entity const member : InstanceMembers(world, root, prefab)) {
			std::vector<Override> own = EntityOverrides(world, member);
			out.insert(out.end(), own.begin(), own.end());
			world.ForEachChild(member, [&](Entity child) {
				if (IsAddedChild(world, child, prefab)) { out.push_back({ Override::Kind::AddedEntity, child, {}, {} }); }
			});
		}
		return out;
	}

	std::string Describe(World const& world, Override const& item) {
		switch (item.kind) {
			case Override::Kind::AddedComponent:   return item.component + " (added)";
			case Override::Kind::RemovedComponent: return item.component + " (removed)";
			case Override::Kind::AddedEntity:      return "child '" + NameOf(world, item.entity) + "' (added)";
			default:
				if (item.property == kComponentEnabledKey) { return item.component + " on/off"; }
				return item.property.empty() ? item.component : item.component + "." + item.property;
		}
	}

	void Revert(World& world, std::vector<Override> const& items, std::string const& label) {
		if (items.empty()) { return; }
		CommitInspectorEdit();
		UndoHistory& history = EditorManager::GetInstance().GetHistory();
		std::vector<std::unique_ptr<EditorCommand>> parts;

		// Entity by entity: one before/after snapshot each, however many of its values change.
		std::vector<Entity> touched;
		for (Override const& item : items) {
			if (item.kind == Override::Kind::AddedEntity) { continue; }
			if (std::find(touched.begin(), touched.end(), item.entity) == touched.end()) { touched.push_back(item.entity); }
		}
		for (Entity const e : touched) {
			if (!world.IsAlive(e)) { continue; }
			std::string before = EntityState::Capture(world, e);
			bool any = false;
			for (Override const& item : items) {
				if (item.entity == e && item.kind != Override::Kind::AddedEntity) { any |= RevertOne(world, item); }
			}
			std::string after = EntityState::Capture(world, e);
			if (any && after != before) {
				parts.push_back(std::make_unique<EntityStateCommand>(history.KeyOf(e), std::move(before), std::move(after), label));
			}
		}

		// Added entities last. They are not deleted: they leave the instance and become roots,
		// keeping their world position (and their own children), the same move as dragging them
		// onto the Hierarchy's empty space - so nothing the user built is lost.
		for (Override const& item : items) {
			if (item.kind != Override::Kind::AddedEntity || !world.IsAlive(item.entity)) { continue; }
			if (auto detach = DetachToRoot(world, item.entity, label)) { parts.push_back(std::move(detach)); }
		}

		if (parts.empty()) { return; }
		history.Push(GroupCommand::Make(std::move(parts), label));
		if (Core* core = EditorManager::GetInstance().GetCore()) { core->GetScenes().MarkDirty(); }
		g_draw.frame = -1;   // the marks are stale now
		CRU_USER_INFO("{}", label);
	}

	void RefreshStoredOverrides(World& world) {
		std::vector<Entity> roots;
		world.CreateQuery<const PrefabInstance>().ForEach([&](Entity e, PrefabInstance const& link) {
			if (link.isRoot) { roots.push_back(e); }
		});
		for (Entity const root : roots) {
			std::uint64_t const prefab = world.Read<PrefabInstance>(root)->prefabAsset;
			for (Entity const member : InstanceMembers(world, root, prefab)) {
				std::vector<std::string> list;
				for (Override const& item : EntityOverrides(world, member)) { list.push_back(Encode(item)); }
				// Written only when it changed: a write stamps the component as changed.
				if (world.Read<PrefabInstance>(member)->overrides != list) {
					world.Write<PrefabInstance>(member)->overrides = std::move(list);
				}
			}
		}
	}

	void InstallSceneHooks(World& world, SceneManager& scenes) {
		static bool installed = false;
		if (installed) { return; }
		installed = true;
		scenes.OnBeforeSave([&world] { RefreshStoredOverrides(world); });
		// Registered after UndoHistory's own OnSceneChanged (which clears it), so the update
		// below survives as the new scene's first undo step.
		scenes.OnSceneChanged([&world] { UpdateChangedInstances(world); });
	}

	void RunStartupCheck(World& world) {
		static bool done = false;
		if (done) { return; }
		Core* core = EditorManager::GetInstance().GetCore();
		if (core == nullptr || !core->GetAssets().IsInitialized()) { return; }   // asset index not up yet: next frame
		done = true;
		UpdateChangedInstances(world);
	}

	void BeginDrawComponent(World& world, Entity entity, std::string_view component) {
		g_draw.world     = &world;
		g_draw.entity    = entity;
		g_draw.component = std::string(component);
	}

	void EndDrawComponent() {
		g_draw.world = nullptr;
		g_draw.component.clear();
	}

	void MarkProperty(std::string const& property) {
		if (g_draw.world == nullptr) { return; }
		for (Override const& item : DrawnOverrides(*g_draw.world, g_draw.entity)) {
			if (item.kind != Override::Kind::Property || item.component != g_draw.component || item.property != property) { continue; }
			DrawOverrideBar();
			ImGui::SetItemTooltip("Overridden: differs from the prefab. Right-click to revert.");
			// One popup per property: they all live in the same component's ID scope.
			ImGui::PushID(property.c_str());
			if (ImGui::BeginPopupContextItem("##PrefabPropertyMenu")) {
				std::string const what = Describe(*g_draw.world, item);
				if (ImGui::MenuItem(("Apply " + property + " to Prefab").c_str(), nullptr, false, CanCreate())) {
					g_pending = PendingAction{ true, { item }, "Apply " + what };
				}
				if (ImGui::MenuItem(("Revert " + property + " to Prefab").c_str())) {
					g_pending = PendingAction{ false, { item }, "Revert " + what };
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
			return;
		}
	}

	void MarkComponentHeader(World& world, Entity entity, std::string_view component) {
		for (Override const& item : DrawnOverrides(world, entity)) {
			if (item.component == component && item.kind != Override::Kind::RemovedComponent) {
				DrawOverrideBar();
				return;
			}
		}
	}

	void ComponentMenuItems(World& world, Entity entity, std::string_view component) {
		std::vector<Override> mine;
		for (Override const& item : DrawnOverrides(world, entity)) {
			if (item.component == component) { mine.push_back(item); }
		}
		if (mine.empty()) { return; }
		ImGui::Separator();
		bool const added = mine.front().kind == Override::Kind::AddedComponent;
		std::string const name(component);
		// One item for the whole component: property "" applies / reverts every value of it.
		Override const whole{ added ? Override::Kind::AddedComponent : Override::Kind::Property, entity, name, {} };
		if (ImGui::MenuItem(added ? "Apply to Prefab (add it there)" : "Apply Component to Prefab", nullptr, false, CanCreate())) {
			g_pending = PendingAction{ true, { whole }, "Apply " + name };
		}
		if (ImGui::MenuItem(added ? "Remove (added to this instance)" : "Revert Component to Prefab")) {
			g_pending = PendingAction{ false, { whole }, (added ? "Remove " : "Revert ") + name };
		}
	}

	// ======================================================================= apply + propagation

	namespace {

		std::uint64_t Pack(Entity e) { return (static_cast<std::uint64_t>(e.index) << 32) | e.generation; }

		void InvalidateCachedPrefab(std::uint64_t prefabAsset) {
			g_prefabs.erase(prefabAsset);
			// The file is being re-written: the ResourceManager's shared copy is out of date too.
			if (Core* core = EditorManager::GetInstance().GetCore()) { core->GetResources().ReloadPrefab(prefabAsset); }
			g_draw.frame = -1;
		}

		bool ReadText(fs::path const& file, std::string& out) {
			std::ifstream in(file, std::ios::binary);
			if (!in) { return false; }
			out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
			return true;
		}

		bool WriteText(fs::path const& file, std::string const& text) {
			std::ofstream out(file, std::ios::binary | std::ios::trunc);
			if (!out) { return false; }
			out << text;
			return out.good();
		}

		// The .cprefab's contents before and after an Apply. The last part of the Apply's undo
		// group, so Ctrl+Z / Ctrl+Y select the applied root; the instance changes around it are
		// snapshots, so the order the file and the entities are restored in doesn't matter.
		class PrefabFileCommand final : public EditorCommand {
		public:
			PrefabFileCommand(std::uint64_t prefab, fs::path file, std::string before, std::string after,
			                  EntityKey select, std::string label)
				: m_prefab(prefab), m_file(std::move(file)), m_before(std::move(before)), m_after(std::move(after))
				, m_select(select), m_label(std::move(label)) {}

			void Undo(UndoHistory&) override { Write(m_before, m_after); }
			void Redo(UndoHistory&) override { Write(m_after, m_before); }
			EntityKey          Target() const override { return m_select; }
			std::string const& Label()  const override { return m_label; }

		private:
			// Only over the version this step left: the file may have been saved since (Prefab
			// Mode, a git pull), and undoing an old Apply must not throw that work away.
			void Write(std::string const& text, std::string const& expected) const {
				std::string current;
				if (ReadText(m_file, current) && current != expected) {
					CRU_USER_WARN("'{}' has changed since this step, so the file was left as it is", m_file.filename().string());
				}
				else if (!WriteText(m_file, text)) {
					CRU_USER_ERROR("Could not write '{}' - the prefab file and the scene may now disagree", m_file.string());
				}
				InvalidateCachedPrefab(m_prefab);
			}

			std::uint64_t m_prefab;
			fs::path      m_file;
			std::string   m_before;
			std::string   m_after;
			EntityKey     m_select;
			std::string   m_label;
		};

		// A part whose entry moved to another parent in the prefab follows it (local Transform
		// kept: it is relative to the prefab's own tree).
		std::unique_ptr<EditorCommand> MoveUnder(World& world, Entity child, Entity parent, std::string const& label) {
			Entity const oldParent = world.GetParent(child);
			Transform const* local = world.Read<Transform>(child);
			if (local == nullptr || oldParent == parent) { return nullptr; }
			Transform const localBefore = *local;
			std::uint32_t const indexBefore = world.GetSiblingIndex(child);
			if (world.TrySetParent(child, parent, false) != ParentResult::Ok) { return nullptr; }
			if (!g_recording) { return nullptr; }
			UndoHistory& history = EditorManager::GetInstance().GetHistory();
			ReparentCommand::Placement const before{ oldParent == kNullEntity ? kNullEntity : history.KeyOf(oldParent), indexBefore, localBefore };
			ReparentCommand::Placement const after{ history.KeyOf(parent), world.GetSiblingIndex(child), *world.Read<Transform>(child) };
			return std::make_unique<ReparentCommand>(history.KeyOf(child), before, after, label);
		}

		bool Overridden(std::vector<Override> const& own, std::string_view component, std::string_view property) {
			return std::any_of(own.begin(), own.end(), [&](Override const& o) {
				return o.kind == Override::Kind::Property && o.component == component && o.property == property;
			});
		}

		bool HasKind(std::vector<Override> const& own, Override::Kind kind, std::string_view component) {
			return std::any_of(own.begin(), own.end(), [&](Override const& o) { return o.kind == kind && o.component == component; });
		}

		// ---- writing an instance's change into the prefab document ----

		template <typename T>
		void WriteComponentInto(World& world, Entity e, JsonWrapper& doc, std::string const& path) {
			T const* mine = world.Read<T>(e);
			if (mine == nullptr) { return; }
			doc.SetObject(path, *mine);
			if constexpr (ToggleableComponent<T>) {
				// Same convention as scene files: "$enabled": false only while switched off.
				if (!world.IsComponentEnabled<T>(e)) { doc.Set<bool>(path + "/" + kComponentEnabledKey, false); }
			}
		}

		template <typename T>
		bool ApplyComponent(World& world, Override const& item, JsonWrapper& doc, std::string const& path, bool isRoot) {
			if constexpr (!kCompared<T>) {
				return false;
			}
			else {
				std::string const enabledPath = path + "/" + kComponentEnabledKey;
				if (item.kind == Override::Kind::RemovedComponent) {
					if (!doc.Has(path)) { return false; }
					doc.Remove(path);
					return true;
				}
				if (item.kind == Override::Kind::AddedComponent) {
					if (!world.HasComponent<T>(item.entity)) { return false; }
					WriteComponentInto<T>(world, item.entity, doc, path);
					return true;
				}

				T const* mine = world.Read<T>(item.entity);
				if (mine == nullptr) { return false; }
				bool const wholeComponent = item.property.empty();

				if (item.property == kComponentEnabledKey) {
					if constexpr (ToggleableComponent<T>) {
						if (world.IsComponentEnabled<T>(item.entity)) { doc.Remove(enabledPath); }
						else                                          { doc.Set<bool>(enabledPath, false); }
						return true;
					}
					return false;
				}

				// The entry's other values stay: start from the file's, take only the applied
				// property (every non-instance-owned one, for a whole component) from the instance.
				bool const wasOff = doc.Has(enabledPath);
				T entry{};
				if (doc.Has(path)) { doc.GetObject(path, entry); }
				T source = *mine;
				for (auto const& prop : rttr::type::get<T>().get_properties()) {
					if (prop.get_metadata(MetaDataTypes::DO_NOT_SERIALIZE).to_bool()) { continue; }
					std::string const property = prop.get_name().to_string();
					if (!wholeComponent && property != item.property) { continue; }
					if (InstanceOwned(isRoot, item.component, property)) { continue; }
					prop.set_value(entry, prop.get_value(source));
				}
				doc.SetObject(path, entry);   // replaces the object, so the on/off state is put back below

				bool off = wasOff;
				if constexpr (ToggleableComponent<T>) {
					if (wholeComponent) { off = !world.IsComponentEnabled<T>(item.entity); }
				}
				if (off) { doc.Set<bool>(enabledPath, false); }
				return true;
			}
		}

		// An entity added under the instance becomes entries of the prefab, with all of its own
		// children, appended after the existing ones (so parents still come before children).
		// `added` receives each one with its new id, to link it once the file is written.
		bool AppendSubtree(World& world, Entity top, JsonWrapper& doc, Prefab const& old,
		                   std::vector<std::pair<Entity, std::uint64_t>>& added) {
			Entity const parent = world.GetParent(top);
			PrefabInstance const* parentLink = world.IsAlive(parent) ? world.Read<PrefabInstance>(parent) : nullptr;
			if (parentLink == nullptr) { return false; }
			int const parentIndex = old.IndexOf(parentLink->fileId);
			if (parentIndex < 0) { return false; }

			auto visit = [&](auto&& self, Entity e, int parent) -> void {
				std::size_t const index = doc.PushObject("entities");
				std::string const base = "entities/" + std::to_string(index);
				std::uint64_t const id = xresource::instance_guid::GenerateGUIDCopy().m_Value;
				doc.Set(base + "/id", AssetRegistry::GuidToString(xresource::instance_guid{ id }));
				doc.Set(base + "/parent", parent);
				EntityIO::WriteComponents(world, e, doc, base + "/components");
				doc.Remove(base + "/components/" + std::string(ComponentTraits<PrefabInstance>::name));   // never a link in a file
				added.emplace_back(e, id);
				int const thisIndex = static_cast<int>(index);
				world.ForEachChild(e, [&](Entity child) { self(self, child, thisIndex); });
			};
			visit(visit, top, parentIndex);
			return true;
		}

		bool ApplyOneToDoc(World& world, Override const& item, JsonWrapper& doc, Prefab const& old,
		                   std::vector<std::pair<Entity, std::uint64_t>>& added) {
			if (!world.IsAlive(item.entity)) { return false; }
			if (item.kind == Override::Kind::AddedEntity) { return AppendSubtree(world, item.entity, doc, old, added); }

			PrefabInstance const* link = world.Read<PrefabInstance>(item.entity);
			if (link == nullptr) { return false; }
			int const index = old.IndexOf(link->fileId);
			if (index < 0) { return false; }
			std::string const path = old.ComponentsPath(static_cast<std::size_t>(index)) + "/" + item.component;

			bool done = false;
			bool matched = false;
			#define CRU_PREFAB_APPLY(T) \
				if (!matched && item.component == ComponentTraits<T>::name) { matched = true; done = ApplyComponent<T>(world, item, doc, path, link->isRoot); }
			CRUCIBLE_COMPONENT_LIST(CRU_PREFAB_APPLY)
			#undef CRU_PREFAB_APPLY
			return done;
		}

		// ---- bringing an instance up to date with the prefab ----

		// One component of one part: everything follows the prefab entry except what this
		// entity overrode (`own`, worked out against the file as it was) and the root's
		// instance-owned values.
		template <typename T>
		void UpdateComponent(World& world, Entity e, JsonWrapper const& doc, std::string const& components,
		                     bool isRoot, std::vector<Override> const& own) {
			if constexpr (kCompared<T>) {
				std::string_view const name = ComponentTraits<T>::name;
				std::string const path = components + "/" + std::string(name);
				bool const inPrefab = doc.Has(path);
				bool const has = world.HasComponent<T>(e);

				if constexpr (RemovableComponent<T>) {
					if (inPrefab && !has) {
						if (HasKind(own, Override::Kind::RemovedComponent, name)) { return; }   // this instance removed it
						T restored{};
						doc.GetObject(path, restored);
						if constexpr (ToggleableComponent<T>) {
							world.AddComponent<T>(e, std::move(restored), doc.Get<bool>(path + "/" + kComponentEnabledKey, true));
						}
						else {
							world.AddComponent<T>(e, std::move(restored));
						}
						return;
					}
					if (!inPrefab && has) {
						if (!HasKind(own, Override::Kind::AddedComponent, name)) { world.RemoveComponent<T>(e); }
						return;
					}
				}
				if (!inPrefab || !has) { return; }
				if (HasKind(own, Override::Kind::AddedComponent, name)) { return; }   // the instance's own: kept whole

				T current = *world.Read<T>(e);
				T theirs{};
				doc.GetObject(path, theirs);
				for (auto const& prop : rttr::type::get<T>().get_properties()) {
					if (prop.get_metadata(MetaDataTypes::DO_NOT_SERIALIZE).to_bool()) { continue; }
					std::string const property = prop.get_name().to_string();
					if (InstanceOwned(isRoot, name, property) || Overridden(own, name, property)) { continue; }
					prop.set_value(current, prop.get_value(theirs));
				}
				if constexpr (std::is_same_v<T, EntityName>) {
					if (current.name != world.Read<EntityName>(e)->name) { world.SetEntityName(e, current.name); }
				}
				else if constexpr (std::is_same_v<T, EntityTag>) {
					if (current.tag != world.Read<EntityTag>(e)->tag) { world.AddComponent<EntityTag>(e, std::move(current)); }
				}
				else {
					*world.Write<T>(e) = std::move(current);
				}

				if constexpr (ToggleableComponent<T>) {
					if (!Overridden(own, name, kComponentEnabledKey)) {
						bool const on = doc.Get<bool>(path + "/" + kComponentEnabledKey, true);
						if (world.IsComponentEnabled<T>(e) != on) { world.SetComponentEnabled<T>(e, on); }
					}
				}
			}
		}

		// Records on an instance root which version of its file it now matches (undoable).
		void StampHash(World& world, Entity root, std::uint64_t hash,
		               std::vector<std::unique_ptr<EditorCommand>>& parts, std::string const& label) {
			PrefabInstance const* link = world.IsAlive(root) ? world.Read<PrefabInstance>(root) : nullptr;
			if (link == nullptr || link->prefabHash == hash) { return; }
			if (!g_recording) { world.Write<PrefabInstance>(root)->prefabHash = hash; return; }
			UndoHistory& history = EditorManager::GetInstance().GetHistory();
			std::string before = EntityState::Capture(world, root);
			world.Write<PrefabInstance>(root)->prefabHash = hash;
			parts.push_back(std::make_unique<EntityStateCommand>(history.KeyOf(root), std::move(before),
			                                                     EntityState::Capture(world, root), label));
		}

		// PrefabInstance::overrides back into Overrides: "C/p", "+C", "-C" (see Encode).
		std::vector<Override> Decode(Entity entity, std::vector<std::string> const& stored) {
			std::vector<Override> out;
			for (std::string const& s : stored) {
				if (s.empty()) { continue; }
				if (s[0] == '+') { out.push_back({ Override::Kind::AddedComponent,   entity, s.substr(1), {} }); continue; }
				if (s[0] == '-') { out.push_back({ Override::Kind::RemovedComponent, entity, s.substr(1), {} }); continue; }
				std::size_t const slash = s.find('/');
				if (slash == std::string::npos) { continue; }
				out.push_back({ Override::Kind::Property, entity, s.substr(0, slash), s.substr(slash + 1) });
			}
			return out;
		}

		// Makes one instance match `prefab` (already the new file), keeping each part's
		// overrides from `overrides` (by Pack(entity)). Every change goes into `parts` for undo.
		void SyncInstance(World& world, Entity root, Prefab const& prefab, std::uint64_t prefabAsset,
		                  std::unordered_map<std::uint64_t, std::vector<Override>> const& overrides,
		                  std::vector<std::unique_ptr<EditorCommand>>& parts, std::string const& label) {
			UndoHistory& history = EditorManager::GetInstance().GetHistory();
			JsonWrapper const& doc = prefab.Document();
			std::size_t const count = prefab.EntityCount();
			if (count == 0 || prefab.IdAt(0) != world.Read<PrefabInstance>(root)->fileId) {
				CRU_USER_WARN("'{}' no longer matches its prefab's root - left as it is (unpack and place it again)", NameOf(world, root));
				return;
			}

			std::unordered_map<std::uint64_t, Entity> members;   // entry id -> part
			for (Entity const m : InstanceMembers(world, root, prefabAsset)) { members[world.Read<PrefabInstance>(m)->fileId] = m; }
			std::unordered_set<std::uint64_t> ids;
			for (std::size_t i = 0; i < count; ++i) { ids.insert(prefab.IdAt(i)); }

			static std::vector<Override> const kNone;
			std::vector<Entity> byIndex(count, kNullEntity);
			std::vector<Entity> created;       // every entity made here
			std::vector<Entity> createdTops;   // ...and the ones whose parent already existed (one undo capture each)

			for (std::size_t i = 0; i < count; ++i) {
				std::string const components = prefab.ComponentsPath(i);
				int const parentIndex = prefab.ParentAt(i);
				Entity const wantParent = parentIndex < 0 ? kNullEntity : byIndex[static_cast<std::size_t>(parentIndex)];
				auto const found = members.find(prefab.IdAt(i));

				if (found != members.end()) {
					Entity const e = found->second;
					byIndex[i] = e;
					auto const ownIt = overrides.find(Pack(e));
					std::vector<Override> const& own = ownIt != overrides.end() ? ownIt->second : kNone;

					std::string before = EntityState::Capture(world, e);
					#define CRU_PREFAB_UPDATE(T) UpdateComponent<T>(world, e, doc, components, i == 0, own);
					CRUCIBLE_COMPONENT_LIST(CRU_PREFAB_UPDATE)
					#undef CRU_PREFAB_UPDATE
					std::string after = EntityState::Capture(world, e);
					if (g_recording && after != before) {
						parts.push_back(std::make_unique<EntityStateCommand>(history.KeyOf(e), std::move(before), std::move(after), label));
					}

					if (i > 0 && world.IsAlive(wantParent)) {
						if (auto move = MoveUnder(world, e, wantParent, label)) { parts.push_back(std::move(move)); }
					}
					continue;
				}

				// An entry this instance doesn't have yet: a new part.
				if (i == 0 || !world.IsAlive(wantParent)) { continue; }
				Entity const e = world.CreateEntity();
				EntityIO::ReadComponents(world, e, doc, components);
				world.ReissueDefaultName(e);
				world.AddComponent<PrefabInstance>(e, PrefabInstance{ prefabAsset, prefab.IdAt(i), false });
				world.TrySetParent(e, wantParent, false);
				byIndex[i] = e;
				bool const parentIsNew = std::find(created.begin(), created.end(), wantParent) != created.end();
				created.push_back(e);
				if (!parentIsNew) { createdTops.push_back(e); }
			}

			// References between parts: values that came from the file name parts by ENTRY id; point
			// them at this instance's parts (scene ids). New parts are captured whole further down, so
			// only changes to existing parts are recorded here (after their update, so undo/redo order holds).
			{
				EntityRefs::Map const toScene = prefab.EntryToSceneMap(
					[&byIndex](std::size_t index) { return index < byIndex.size() ? byIndex[index] : kNullEntity; },
					&EnsureSceneIdOf);
				for (Entity const e : byIndex) {
					if (!world.IsAlive(e)) { continue; }
					if (std::find(created.begin(), created.end(), e) != created.end()) {
						EntityRefs::RemapEntity(world, e, toScene);
						continue;
					}
					std::string before = EntityState::Capture(world, e);
					if (EntityRefs::RemapEntity(world, e, toScene) && g_recording) {
						parts.push_back(std::make_unique<EntityStateCommand>(history.KeyOf(e), std::move(before), EntityState::Capture(world, e), label));
					}
				}
			}

			// Parts whose entry the prefab no longer has. Only the top-most of a removed branch is
			// deleted (the rest goes with it); children the user added under them are detached as
			// roots first, so nothing they built is lost (the same rule as Revert / Detach).
			for (auto const& [id, e] : members) {
				if (ids.count(id) != 0 || !world.IsAlive(e)) { continue; }
				Entity const parent = world.GetParent(e);
				PrefabInstance const* parentLink = world.IsAlive(parent) ? world.Read<PrefabInstance>(parent) : nullptr;
				if (parentLink != nullptr && parentLink->prefabAsset == prefabAsset && ids.count(parentLink->fileId) == 0) { continue; }

				std::vector<Entity> subtree;
				world.CollectSubtree(e, subtree);
				for (Entity const x : subtree) {
					if (x == e || !world.IsAlive(x)) { continue; }
					if (IsAddedChild(world, x, prefabAsset) && !IsAddedChild(world, world.GetParent(x), prefabAsset)) {
						if (auto detach = DetachToRoot(world, x, label)) { parts.push_back(std::move(detach)); }
					}
				}
				if (g_recording) { parts.push_back(DeleteEntityCommand::Capture(history, e, label)); }
				world.DestroyEntity(e);
			}

			// New parts last: undo removes them first.
			for (Entity const top : createdTops) {
				if (!g_recording) { break; }
				parts.push_back(std::make_unique<InvertedCommand>(DeleteEntityCommand::Capture(history, top, label), label));
			}
		}
	}

	namespace {
		char const* const kAllScenesPopup = "Apply to All Scenes?";
		std::vector<std::string> g_otherScenes;   // listed in the confirm box; filled when it opens

		void DrawAllScenesConfirm(World& world, Entity root, std::vector<Override> const& all, std::string const& path) {
			ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
			if (!ImGui::BeginPopupModal(kAllScenesPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) { return; }

			std::string const name = PrefabNameOf(path);
			if (all.empty()) { ImGui::Text("This instance has no overrides: %s itself stays as it is.", name.c_str()); }
			else             { ImGui::Text("Applies %zu override%s from this instance to %s.", all.size(), all.size() == 1 ? "" : "s", name.c_str()); }

			ImGui::Spacing();
			if (g_otherScenes.empty()) {
				ImGui::TextDisabled("No other scene uses %s - only this one is updated.", name.c_str());
			}
			else {
				ImGui::Text("Then these scenes are updated and saved (their own overrides kept):");
				for (std::string const& scene : g_otherScenes) { ImGui::BulletText("%s", scene.c_str()); }
			}

			ImGui::Spacing();
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Ctrl+Z undoes this scene's part only.");
			ImGui::TextDisabled("Other scene files are written straight to disk - git has the old versions.");

			ImGui::Spacing();
			bool close = false;
			if (ImGui::Button("Apply", ImVec2(120.0f, 0.0f))) {
				std::uint64_t const prefab = world.Read<PrefabInstance>(root)->prefabAsset;
				if (!all.empty()) { ApplyToPrefab(world, root, all, "Apply All (" + name + ")"); }   // this scene: undoable
				std::size_t const scenes = UpdateOtherScenes(prefab);                               // the rest: on disk
				CRU_USER_INFO("Apply to All Scenes ({}): {} other scene{} updated", name, scenes, scenes == 1 ? "" : "s");
				close = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { close = true; }
			if (close) { ImGui::CloseCurrentPopup(); }
			ImGui::EndPopup();
		}
	}

	Entity InstanceRootOf(World const& world, Entity entity) {
		PrefabInstance const* link = world.IsAlive(entity) ? world.Read<PrefabInstance>(entity) : nullptr;
		if (link == nullptr) { return kNullEntity; }
		std::uint64_t const prefab = link->prefabAsset;
		for (Entity e = entity; e.IsValid(); e = world.GetParent(e)) {
			PrefabInstance const* l = world.Read<PrefabInstance>(e);
			if (l == nullptr || l->prefabAsset != prefab) { return kNullEntity; }
			if (l->isRoot) { return e; }
		}
		return kNullEntity;
	}

	bool ApplyToPrefab(World& world, Entity root, std::vector<Override> const& items, std::string const& label) {
		if (items.empty() || !IsInstanceRoot(world, root)) { return false; }
		if (!CanCreate()) {
			CRU_USER_WARN("Stop Play Mode to apply changes to a prefab");
			return false;
		}
		Core* core = EditorManager::GetInstance().GetCore();
		std::uint64_t const prefab = world.Read<PrefabInstance>(root)->prefabAsset;
		std::string const path = PrefabPathOf(prefab);
		Prefab const* old = LoadedPrefab(prefab);
		if (path.empty() || old == nullptr) {
			CRU_USER_WARN("Cannot apply: the prefab of '{}' is missing or failed to load", NameOf(world, root));
			return false;
		}
		if (old->Version() <= 1) {
			CRU_USER_WARN("Cannot apply to '{}': it is an old single-entity prefab - create it again from the instance", path);
			return false;
		}

		fs::path const file = core->GetAssets().Resolve(path);
		std::string before;
		if (!ReadText(file, before)) {
			CRU_USER_ERROR("Cannot apply: could not read '{}'", path);
			return false;
		}
		CommitInspectorEdit();

		// The other instances' overrides, against the file as it is NOW - before it changes.
		std::vector<Entity> others;
		world.CreateQuery<const PrefabInstance>().ForEach([&](Entity e, PrefabInstance const& link) {
			if (link.isRoot && link.prefabAsset == prefab && !(e == root)) { others.push_back(e); }
		});
		std::unordered_map<std::uint64_t, std::vector<Override>> overrides;
		for (Entity const other : others) {
			for (Entity const m : InstanceMembers(world, other, prefab)) { overrides[Pack(m)] = EntityOverrides(world, m); }
		}

		// The new file: the current one with the applied changes written in.
		JsonWrapper doc;
		if (!doc.ReadFromString(before)) {
			CRU_USER_ERROR("Cannot apply: '{}' could not be parsed", path);
			return false;
		}
		for (std::size_t i = 0; i < old->EntityCount(); ++i) {   // a version 2 file gets the ids its instances already use
			std::string const idPath = "entities/" + std::to_string(i) + "/id";
			if (!doc.Has(idPath)) { doc.Set(idPath, AssetRegistry::GuidToString(xresource::instance_guid{ old->IdAt(i) })); }
		}
		doc.Set("version", kPrefabVersion);

		std::vector<std::pair<Entity, std::uint64_t>> added;
		std::size_t applied = 0;
		for (Override const& item : items) {
			if (ApplyOneToDoc(world, item, doc, *old, added)) { ++applied; }
		}
		if (applied == 0) {
			CRU_USER_WARN("Nothing to apply to '{}'", path);
			return false;
		}

		// Entity references written from the instance hold its scene ids; the file must hold entry
		// ids. A part of this instance (or an entity just added to the prefab) -> its entry id;
		// a value that already is an entry id -> kept; anything else (an outside entity, or an
		// older file's leftover scene id) -> cleared.
		{
			std::unordered_map<std::uint64_t, std::uint64_t> entryOfScene;   // scene id -> entry id
			for (Entity const m : InstanceMembers(world, root, prefab)) {
				if (std::uint64_t const sceneId = SceneIdOf(m); sceneId != 0) { entryOfScene[sceneId] = world.Read<PrefabInstance>(m)->fileId; }
			}
			std::unordered_set<std::uint64_t> entryIds;
			for (std::size_t i = 0; i < old->EntityCount(); ++i) { entryIds.insert(old->IdAt(i)); }
			for (auto const& [entity, id] : added) {
				entryIds.insert(id);
				if (std::uint64_t const sceneId = SceneIdOf(entity); sceneId != 0) { entryOfScene[sceneId] = id; }
			}
			std::size_t cleared = 0;
			EntityRefs::Map const toEntry = [&](std::uint64_t id) -> std::uint64_t {
				if (entryIds.count(id) != 0) { return id; }
				auto const found = entryOfScene.find(id);
				if (found != entryOfScene.end()) { return found->second; }
				++cleared;
				return 0;
			};
			for (std::size_t i = 0; i < doc.GetListSize("entities"); ++i) {
				EntityRefs::RemapDocument(doc, "entities/" + std::to_string(i) + "/components", toEntry);
			}
			if (cleared > 0) {
				CRU_USER_WARN("{}: {} reference{} to entities outside the prefab {} cleared - a prefab can only point at its own parts",
				              path, cleared, cleared == 1 ? "" : "s", cleared == 1 ? "was" : "were");
			}
		}

		if (!doc.SaveToFile(file)) {
			CRU_USER_ERROR("Cannot apply: could not write '{}'", path);
			return false;
		}
		std::string after;
		ReadText(file, after);
		InvalidateCachedPrefab(prefab);   // `old` is gone from here on

		UndoHistory& history = EditorManager::GetInstance().GetHistory();
		std::vector<std::unique_ptr<EditorCommand>> parts;

		// Entities added under the instance are parts of the prefab now.
		for (auto const& [entity, id] : added) {
			if (!world.IsAlive(entity)) { continue; }
			std::string beforeLink = EntityState::Capture(world, entity);
			world.AddComponent<PrefabInstance>(entity, PrefabInstance{ prefab, id, false });
			parts.push_back(std::make_unique<EntityStateCommand>(history.KeyOf(entity), std::move(beforeLink),
			                                                     EntityState::Capture(world, entity), label));
		}

		// Every other instance in the open scene follows the new file, keeping its own overrides,
		// and all of them - the applied one too - now match this version of it.
		std::size_t updated = 0;
		if (Prefab const* fresh = LoadedPrefab(prefab)) {
			for (Entity const other : others) {
				if (!world.IsAlive(other)) { continue; }
				std::size_t const partsBefore = parts.size();
				SyncInstance(world, other, *fresh, prefab, overrides, parts, label);
				if (parts.size() != partsBefore) { ++updated; }
				StampHash(world, other, fresh->ContentHash(), parts, label);
			}
			StampHash(world, root, fresh->ContentHash(), parts, label);
		}

		parts.push_back(std::make_unique<PrefabFileCommand>(prefab, file, std::move(before), std::move(after), history.KeyOf(root), label));
		history.Push(GroupCommand::Make(std::move(parts), label));
		core->GetScenes().MarkDirty();
		g_draw.frame = -1;

		CRU_USER_INFO("{}: {} change{} written to {}; {} other instance{} in this scene updated", label,
		              applied, applied == 1 ? "" : "s", path, updated, updated == 1 ? "" : "s");
		return true;
	}

	void RememberPrefabVersion(std::uint64_t prefabAsset) {
		LoadedPrefab(prefabAsset);   // caches it in g_versions, by content hash
	}

	void PrefabFileReplaced(std::uint64_t prefabAsset) {
		InvalidateCachedPrefab(prefabAsset);
		LoadedPrefab(prefabAsset);
	}

	namespace {
		// The update pass over one world: every instance (of `onlyPrefab`, or of any prefab when it
		// is 0) whose prefab changed since it last matched. Returns how many were updated.
		std::size_t UpdateInstancesIn(World& world, std::uint64_t onlyPrefab, std::vector<std::unique_ptr<EditorCommand>>& parts,
		                              std::string const& label, std::size_t& fromSavedList);
	}

	std::size_t UpdateChangedInstances(World& world) {
		// Never while playing: the world holds runtime state, and Stop throws it away anyway.
		if (!CanCreate()) { return 0; }

		std::string const label = "Update Prefab Instances";
		std::vector<std::unique_ptr<EditorCommand>> parts;
		std::size_t fromSavedList = 0;   // updated without the old file in memory: the scene's saved overrides were used
		std::size_t const updated = UpdateInstancesIn(world, 0, parts, label, fromSavedList);

		if (parts.empty()) { return updated; }
		EditorManager::GetInstance().GetHistory().Push(GroupCommand::Make(std::move(parts), label));
		if (Core* core = EditorManager::GetInstance().GetCore()) { core->GetScenes().MarkDirty(); }
		g_draw.frame = -1;
		CRU_USER_INFO("Updated {} prefab instance{} to match changed prefabs{}", updated, updated == 1 ? "" : "s",
		              fromSavedList > 0 ? " (using the overrides saved with the scene)" : "");
		return updated;
	}

	namespace {
	std::size_t UpdateInstancesIn(World& world, std::uint64_t onlyPrefab, std::vector<std::unique_ptr<EditorCommand>>& parts,
	                              std::string const& label, std::size_t& fromSavedList) {
		std::vector<Entity> roots;
		world.CreateQuery<const PrefabInstance>().ForEach([&](Entity e, PrefabInstance const& link) {
			if (link.isRoot && (onlyPrefab == 0 || link.prefabAsset == onlyPrefab)) { roots.push_back(e); }
		});

		std::size_t updated = 0;

		for (Entity const root : roots) {
			if (!world.IsAlive(root)) { continue; }   // a root deleted while updating an earlier one
			PrefabInstance const* link = world.Read<PrefabInstance>(root);
			if (link == nullptr || !link->isRoot) { continue; }
			std::uint64_t const prefab = link->prefabAsset;
			std::uint64_t const seen = link->prefabHash;

			Prefab const* current = LoadedPrefab(prefab);
			if (current == nullptr) { continue; }   // missing or unreadable: left alone (its rows show red)
			std::uint64_t const now = current->ContentHash();
			if (seen == now) { continue; }

			// Placed before versions were tracked: there is no telling what it last matched, so it
			// is not touched - just marked as matching the file from here on.
			if (seen == 0) {
				world.Write<PrefabInstance>(root)->prefabHash = now;
				continue;
			}

			// What this instance changed, against the version it last matched: exactly, if that
			// version was loaded this session; else from the list saved with the scene.
			Prefab const* baseline = PrefabVersion(prefab, seen);
			if (baseline == nullptr) { ++fromSavedList; }
			std::unordered_map<std::uint64_t, std::vector<Override>> overrides;
			for (Entity const m : InstanceMembers(world, root, prefab)) {
				overrides[Pack(m)] = baseline != nullptr
					? EntityOverridesAgainst(world, m, *baseline)
					: Decode(m, world.Read<PrefabInstance>(m)->overrides);
			}

			SyncInstance(world, root, *current, prefab, overrides, parts, label);
			StampHash(world, root, now, parts, label);
			++updated;
		}
		return updated;
	}
	}

	// ======================================================================= other scenes

	namespace {
		std::string PathKey(fs::path const& p) {
			std::string key = p.lexically_normal().generic_string();
			std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return key;
		}
	}

	std::vector<std::string> ScenesUsing(std::uint64_t prefabAsset) {
		std::vector<std::string> scenes;
		Core* core = EditorManager::GetInstance().GetCore();
		if (core == nullptr || prefabAsset == 0) { return scenes; }
		SceneManager& open = core->GetScenes();
		std::string const openKey = open.HasPath() ? PathKey(open.CurrentPath()) : std::string{};
		// A scene file stores the guid as a number ("prefabAsset": 1234...), which is all this looks for.
		std::string const needle = std::to_string(prefabAsset);
		for (AssetEntry const* scene : core->GetAssets().GetByType(AssetType::Scene)) {
			if (scene == nullptr || PathKey(scene->absolutePath) == openKey) { continue; }
			std::string text;
			if (ReadText(scene->absolutePath, text) && text.find(needle) != std::string::npos) { scenes.push_back(scene->virtualPath); }
		}
		std::sort(scenes.begin(), scenes.end());
		return scenes;
	}

	std::size_t UpdateOtherScenes(std::uint64_t prefabAsset) {
		if (!CanCreate()) {
			CRU_USER_WARN("Stop Play Mode to update other scenes");
			return 0;
		}
		Core* core = EditorManager::GetInstance().GetCore();
		std::string const name = PrefabNameOf(PrefabPathOf(prefabAsset));
		std::size_t rewritten = 0;

		for (std::string const& virtualPath : ScenesUsing(prefabAsset)) {
			fs::path const file = core->GetAssets().Resolve(virtualPath);

			// Its own world, never the open one: load, update, save. Not undoable (it is another
			// file - git has it), so nothing is recorded while doing it.
			World scratch;
			Scene scene;
			if (!scene.Load(scratch, file)) {
				CRU_USER_WARN("Could not open {} to update its {} instances - skipped", virtualPath, name);
				continue;
			}
			std::size_t updated = 0;
			{
				NotRecorded const quiet;
				SceneIdsFrom const ids(scene);   // entity references in this file use ITS scene ids
				std::vector<std::unique_ptr<EditorCommand>> unused;
				std::size_t fromSavedList = 0;
				updated = UpdateInstancesIn(scratch, prefabAsset, unused, "Update", fromSavedList);
				if (updated > 0) { RefreshStoredOverrides(scratch); }   // what the open scene's save hook would do
			}
			g_draw.frame = -1;   // the scratch world's entities share handle numbers with the open scene's
			if (updated == 0) { continue; }   // already up to date: the file is left untouched

			if (!scene.Save(scratch, file)) {
				CRU_USER_ERROR("Could not save {} after updating its {} instances", virtualPath, name);
				continue;
			}
			++rewritten;
			CRU_USER_INFO("Updated {} {} instance{} in {}", updated, name, updated == 1 ? "" : "s", virtualPath);
		}
		return rewritten;
	}

	namespace {
		// The narrowest a prefab-bar button gets before the row wraps onto another line.
		constexpr float kMinBarButtonWidth = 84.0f;

		// A row of buttons that always fits the panel: as many per line as can be at least
		// `minWidth` wide, the lines balanced (4 -> 2 + 2, not 3 + 1), and each line's buttons
		// stretched to share its width equally. Labels too long for a button are clipped by ImGui.
		class ButtonRow {
		public:
			ButtonRow(int count, float minWidth) {
				float const available = ImGui::GetContentRegionAvail().x;
				float const spacing = ImGui::GetStyle().ItemSpacing.x;
				int const fit = std::clamp(static_cast<int>((available + spacing) / (minWidth + spacing)), 1, std::max(count, 1));
				int const lines = (count + fit - 1) / fit;
				m_perLine = (count + lines - 1) / lines;
				m_width = std::max(1.0f, (available - spacing * static_cast<float>(m_perLine - 1)) / static_cast<float>(m_perLine));
			}

			bool Button(char const* label) {
				if (m_drawn % m_perLine != 0) { ImGui::SameLine(); }
				++m_drawn;
				return ImGui::Button(label, ImVec2(m_width, 0.0f));
			}

		private:
			int   m_perLine = 1;
			float m_width = 0.0f;
			int   m_drawn = 0;
		};

		void RevealInBrowser(std::string const& path) {
			if (auto browser = EditorManager::GetInstance().GetPanel<AssetBrowser>(ImguiPanels::ASSETBROWSER)) { browser->Reveal(path); }
		}
	}

	void DrawInspectorBar(World& world, Entity entity) {
		// A revert / apply picked last frame inside the component list (see g_pending).
		if (g_pending) {
			PendingAction const pending = std::move(*g_pending);
			g_pending.reset();
			if (pending.apply) {
				Entity const owner = pending.items.empty() ? kNullEntity : InstanceRootOf(world, pending.items.front().entity);
				ApplyToPrefab(world, owner, pending.items, pending.label);
			}
			else {
				Revert(world, pending.items, pending.label);
			}
		}

		PrefabInstance const* link = world.IsAlive(entity) ? world.Read<PrefabInstance>(entity) : nullptr;
		if (link == nullptr) { return; }

		bool const root = link->isRoot;
		std::uint64_t const prefab = link->prefabAsset;   // copied: Unpack / Revert below can move the component
		std::string const path = PrefabPathOf(prefab);

		ImGui::Separator();

		// Line 1: what this is part of. Its own line, so a long prefab name never pushes the
		// buttons off the panel.
		char const* const prefix = root ? "Prefab: " : "Part of prefab: ";
		if (path.empty()) {
			ImGui::TextColored(kMissingColour, "%smissing", prefix);
			ImGui::SetItemTooltip("Its .cprefab is no longer in the project (guid %s).",
			                      AssetRegistry::GuidToString(xresource::instance_guid{ prefab }).c_str());
		}
		else {
			ImGui::TextColored(kPrefabColour, "%s%s", prefix, PrefabNameOf(path).c_str());
			ImGui::SetItemTooltip("%s", path.c_str());
		}

		if (!root) {
			// A part: Select, and its own count. The full list, Apply and Unpack are on the
			// instance's root, as in Unity.
			std::size_t const count = DrawnOverrides(world, entity).size();
			ButtonRow row(1, kMinBarButtonWidth);
			ImGui::BeginDisabled(path.empty());
			if (row.Button("Select")) { RevealInBrowser(path); }
			ImGui::EndDisabled();
			ImGui::SetItemTooltip("Show the .cprefab in the Asset Browser");
			if (count > 0) { ImGui::TextDisabled("%zu override%s here (listed on the instance's root)", count, count == 1 ? "" : "s"); }
			return;
		}

		// Line 2+: the root's buttons, sharing the panel's width (wrapping onto a second row
		// when it is narrow): Select | Overrides (n) | Unpack. Apply lives in the Overrides menu.
		std::vector<Override> const all = InstanceOverrides(world, entity);
		ButtonRow row(3, kMinBarButtonWidth);

		ImGui::BeginDisabled(path.empty());
		if (row.Button("Select")) { RevealInBrowser(path); }
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Show the .cprefab in the Asset Browser");

		// Always clickable: with no overrides the menu still offers Apply to All Scenes (push the
		// prefab as it is now out to every scene).
		std::string const overridesLabel = all.empty() ? std::string("No Overrides") : "Overrides (" + std::to_string(all.size()) + ")";
		ImGui::BeginDisabled(path.empty());
		if (row.Button(overridesLabel.c_str())) { ImGui::OpenPopup("##PrefabOverrides"); }
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("What this instance changes from its prefab - apply or revert each, or all of them");

		bool const unpack = row.Button("Unpack");
		ImGui::SetItemTooltip("Make this instance ordinary entities, no longer linked to the prefab");

		bool openAllScenes = false;
		if (ImGui::BeginPopup("##PrefabOverrides")) {
			if (!all.empty()) {
				ImGui::TextDisabled("Differences from %s", PrefabNameOf(path).c_str());
				ImGui::Separator();
			}
			bool reverted = false;   // after one revert the list is stale: the rest shows next frame
			for (std::size_t i = 0; i < all.size() && !reverted; ++i) {   // "reverted": reverted or applied
				ImGui::PushID(static_cast<int>(i));
				std::string const who = all[i].kind == Override::Kind::AddedEntity ? std::string{} : NameOf(world, all[i].entity) + ": ";
				std::string const what = Describe(world, all[i]);
				ImGui::AlignTextToFramePadding();
				ImGui::Text("%s%s", who.c_str(), what.c_str());
				ImGui::SameLine();
				ImGui::BeginDisabled(!CanCreate());
				if (ImGui::SmallButton("Apply")) {
					ApplyToPrefab(world, entity, { all[i] }, "Apply " + what);
					reverted = true;
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				// An added child isn't deleted: it is detached and becomes a root (see Revert).
				bool const added = all[i].kind == Override::Kind::AddedEntity;
				if (ImGui::SmallButton(added ? "Detach" : "Revert")) {
					Revert(world, { all[i] }, (added ? "Detach " : "Revert ") + what);
					reverted = true;
				}
				if (added) { ImGui::SetItemTooltip("Move it out of the instance as a root object (world position kept)"); }
				ImGui::PopID();
			}
			if (all.empty()) { ImGui::TextDisabled("This instance matches its prefab."); }
			if (!reverted) {
				ImGui::Separator();
				ImGui::BeginDisabled(all.empty());
				if (ImGui::Button("Revert All")) {   // added children are detached, not deleted
					Revert(world, all, "Revert All (" + PrefabNameOf(path) + ")");
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndDisabled();
				ImGui::SameLine();

				ImGui::BeginDisabled(all.empty() || !CanCreate());
				if (ImGui::Button("Apply All")) {
					ApplyToPrefab(world, entity, all, "Apply All (" + PrefabNameOf(path) + ")");
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndDisabled();
				ImGui::SetItemTooltip("Write every override into the prefab and update its other instances in this scene (Ctrl+Z undoes it all)");
				ImGui::SameLine();

				ImGui::BeginDisabled(!CanCreate());
				if (ImGui::Button("Apply to All Scenes...")) {
					openAllScenes = true;   // the confirm box is opened below, outside this menu
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndDisabled();
				ImGui::SetItemTooltip("Apply, then update and save every other scene that uses the prefab");
			}
			ImGui::EndPopup();
		}

		// Opened out here, not from inside the menu, so it sits at the same ID level as the
		// BeginPopupModal in DrawAllScenesConfirm.
		if (openAllScenes) {
			g_otherScenes = ScenesUsing(prefab);
			ImGui::OpenPopup(kAllScenesPopup);
		}
		DrawAllScenesConfirm(world, entity, all, path);

		if (unpack) { Unpack(world, entity); }   // last: the link is gone afterwards
	}
}
