#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <filesystem>
#include <vector>

#include "EngineExport.h"
#include "Scene.h"

namespace Crucible {

	class World;
	class ResourceManager;

	#ifdef _MSC_VER
	#pragma warning(push)
	#pragma warning(disable : 4251)   // STL members across the DLL boundary
	#endif

	class ENGINE_API SceneManager
	{
	public:
		explicit SceneManager(World& world);

		// Holds a reference, and MSVC instantiates copy operations for
		// dllexport classes - so forbid them outright.
		SceneManager(const SceneManager&) = delete;
		SceneManager& operator=(const SceneManager&) = delete;

		/*********************************************************************
		 \brief  Save to the current file.
		 \return false if there is no current file (caller should SaveAs),
				 the write failed, or a play session is running.
		 *********************************************************************/
		bool Save(); // to current path; false if none

		/*********************************************************************
		 \brief  Save to a new file, which becomes the current file.
		 \details
			The scene name is taken from the filename, so the name inside
			the file always matches the file it lives in.
		 \note   Refused (false) while a play session is running: the world
				 then holds runtime state, which Stop throws away.
		 *********************************************************************/
		bool SaveAs(const std::filesystem::path& file);


		/*********************************************************************
		 \brief  Ask for a new, untitled scene (holding the default objects
				 - see PopulateDefaultScene) at the next safe point.
		 \note   During a play session this is a runtime scene change (a
				 level switch): it is applied, and Stop still restores the
				 scene that was open at Play.
		 *********************************************************************/
		void RequestNew(bool bypassLoadGate = false);

		/*********************************************************************
		 \brief  Ask for a scene file to be loaded at the next safe point.
		 \details
			A missing file is rejected here, so the caller gets feedback
			straight away rather than next frame.
		 \return false if the file does not exist (nothing is queued).
		 \note   During a play session this is a runtime scene change, as
				 for RequestNew.
		 *********************************************************************/
		bool RequestLoad(const std::filesystem::path& file, bool bypassLoadGate = false);

		/*********************************************************************
		 \brief  Ask for a scene to be loaded, by its asset GUID, at the next
				 safe point.
		 \details
			How the game loads scenes: the file is found and read through the
			ResourceManager (which reads it through the AssetManager), so a
			scene renamed or moved in the project still loads. A GUID that is
			not a scene in the project is rejected here, straight away.
			RequestLoad(path) stays for editor file operations (File > Open).
		 \param[in] sceneAsset The .scene's asset GUID (from its .meta).
		 \return false if there is no ResourceManager (SetResources) or the
				 GUID is not a scene - nothing is queued.
		 \note   During a play session this is a runtime scene change, as
				 for RequestNew.
		 *********************************************************************/
		bool RequestLoadAsset(std::uint64_t sceneAsset, bool bypassLoadGate = false);

		/*********************************************************************
		 \brief  The ResourceManager scenes are loaded through. Core sets it
				 once the ResourceManager exists (it is created after this).
		 *********************************************************************/
		void SetResources(ResourceManager* resources) noexcept { m_resources = resources; }

		/// The ResourceManager set by SetResources, or nullptr (scripts' Scene.BeginPreload uses it).
		ResourceManager* Resources() const noexcept { return m_resources; }

		/*********************************************************************
		 \brief  Write a new .scene file holding the default objects, without
				 opening it.
		 \details
			Builds the defaults (see PopulateDefaultScene) in a throwaway
			World and saves that, so the open scene, its dirty flag and the
			undo history are untouched. The scene name is the file's stem,
			as for SaveAs. Used by the Asset Browser's Create Scene, so a
			scene made there starts out the same as File > New Scene.
		 \return false if the file already exists (nothing is overwritten)
				 or the write failed.
		 *********************************************************************/
		bool CreateSceneFile(const std::filesystem::path& file);

		/*********************************************************************
		 \brief  Apply a pending New or Load, if there is one.
		 \details
			Call exactly once per frame, from Core::Run, before systems
			update. If several requests arrive in one frame, the last wins.
		 *********************************************************************/
		void ApplyPending();                           // call once per frame, before systems

		// ===================================================================
		//  Play mode
		// ===================================================================

		/*********************************************************************
		 \brief  Everything Stop needs to put the open scene back as it was
				 when Play was pressed.
		 *********************************************************************/
		struct Snapshot
		{
			SceneSnapshot         scene;          //!< entities, guids, component values, scene name
			std::filesystem::path path;           //!< CurrentPath() at capture
			bool                  dirty = false;  //!< IsDirty() at capture

			bool IsValid() const noexcept { return scene.IsValid(); }
		};

		/*********************************************************************
		 \brief  Copy the open scene - unsaved edits included - into memory.
		 \return false (out untouched) if the copy failed or did not read
				 back. Nothing is written to disk.
		 *********************************************************************/
		bool CaptureSnapshot(Snapshot& out);

		/*********************************************************************
		 \brief  Put the open scene back to a snapshot: entities and their
				 serialized component values, guids, name, path and dirty
				 flag.
		 \return false if the snapshot was refused; it is checked before the
				 world is touched, so the world is then unchanged.
		 \details
			Every Entity handle held before the call is stale afterwards;
			find an entity again through Current().EntityOf(guid).
			OnSceneChanged listeners are NOT called - this is the same scene
			coming back, not a different one (Core reports it through
			EngineEvents::PlayStateChanged instead).
		 *********************************************************************/
		bool RestoreSnapshot(const Snapshot& snapshot);

		/*********************************************************************
		 \brief  Core marks the play session here (Play to Stop) so saving
				 can be refused while the world holds runtime state.
		 *********************************************************************/
		void SetPlaySessionActive(bool active) noexcept
		{
			m_playSession = active;
			if (!active)
			{
				m_persistent.clear();   // persistence lasts one play session
				m_gateClaimed = false;  // and so does the loading screen's claim
				ClearHeld();
			}
		}
		bool IsPlaySessionActive() const noexcept { return m_playSession; }

		// ===================================================================
		//  DontDestroyOnLoad
		// ===================================================================

		/*********************************************************************
		 \brief  Unity's DontDestroyOnLoad: keep a root entity, and
				 everything below it, through runtime scene changes.
		 \details
			A Load or New during play then replaces every other entity and
			leaves this one alive, with the same handle and its scripts
			still running. It lasts until the entity is destroyed or the
			play session ends; Stop still restores the scene as it was at
			Play.
		 \return false (with a warning) outside a play session, for a dead
				 entity, or for one that has a parent - as in Unity, only
				 a root can be marked.
		 *********************************************************************/
		bool DontDestroyOnLoad(Entity entity);

		/*********************************************************************
		 \brief  Whether a scene change would keep this entity: it, or one
				 of its ancestors, was marked with DontDestroyOnLoad.
		 *********************************************************************/
		bool IsPersistent(Entity entity) const;

		// ===================================================================
		//  Load gate (the loading screen)
		// ===================================================================

		/// The prefab the engine spawns at the start of every play session as the loading
		/// screen (Prefabs/LoadingScreen.cprefab, a path inside Assets / Resources).
		static constexpr const char* kLoadingScreenPrefab = "Prefabs/Loading Screen/LoadingScreen.cprefab";

		/*********************************************************************
		 \brief  A loading screen takes over runtime scene changes (true), or
				 gives them back (false).
		 \details
			While claimed, a New / Load / LoadAsset during play is HELD instead
			of queued: the loading screen sees it (HasHeldLoad), preloads its
			assets with a progress bar, then lets it through (ReleaseHeldLoad).
			Every way of changing scene goes through this - Scene.Load,
			Scene.Reload and a UIButton's LoadScene - unless the caller passes
			bypassLoadGate. Releasing the claim lets a held load through, so a
			destroyed loading screen never leaves the game stuck. Lasts one
			play session; outside play nothing is ever held.
		 *********************************************************************/
		void SetLoadGate(bool claimed);
		bool IsLoadGateClaimed() const noexcept { return m_gateClaimed; }

		/// Whether a scene change is waiting for the loading screen.
		bool HasHeldLoad() const noexcept { return m_held != Pending::None; }

		/// Goes up by one every time a new scene change is held (a newer request replaces an older one).
		std::uint32_t HeldLoadSerial() const noexcept { return m_heldSerial; }

		/*********************************************************************
		 \brief  The held scene's file text, for preloading its assets.
		 \return false if nothing is held, the held change is a New scene
				 (nothing to preload), or the file can't be read.
		 *********************************************************************/
		bool ReadHeldScene(std::string& text) const;

		/// Lets the held scene change through: it is applied at the next frame's safe point.
		void ReleaseHeldLoad();


		// ===================================================================
		//  States of the SceneManager
		// ===================================================================
		bool HasPath() const							 { return !m_path.empty(); }
		const std::filesystem::path& CurrentPath() const { return m_path; }
		const std::string& CurrentName() const			 { return m_scene.Name(); }

		/*********************************************************************
		 \brief  The open scene's file was renamed or moved on disk (the Asset
				 Browser's Rename): save to the new place from now on, and show
				 the new name.
		 \details Without this, Ctrl+S would recreate the old file name - a
				  second copy of the scene with no .meta. Touches neither the world
				  nor the dirty flag, and does not notify listeners: the scene
				  itself did not change, so the undo history stays.
		 *********************************************************************/
		void RelocateCurrentFile(const std::filesystem::path& file)
		{
			m_path = file;
			m_scene.SetName(file.stem().string());
		}

		/*********************************************************************
		 \brief  The open scene's file was deleted on disk (the Asset Browser's
				 Delete): keep the scene open, but with no file behind it.
		 \details The entities stay as they are, so nothing is lost. With no
				  path, the next Save asks for a name (SaveAs) instead of quietly
				  recreating the deleted file, and the scene is marked dirty
				  because it now exists only in memory. Like RelocateCurrentFile,
				  listeners are not notified: the world did not change, so the
				  undo history stays.
		 *********************************************************************/
		void DetachCurrentFile()
		{
			m_path.clear();
			m_dirty = true;
		}

		/*********************************************************************
		  \brief  Whether the scene has changed since it was last saved/loaded.
		  \details
			 Set by hand through MarkDirty from editor actions. Not derived
			 from component change ticks - runtime systems such as physics
			 write every frame, which would make the scene permanently dirty.
		  *********************************************************************/
		bool IsDirty() const { return m_dirty; }
		void MarkDirty()	 { m_dirty = true; }

		/*********************************************************************
		 \brief  The Scene object for the open scene (guid lookups etc.).
		 *********************************************************************/
		const Scene& Current() const { return m_scene; }

		/*********************************************************************
		 \brief  The open scene's guid for an entity, issued now if it has
		         none (Scene::EnsureId). The one write to the guid map that
		         editor tools need - the Inspector's entity picker - without
		         handing out the whole Scene as mutable.
		 *********************************************************************/
		xresource::instance_guid EnsureId(Entity entity) { return m_scene.EnsureId(entity); }

		// ===================================================================
		//  Listeners
		// ===================================================================

		/*********************************************************************
		 \brief  Register a callback run after the world has been replaced.
		 \details
			Fires after a successful New or Load, never after Save. Every
			Entity handle held before the callback is stale.
		 \note   Not called by RestoreSnapshot (play-mode Stop) - see there.
		 *********************************************************************/
		void OnSceneChanged(std::function<void()> callback);

		/*********************************************************************
		 \brief  Register a callback run at the start of every Save / SaveAs,
				 just before the file is written (after the play-mode check).
		 \details
			For editor tools that keep data on entities which must be up to
			date in the file - the prefab override lists, for one.
		 *********************************************************************/
		void OnBeforeSave(std::function<void()> callback);

	private:
		enum class Pending { None, New, Load, LoadAsset };

		// Creates a new scene holding the default objects, replacing the current one.
		void ApplyNew();
		// Adds the objects every new scene starts with (a camera and a
		// directional light). The single place to change those defaults:
		// both ApplyNew and CreateSceneFile go through it.
		static void PopulateDefaultScene(World& world);
		// Loads a scene file, replacing the current one.
		void ApplyLoad(const std::filesystem::path& file);
		// Loads a scene by its asset GUID, read through the ResourceManager.
		void ApplyLoadAsset(std::uint64_t sceneAsset);
		// Notifies every listener that the scene has changed. Called after ApplyNew or ApplyLoad.
		void NotifyChanged();
		// The live DontDestroyOnLoad roots a scene change keeps (none outside play).
		std::vector<Entity> KeepRoots();
		// Whether a request now should be held for the loading screen.
		bool ShouldHold(bool bypassLoadGate) const noexcept { return !bypassLoadGate && m_playSession && m_gateClaimed; }
		// Holds a request for the loading screen (replacing an older held one).
		void Hold(Pending kind, std::filesystem::path path, std::uint64_t asset);
		void ClearHeld() noexcept
		{
			m_held = Pending::None;
			m_heldPath.clear();
			m_heldAsset = 0;
		}

		World&								 m_world;
		Scene								 m_scene;
		std::filesystem::path				 m_path;
		bool								 m_dirty = false;
		bool								 m_playSession = false;   // Play..Stop: saving refused

		Pending								 m_pending = Pending::None;
		std::filesystem::path				 m_pendingPath;
		std::uint64_t						 m_pendingAsset = 0;     // Pending::LoadAsset
		ResourceManager*					 m_resources = nullptr;  // SetResources; scenes by GUID load through it

		std::vector<std::function<void()>>	 m_listeners;
		std::vector<std::function<void()>>	 m_beforeSave;   // OnBeforeSave
		std::vector<Entity>					 m_persistent;   // DontDestroyOnLoad roots; may hold dead handles

		bool								 m_gateClaimed = false;   // SetLoadGate: a loading screen takes scene changes
		Pending								 m_held = Pending::None;  // a scene change waiting for the loading screen
		std::filesystem::path				 m_heldPath;
		std::uint64_t						 m_heldAsset = 0;
		std::uint32_t						 m_heldSerial = 0;
	};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
}