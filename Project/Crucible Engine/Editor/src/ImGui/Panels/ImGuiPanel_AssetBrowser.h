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
* AssetManager's index, so .meta files are hidden and every file has a type.
*
* Interaction:
*	click          select
*	double-click   folder: open it.  .scene: open the scene (same guard as File > Open).
*	               script (.lua/.cs): open that file in VS Code.  Anything else: logged.
*	drag           starts a drag-drop with EditorWidgets::kAssetPayload (virtual path)
*	right-click    context menu (Rename, Show in Explorer, Copy Path, Copy GUID, New Folder,
*	               Create Scene - inside Scenes/ only -, Create Script - inside Scripts/
*	               only - and Refresh)
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

		// ---- Create Script (only offered inside Scripts/) ----
		// An empty .lua file, then straight into rename like Create Scene.
		bool IsInScriptsFolder(std::string const& folder) const;
		void CreateScript();

		// ---- Rename (F2 / right-click) ----
		// The field is drawn over the tile's label; the rename itself happens after the frame's
		// drawing (ApplyPendingRename), so the item list never changes mid-loop.
		void BeginRename(std::string const& virtualPath);
		void DrawRenameField(ImVec2 labelPos, float width);
		void ApplyPendingRename();
		bool RenameOnDisk(std::string const& from, std::string const& newName, std::string& outVirtualPath);

		// ---- Delete (Del / right-click; scene files only) ----
		// Asks first in a modal. The delete itself happens after the frame's drawing
		// (ApplyPendingDelete), like rename, so the item list never changes mid-loop.
		void BeginDelete(std::string const& virtualPath);
		void DrawDeleteConfirm();
		void ApplyPendingDelete();
		bool DeleteOnDisk(std::string const& virtualPath);

		AssetManager&   m_assets;               // Core's shared index (core.GetAssets())
		SceneManager&   m_scenes;               // to open scenes, and to follow a rename of the open one
		bool            m_ready = false;
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

		char  m_search[128] = {};
		float m_tileSize = 72.0f;
		float m_treeWidth = 190.0f;
	};

}
