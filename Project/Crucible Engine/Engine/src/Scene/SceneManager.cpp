

#include <utility>
#include <system_error>

#include "Scene/SceneManager.h"
#include "Diagnostics/Logger.h"
#include "ECS/World.h"

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
		return true;
	}

	void SceneManager::ApplyPending() {
		// Take the request first, so a listener that requests another
		// change from inside NotifyChanged queues it for NEXT frame
		// instead of being wiped out here.
		const Pending   pending = std::exchange(m_pending, Pending::None);
		const std::filesystem::path  path = std::exchange(m_pendingPath, std::filesystem::path{});

		switch (pending)
		{
		case Pending::None:                     return;
		case Pending::New:  ApplyNew();         return;
		case Pending::Load: ApplyLoad(path);    return;
		}
	}

	void SceneManager::ApplyNew() {
		m_world.DestroyAllEntities();

		m_scene.Forget();
		m_scene.SetName("Untitled");

		m_path.clear();
		m_dirty = false;

		CRU_INFO("SceneManager: new scene");
		NotifyChanged();
	}

	void SceneManager::ApplyLoad(const std::filesystem::path& file) {
		// Scene::Load validates the file before destroying anything, so a
		// false return means the world, the guid map and our path are all
		// still exactly as they were.
		if (!m_scene.Load(m_world, file))
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
		if (!m_scene.RestoreSnapshot(m_world, snapshot.scene))
		{
			return false;
		}

		m_path  = snapshot.path;
		m_dirty = snapshot.dirty;
		return true;
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