#pragma once
/*!
* @file			PrefabStage.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			24 November 2026
*
* @brief		Prefab Mode: edit a .cprefab on its own, then come back to the scene.
*
* Works like Play / Stop. Opening snapshots the scene (unsaved edits included), puts its undo
* history aside and replaces the world's contents with the prefab; exiting restores all of it.
* Every panel keeps working on the same World.
*
* The prefab's entities are NOT linked instances (no PrefabInstance), so nothing about them is
* locked. Which file entry each one came from is kept here, by undo key (EntryIdOf): unlike a
* scene id, that survives deleting a part and undoing the delete.
*
* Save writes a temporary file, checks it loads, then replaces the .cprefab, so a failed save
* leaves the prefab as it was. On Exit the open scene's instances update (their overrides kept),
* and if a save happened and other scenes use the prefab, a box offers to update them too.
*
* While a prefab is open:
*	- saving, opening and creating scenes are refused (SceneManager::SetSceneLocked), so the
*	  prefab can never be written over the scene's file;
*	- Play is refused, and so are Create Prefab, Apply, placing prefabs and project-wide tag
*	  changes, which all assume the world holds the scene.
*
* Exit with unsaved edits asks Save / Discard / Cancel. The Scene view frames the prefab on open
* and returns to where it was on Exit. The preview light and ground grid are render-only: they
* are not entities, so they can never end up in the file.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ECS/Core/ECSCore.h"   // Entity

#include <cstdint>
#include <string>

namespace Crucible::PrefabStage {

	// True from the frame a prefab has opened until the frame it has closed.
	bool IsActive();

	// Asks to open the .cprefab at `virtualPath`, at the next safe point. Refused (with a
	// message) during Play or while another prefab is open.
	void RequestOpen(std::string const& virtualPath);

	// Asks to go back to the scene at the next safe point, first asking Save / Discard / Cancel
	// if there are unsaved edits. Does nothing if no prefab is open.
	void RequestExit();

	// True once after that box was cancelled, so a pending "close the editor" can be dropped.
	bool ConsumeExitCancelled();

	// Writes the open prefab to its file, keeping the ids its instances link to. If the file
	// changed on disk since it was opened or saved, the first call warns and writes nothing; a
	// second call overwrites it. Returns true if the prefab on disk now matches the stage.
	bool Save();

	// Why Play is refused right now; "" when no prefab is open.
	std::string PlayBlockReason();

	// The open prefab's virtual path ("" if none) and root entity (kNullEntity if none).
	std::string const& OpenPath();
	Entity Root();

	// The file entry `entity` came from; 0 for an entity added since, or when no prefab is open.
	std::uint64_t EntryIdOf(Entity entity);

	// The "Prefab Mode" bar and the "update other scenes" box. Call every frame, before the
	// dockspace; draws nothing when neither is showing.
	void DrawBar();
}
