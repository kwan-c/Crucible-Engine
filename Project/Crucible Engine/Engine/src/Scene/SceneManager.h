#pragma once

#include <string>
#include <functional>
#include <filesystem>
#include <vector>

#include "EngineExport.h"
#include "Scene.h"

namespace Crucible {

	class World;

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
		 \brief  Ask for an empty, untitled scene at the next safe point.
		 \note   During a play session this is a runtime scene change (a
				 level switch): it is applied, and Stop still restores the
				 scene that was open at Play.
		 *********************************************************************/
		void RequestNew();

		/*********************************************************************
		 \brief  Ask for a scene file to be loaded at the next safe point.
		 \details
			A missing file is rejected here, so the caller gets feedback
			straight away rather than next frame.
		 \return false if the file does not exist (nothing is queued).
		 \note   During a play session this is a runtime scene change, as
				 for RequestNew.
		 *********************************************************************/
		bool RequestLoad(const std::filesystem::path& file);

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
		void SetPlaySessionActive(bool active) noexcept { m_playSession = active; }
		bool IsPlaySessionActive() const noexcept { return m_playSession; }


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

	private:
		enum class Pending { None, New, Load };

		// Creates a new empty scene, replacing the current one.
		void ApplyNew();
		// Loads a scene file, replacing the current one.
		void ApplyLoad(const std::filesystem::path& file);
		// Notifies every listener that the scene has changed. Called after ApplyNew or ApplyLoad.
		void NotifyChanged();

		World&								 m_world;
		Scene								 m_scene;
		std::filesystem::path				 m_path;
		bool								 m_dirty = false;
		bool								 m_playSession = false;   // Play..Stop: saving refused

		Pending								 m_pending = Pending::None;
		std::filesystem::path				 m_pendingPath;

		std::vector<std::function<void()>>	 m_listeners;
	};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
}