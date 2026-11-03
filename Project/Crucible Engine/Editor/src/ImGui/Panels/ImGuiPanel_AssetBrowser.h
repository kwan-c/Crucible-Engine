#pragma once
/*!
* @file			ImGuiPanel_AssetBrowser.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			17 September 2026
*
* @brief		The editor's Asset Browser (Unity's "Project" window).
*
* Left:  folder tree of the Assets folder.
* Right: the current folder's contents as a tile grid, with a breadcrumb bar,
*        search (searches every indexed asset), tile-size slider and a footer
*        showing the selection.
*
* Folders come from the disk (so empty folders still show up); files come from
* AssetManager's index, so .meta files are hidden and every file has a type. Companion files
* edited through another asset (a font's .fontsettings - the Font Importer) are hidden too.
* Files are labelled without their extension (the tile's badge shows it), over up to two lines;
* a longer name keeps its start and its END ("...BoldItalic"), so similar names stay apart.
* The full name is in the tooltip and the footer.
*
* Interaction:
*	click          select
*	double-click   folder: open it.  .scene: open the scene (same guard as File > Open).
*	               C# script (.cs): open that file in VS Code.  Anything else: logged.
*	drag           starts a drag-drop with EditorWidgets::kAssetPayload (virtual path)
*	drop entity    a Hierarchy entity dropped on the grid is saved as a .cprefab in the folder
*	               being viewed; dropped on a folder tile, in that folder (Prefabs/PrefabAuthoring.h)
*	right-click    context menu (Rename, Delete, Show in Explorer, Copy Path, Copy GUID, New Folder,
*	               Create Scene - inside Scenes/ only - Create C# Script - inside Scripts/ only -
*	               and Refresh)
*	Del            delete the selected file or folder. Asks first, listing any other asset that
*	               still uses it; on Windows it goes to the Recycle Bin, .meta and all
*	F2             rename the selection (a file's .meta moves with it, so its guid is kept)
*	Backspace      go to parent folder,  F5  refresh
*
* Uses Core's shared AssetManager (core.GetAssets()) - the same index the engine resolves
* asset ids through - so anything created or renamed here is visible everywhere at once.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/ImGuiManager.h"
#include "AssetPipe/Assetmanager.h"
#include "ECS/Core/ECSCore.h"   // Entity (Create Prefab drops)

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Crucible {

	class ECSAudioSystem;
	class SceneManager;

	class AssetBrowser : public EditorPanel {
	public:
		// The audio system plays previews of audio files: select one and the Inspector shows
		// Unity's clip preview (Play/Stop, Auto Play). Null is allowed: the browser works,
		// previews do nothing.
		AssetBrowser(AssetManager& assets, SceneManager& scenes, ECSAudioSystem* audio = nullptr)
			: m_assets(assets), m_scenes(scenes), m_audio(audio) {}

		// REQUIRED Overrides
		void Init() override;
		void Update(float dt) override;

		// Virtual path of the selected asset or folder ("" when nothing is selected).
		std::string const& GetSelected() const { return m_selected; }

		// Rescan the Assets folder (picks up files added / renamed / removed on disk).
		void Refresh();

		// Show an asset: clears the search, opens its folder, selects it and brings the browser
		// to the front. Used by the Inspector's prefab bar (Select).
		void Reveal(std::string const& virtualPath);

	private:
		bool IsRefreshAllowed() const;

		struct Item {
			std::string    name;          // file or folder name, e.g. "player.png"
			std::string    virtualPath;   // relative to the asset root, forward slashes
			std::string    badge;         // short label drawn on the tile, e.g. "PNG"
			bool           isFolder = false;
			AssetType      type = AssetType::Unknown;
			std::uintmax_t sizeBytes = 0;
		};

		struct FolderNode {
			std::string             name;
			std::string             virtualPath;   // "" for the root
			std::vector<FolderNode> children;
		};

		void RebuildFolderTree();
		void RebuildListing();
		void BuildFolderNode(FolderNode& node);

		void DrawToolbar();
		void DrawFolderTree(FolderNode const& node);
		void DrawGrid();
		void DrawTile(Item const& item);
		void DrawItemContextMenu(Item const& item);
		void DrawFooter();

		// Selection -> Inspector. An audio file shows the clip preview there; anything else
		// takes the preview back out, the way Unity's Inspector follows the selection.
		void ShowInInspector(Item const& item);
		void DrawAudioPreview(std::string const& name, bool loaded);   // loaded = false: the name and "could not load"

		void NavigateTo(std::string const& folder);   // deferred until the end of the frame
		void CreateFolder();
		std::string AbsolutePathOf(std::string const& virtualPath) const;

		// ---- Open (double-click) ----
		void OpenItem(Item const& item);

		// ---- Create Scene (only offered inside Scenes/) ----
		bool IsInScenesFolder(std::string const& folder) const;
		void CreateScene();

		// ---- Create State Graph / Behavior Graph (AI, any folder) ----
		// Writes a starter graph (Graph/GraphAssets.h) into the folder being viewed, then
		// starts a rename, as Create Scene does.
		void CreateGraphAsset(bool stateGraph);

		// ---- Create C# Script (only enabled inside Scripts/, the folder the build compiles) ----
		// Asks for the class name in a modal, then writes <name>.cs from the EntityScript
		// template into the folder being viewed (Scripting/ScriptAuthoring.h).
		bool IsInScriptsFolder(std::string const& folder) const;
		void BeginCreateScript();
		void DrawCreateScriptModal();

		// ---- Rename (F2 / right-click) ----
		// The field is drawn over the tile's label; the rename itself happens after the frame's
		// drawing (ApplyPendingRename), so the item list never changes mid-loop.
		void BeginRename(std::string const& virtualPath);
		void DrawRenameField(ImVec2 labelPos, float width);
		void ApplyPendingRename();
		bool RenameOnDisk(std::string const& from, std::string const& newName, std::string& outVirtualPath);

		// ---- Delete (Del / right-click; any file or folder) ----
		// Asks first in a modal. The delete itself happens after the frame's drawing
		// (ApplyPendingDelete), like rename, so the item list never changes mid-loop.
		void BeginDelete(std::string const& virtualPath);
		void DrawDeleteConfirm();
		void ApplyPendingDelete();
		bool DeleteOnDisk(std::string const& virtualPath);
		// Files outside `virtualPath` that refer to it - or, for a folder, to anything in it - by
		// guid or by path. fileCount receives how many files are being deleted.
		std::vector<std::string> FindReferences(std::string const& virtualPath, bool isFolder, std::size_t& fileCount) const;

		// ---- Create Prefab (a Hierarchy entity dropped on the grid or on a folder tile) ----
		// Saved after the frame's drawing (ApplyPendingPrefab), like rename, because saving
		// refreshes the index and the item list.
		struct PendingPrefab { Entity dragged; std::string folder; };
		std::optional<PendingPrefab> m_pendingPrefab;
		// Lets the item just drawn accept a Hierarchy entity; the prefab is saved in `folder`.
		void AcceptEntityDrop(std::string const& folder);
		void ApplyPendingPrefab();

		AssetManager&   m_assets;               // Core's shared index (core.GetAssets())
		SceneManager&   m_scenes;               // to open scenes, and to follow a rename of the open one
		bool            m_ready = false;
		bool            m_appWasFocused = true;   // the editor window had the focus last frame (refresh when it comes back)
		ECSAudioSystem* m_audio = nullptr;      // not owned; Core owns it and outlives every panel
		bool            m_audioInInspector = false;   // the Inspector is showing our clip preview
		bool            m_autoPlay = false;           // Unity's Auto Play toggle: play on select. Off, as in Unity

		FolderNode        m_root;
		std::vector<Item> m_items;              // what the grid shows right now
		bool              m_listingDirty = true;
		bool              m_syncTree = true;    // open the tree down to the current folder next frame

		std::string                m_currentFolder;   // "" = Assets root
		std::optional<std::string> m_navigateTo;
		std::string                m_selected;

		std::string m_renaming;                 // virtual path being renamed ("" = none)
		char        m_renameBuffer[256] = {};
		bool        m_renameFocus = false;      // give the field keyboard focus on its first frame
		int         m_renameIdleFrames = 0;     // frames the field has existed without focus
		std::optional<std::pair<std::string, std::string>> m_pendingRename;   // { from, typed name }

		std::string                m_deleteTarget;              // virtual path the confirm modal asks about ("" = none)
		bool                       m_openDeleteConfirm = false; // open the modal next time it is drawn
		std::optional<std::string> m_pendingDelete;             // confirmed; applied after drawing
		// Gathered when the confirm modal opens (BeginDelete) and shown in it.
		struct DeleteInfo {
			bool                     isFolder  = false;
			std::size_t              fileCount = 0;   // files that will go (a folder's contents; 1 for a file)
			std::vector<std::string> usedBy;          // other assets that still reference them
		};
		DeleteInfo                 m_deleteInfo;

		bool        m_openCreateScript = false;   // open the Create C# Script modal next time it is drawn
		std::string m_createScriptFolder;         // virtual path the script will be created in
		char        m_createScriptName[128] = {};
		char        m_createScriptNamespace[128] = "Crucible.Scripts";
		std::string m_createScriptError;          // shown in the modal until the next attempt

		char  m_search[128] = {};
		float m_tileSize = 72.0f;   // wide enough for ~12 characters a line
		static constexpr float m_labelLines = 2.0f;   // a tile's name gets up to two lines (see TileLabel in the .cpp)
		float m_treeWidth = 190.0f;
	};

}
