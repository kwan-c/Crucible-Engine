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
		 \return false if there is no current file (caller should SaveAs)
				 or the write failed.
		 *********************************************************************/
		bool Save(); // to current path; false if none

		/*********************************************************************
		 \brief  Save to a new file, which becomes the current file.
		 \details
			The scene name is taken from the filename, so the name inside
			the file always matches the file it lives in.
		 *********************************************************************/
		bool SaveAs(const std::filesystem::path& file);
		

		/*********************************************************************
		 \brief  Ask for an empty, untitled scene at the next safe point.
		 *********************************************************************/
		void RequestNew();

		/*********************************************************************
		 \brief  Ask for a scene file to be loaded at the next safe point.
		 \details
			A missing file is rejected here, so the caller gets feedback
			straight away rather than next frame.
		 \return false if the file does not exist (nothing is queued).
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
		//  States of the SceneManager
		// ===================================================================
		bool HasPath() const							 { return !m_path.empty(); }
		const std::filesystem::path& CurrentPath() const { return m_path; }
		const std::string& CurrentName() const			 { return m_scene.Name(); }

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

		Pending								 m_pending = Pending::None;
		std::filesystem::path				 m_pendingPath;

		std::vector<std::function<void()>>	 m_listeners;
	};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
}