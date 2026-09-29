#pragma once
/*!
* @file			ImGuiPanel_AssetBrowser.h
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
*	double-click   open folder (files: logs for now; later hand off to an editor per type)
*	drag           starts a drag-drop with EditorWidgets::kAssetPayload (virtual path)
*	right-click    context menu (Show in Explorer, Copy Path, Copy GUID, New Folder, Refresh)
*	Backspace      go to parent folder,  F5  refresh
*/

#include "ImGui/ImGuiManager.h"
#include "AssetPipe/Assetmanager.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Crucible {

	class AssetBrowser : public EditorPanel {
	public:
		// REQUIRED Overrides
		void Init() override;
		void Update(float dt) override;

		// Virtual path of the selected asset or folder ("" when nothing is selected).
		std::string const& GetSelected() const { return m_selected; }

		// Rescan the Assets folder (picks up files added / renamed / removed on disk).
		void Refresh();

	private:
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

		void NavigateTo(std::string const& folder);   // deferred until the end of the frame
		void CreateFolder();
		std::string AbsolutePathOf(std::string const& virtualPath) const;

		AssetManager m_assets;                  // the browser's own index (see note in Init)
		bool         m_ready = false;

		FolderNode        m_root;
		std::vector<Item> m_items;              // what the grid shows right now
		bool              m_listingDirty = true;
		bool              m_syncTree = true;    // open the tree down to the current folder next frame

		std::string                m_currentFolder;   // "" = Assets root
		std::optional<std::string> m_navigateTo;
		std::string                m_selected;

		char  m_search[128] = {};
		float m_tileSize = 72.0f;
		float m_treeWidth = 190.0f;
	};

}
