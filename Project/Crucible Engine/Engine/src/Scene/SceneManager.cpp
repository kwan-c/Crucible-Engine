

#include <memory>
#include <utility>
#include <system_error>

#include "Scene/SceneManager.h"
#include "Diagnostics/Logger.h"
#include "ECS/World.h"
#include "ECS/Components/Components.h"   // Transform, CameraComponent, Light
#include "Resources/ResourceManager.h"

namespace Crucible {
	SceneManager::SceneManager(World& world) : m_world(world) {}

	bool SceneManager::Save() {
		if (!HasPath())
		{
			CRU_USER_WARN("SceneManager: '{}' has never been saved - use SaveAs", m_scene.Name());
			return false;
		}

		return SaveAs(m_path);
	}

	bool SceneManager::SaveAs(const std::filesystem::path& file) {
		// The world holds runtime state until Stop restores the edited scene.
		if (m_playSession)
		{
			CRU_USER_WARN("SceneManager: cannot save '{}' while in play mode - press Stop first", m_scene.Name());
			return false;
		}

		for (const auto& callback : m_beforeSave) callback();   // OnBeforeSave

		const std::string oldName = m_scene.Name();
		m_scene.SetName(file.stem().string());

		if (!m_scene.Save(m_world, file))
		{
			m_scene.SetName(oldName);
			CRU_USER_ERROR("SceneManager: failed to save '{}' to {}", m_scene.Name(), file.string());
			return false;
		}

		m_path = file;
		m_dirty = false;
		CRU_USER_INFO("Saved scene '{}'", m_scene.Name());

		// TODO: once Core owns an AssetManager, call Refresh() here so a
		// brand-new file shows up in the Asset Browser without a restart.
		return true;
	}

	// =======================================================================
	//  Requests
	// =======================================================================
	void SceneManager::RequestNew() {
		m_pending = Pending::New;
		m_pendingPath.clear();
		m_pendingAsset = 0;
	}

	bool SceneManager::CreateSceneFile(const std::filesystem::path& file) {
		std::error_code ec;
		if (std::filesystem::exists(file, ec))
		{
			CRU_USER_WARN("SceneManager: cannot create '{}' - a file with that name already exists", file.string());
			return false;
		}

		// A throwaway world, so the open scene is never touched. World's
		// constructor registers every component, so Scene::Save writes exactly
		// what it would for the editor's world.
		auto world = std::make_unique<World>();
		PopulateDefaultScene(*world);

		// A fresh Scene: every entity gets a new guid, unique to this file.
		Scene scene;
		scene.SetName(file.stem().string());
		if (!scene.Save(*world, file))
		{
			CRU_USER_ERROR("SceneManager: failed to create scene file {}", file.string());
			return false;
		}
		return true;
	}

	bool SceneManager::RequestLoad(const std::filesystem::path& file) {
		std::error_code ec;
		if (!std::filesystem::is_regular_file(file, ec))
		{
			CRU_USER_ERROR("SceneManager: cannot load '{}' - file does not exist", file.string());
			return false;
		}

		m_pending = Pending::Load;
		m_pendingPath = file;
		m_pendingAsset = 0;
		return true;
	}

	bool SceneManager::RequestLoadAsset(std::uint64_t sceneAsset) {
		if (m_resources == nullptr)
		{
			CRU_USER_ERROR("SceneManager: cannot load scene {:X} - no ResourceManager to load it through", sceneAsset);
			return false;
		}
		// Checked now, so the caller gets feedback straight away rather than next frame.
		if (!m_resources->IsScene(sceneAsset))
		{
			CRU_USER_ERROR("SceneManager: cannot load {} - it is not a scene in the project",
						   m_resources->GetName(sceneAsset));
			return false;
		}

		m_pending = Pending::LoadAsset;
		m_pendingAsset = sceneAsset;
		m_pendingPath.clear();
		return true;
	}

	void SceneManager::ApplyPending() {
		// Take the request first, so a listener that requests another
		// change from inside NotifyChanged queues it for NEXT frame
		// instead of being wiped out here.
		const Pending   pending = std::exchange(m_pending, Pending::None);
		const std::filesystem::path  path = std::exchange(m_pendingPath, std::filesystem::path{});
		const std::uint64_t          asset = std::exchange(m_pendingAsset, std::uint64_t{ 0 });

		switch (pending)
		{
		case Pending::None:                     return;
		case Pending::New:  ApplyNew();         return;
		case Pending::Load: ApplyLoad(path);    return;
		case Pending::LoadAsset: ApplyLoadAsset(asset); return;
		}
	}

	void SceneManager::ApplyNew() {
		m_world.DestroyAllEntities();

		m_scene.Forget();
		m_scene.SetName("Untitled");

		PopulateDefaultScene(m_world);

		m_path.clear();
		m_dirty = false;   // the defaults are not an unsaved edit

		CRU_INFO("SceneManager: new scene");
		NotifyChanged();
	}

	void SceneManager::PopulateDefaultScene(World& world) {
		// Ground - a flattened built-in cube centred at the origin.
		//
		// Only the asset id is set, not the runtime handle: when meshAsset is non-zero,
		// RenderExtractSystem resolves it through Core's ResourceManager every frame (the
		// same way an entity loaded from a scene file draws). BuiltinId is static and the
		// id is stable across runs
		const Entity ground = world.CreateNamedEntity("Ground");
		MeshRenderer mr{};
		mr.meshAsset = ResourceManager::BuiltinId("Cube");
		mr.colour    = { .5f, .5f, .5f, .5f };   // default colour is (0,0,0,0)
		world.AddComponent<MeshRenderer>(ground, mr);

		if (Transform* t = world.Write<Transform>(ground)) { t->scale = Math::vec3{ 30.f, .5f, 30.f }; }

		// Directional light - same values as the Hierarchy's Create > Light >
		// Directional. Adding it takes over from the Renderer's built-in sun.
		const Entity sun = world.CreateNamedEntity("Directional Light");
		Light light{};
		light.type      = LightType::Directional;
		light.intensity = 1.f;
		world.AddComponent<Light>(sun, light);

		// A directional light's position is ignored; it shines along its local -Z. Pitch it
		// down 50 degrees so it lights the ground instead of skimming along it.
		if (Transform* t = world.Write<Transform>(sun))
		{
			t->positions = Math::vec3{ 100.f, 100.f, 100.f };   // only where the gizmo sits
			t->rotation  = glm::angleAxis(glm::radians(-50.f), Math::vec3{ 1.f, 0.f, 0.f });
		}
	}

	void SceneManager::ApplyLoad(const std::filesystem::path& file) {
		// Scene::Load validates the file before destroying anything, so a
		// false return means the world, the guid map and our path are all
		// still exactly as they were.
		bool loaded = false;
		{
			// Entities are created before their names are read, so a default handed out
			// mid-load could repeat a name later in the file. Instead, blank names (an
			// entity saved without one, or before EntityName existed) are filled when the
			// scope ends, after the highest "Entity N" the file holds.
			World::DeferredNaming loading(m_world);
			loaded = m_scene.Load(m_world, file);
		}
		if (!loaded)
		{
			CRU_USER_ERROR("SceneManager: failed to load '{}' - keeping '{}'", file.string(), m_scene.Name());
			return;
		}

		m_path = file;
		m_dirty = false;
		CRU_USER_INFO("Loaded scene '{}'", m_scene.Name());

		NotifyChanged();
	}

	void SceneManager::ApplyLoadAsset(std::uint64_t sceneAsset) {
		// Read through the ResourceManager first: if the scene can no longer be read (deleted
		// since the request), the open scene stays exactly as it is.
		std::string text;
		if (m_resources == nullptr || !m_resources->ReadScene(sceneAsset, text))
		{
			CRU_USER_ERROR("SceneManager: failed to load scene {:X} - keeping '{}'", sceneAsset, m_scene.Name());
			return;
		}

		// Where Save writes back to. Loading itself only needed the GUID.
		const std::filesystem::path file = m_resources->GetFilePath(sceneAsset);

		// As in ApplyLoad: Scene checks the text before destroying anything, and blank
		// names are filled once every entity exists.
		bool loaded = false;
		{
			World::DeferredNaming loading(m_world);
			loaded = m_scene.LoadFromText(m_world, text, file);
		}
		if (!loaded)
		{
			CRU_USER_ERROR("SceneManager: failed to load '{}' - keeping '{}'", file.string(), m_scene.Name());
			return;
		}

		m_path = file;
		m_dirty = false;
		CRU_USER_INFO("Loaded scene '{}'", m_scene.Name());

		NotifyChanged();
	}

	// =======================================================================
	//  Play mode
	// =======================================================================
	bool SceneManager::CaptureSnapshot(Snapshot& out) {
		Snapshot snapshot;
		if (!m_scene.CaptureSnapshot(m_world, snapshot.scene))
		{
			return false;   // Scene already logged why
		}

		snapshot.path  = m_path;
		snapshot.dirty = m_dirty;
		out = std::move(snapshot);
		return true;
	}

	bool SceneManager::RestoreSnapshot(const Snapshot& snapshot) {
		// Scene checks the snapshot before destroying anything, so a false
		// here with no "part-way" error means the world is untouched.
		bool restored = false;
		{
			// Same as ApplyLoad. The rescan at the end also gives runtime spawns' numbers back.
			World::DeferredNaming restoring(m_world);
			restored = m_scene.RestoreSnapshot(m_world, snapshot.scene);
		}
		if (!restored)
		{
			return false;
		}

		m_path  = snapshot.path;
		m_dirty = snapshot.dirty;
		return true;
	}

	void SceneManager::OnBeforeSave(std::function<void()> callback) {
		if (callback)
		{
			m_beforeSave.push_back(std::move(callback));
		}
	}

	void SceneManager::OnSceneChanged(std::function<void()> callback) {
		if (callback)
		{
			m_listeners.push_back(std::move(callback));
		}
	}

	void SceneManager::NotifyChanged() {
		for (const auto& listener : m_listeners)
		{
			listener();
		}
	}
}