/*************************************************************************
* \file:         TagReferences.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          13 November 2026
* \version       1.0
* \brief
*                Finds every place a tag is used across the project, and
*                renames or deletes a tag everywhere with a backup and
*                rollback (the Tag Manager's Rename / Replace / Reset).
*
* \details
*                Scan looks at:
*                  - the open scene as it is in memory (unsaved edits
*                    included): entities with the tag;
*                  - every .scene and .cprefab under Assets: "EntityTag":
*                    { "tag": ... } entries (the open scene's own file too);
*                  - every JSON asset (.scene, .cprefab, .json, .mat,
*                    .animcontroller, .stategraph, .behaviorgraph,
*                    .fontsettings) for saved tag SETTINGS - a member named
*                    tag/tags/...Tag/...Tags/tagXxx (e.g. a navigation
*                    surface's "obstacleTags") whose value names the tag;
*                  - every .cs under Assets for the exact string literal
*                    "Tag" (report only: scripts are never rewritten, and a
*                    tag built at run time can't be found).
*                A file that can't be read, or isn't valid JSON (empty files
*                count as empty), makes the scan INCOMPLETE: no project-wide
*                change is allowed then.
*
*                Retag rewrites the scenes/prefabs that use the tag, then
*                the tag list, then the open scene's entities:
*                  1. scan again (fresh), refuse if incomplete, if a tag
*                     setting names the tag (those aren't rewritten), or if
*                     a file to change is read-only / locked;
*                  2. build every new file in memory and check it parses
*                     back to the old document with only the tags changed;
*                  3. copy every original (and Tags.json) into a backup
*                     folder with a manifest - nothing is written if that
*                     fails;
*                  4. replace each file (temp file + rename, so a file is
*                     never half written); a file changed on disk since the
*                     scan, or a failed write, rolls back every file already
*                     replaced;
*                  5. update and save the tag list; if that fails the files
*                     and the list are rolled back too;
*                  6. retag the open scene's entities (in memory - the open
*                     scene's FILE was rewritten in step 4, its unsaved
*                     edits are never saved here).
*                This is NOT atomic across files: a crash between steps can
*                leave some files rewritten. The backup folder (kept after
*                success too) and its manifest.txt are the recovery path.
*                Files are written in the engine's own JSON style
*                (PrettyWriter, the file's own line endings and BOM), so an
*                engine-written file changes only in the tag values.
*
* \history
*    -v1.0       Scan, Retag (backup, rollback)
*
**************************************************************************/
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Crucible
{
	class World;
	class TagRegistry;

	namespace TagReferences
	{
		/// A scene or prefab on disk with entities using the tag.
		struct FileUsage
		{
			std::filesystem::path path;           // absolute
			std::string           display;        // relative to Assets, e.g. "Scenes/Level1.scene"
			std::size_t           entities = 0;   // "EntityTag" entries with the tag
			bool                  prefab = false; // .cprefab (otherwise .scene)
			bool                  openScene = false;   // the scene open in the editor
		};

		/// An exact string literal of the tag in a script (report only).
		struct ScriptUsage
		{
			std::string file;   // relative to Assets
			std::size_t line = 0;
			std::string text;   // the line, trimmed
		};

		/// A saved tag setting naming the tag (e.g. a navigation surface's obstacle tags).
		struct SettingUsage
		{
			std::string file;   // relative to Assets, or "open scene"
			std::string key;    // "FlowFieldSurface.obstacleTags"
		};

		/*********************************************************************
		 \brief  Where a tag is used. Built by Scan; read by the Tag Manager.
		 *********************************************************************/
		struct Report
		{
			std::string               tag;
			std::size_t               liveEntities = 0;   // open scene, in memory
			std::vector<FileUsage>    files;              // scenes and prefabs on disk
			std::vector<ScriptUsage>  scripts;            // "tag" literals in Assets/**/*.cs
			std::vector<SettingUsage> settings;           // saved tag settings naming it
			std::vector<std::string>  navigationScenes;   // scenes with a FlowFieldSurface ("open scene" for the live one)
			std::vector<std::string>  problems;           // why the scan is incomplete / a file can't be changed
			std::size_t               scannedFiles = 0;

			bool Complete() const { return problems.empty(); }
			/// Entities or settings anywhere use it (scripts don't count: they are only reported).
			bool InUse() const { return liveEntities > 0 || !files.empty() || !settings.empty(); }
			std::size_t SceneFiles() const;    // .scene files using it (the open scene's file included)
			std::size_t PrefabFiles() const;
			/// Entities using it: the open scene from memory, every other file from disk.
			std::size_t Entities() const;
		};

		/*********************************************************************
		 \brief  Finds every use of tag (see the file header).
		 \param world          The open scene's world.
		 \param assetsRoot     The Assets folder; empty -> incomplete scan.
		 \param openSceneFile  The open scene's file (empty if unsaved).
		 \param registryFile   Tags.json - skipped (it is the list itself).
		 \param forWrite       Also check that every file to change, and
							   registryFile, can be written (Retag does).
		 *********************************************************************/
		Report Scan(World& world, std::string_view tag, std::filesystem::path const& assetsRoot,
		            std::filesystem::path const& openSceneFile, std::filesystem::path const& registryFile,
		            bool forWrite = false);

		/// Rename: every use of `from` becomes `to`, and `from` is renamed in the list.
		/// Delete: every use of `from` becomes `to` (an existing tag, or "Untagged"),
		/// and `from` is removed from the list.
		enum class Operation { Rename, Delete };

		struct Options
		{
			std::filesystem::path assetsRoot;
			std::filesystem::path openSceneFile;
			std::filesystem::path registryFile;   // Tags.json, saved with the new list
			std::filesystem::path backupRoot;     // a folder per operation is made inside it
			// Called before each file is replaced; returning false fails that write.
			// For tests and tools that need to check the rollback; normally empty.
			std::function<bool(std::filesystem::path const&)> beforeWrite;
		};

		struct Result
		{
			bool                     ok = false;
			std::string              message;               // what happened, for the user
			std::vector<std::string> details;               // problems, or files changed
			std::filesystem::path    backupFolder;          // empty if nothing was backed up
			std::vector<std::filesystem::path> changedFiles;   // absolute; scenes and prefabs
			std::size_t              liveEntitiesChanged = 0;
			std::size_t              reformattedFiles = 0;  // files not in the engine's JSON style (rewritten in it)
		};

		/*********************************************************************
		 \brief  Renames or deletes `from` across the project (see the file
				 header for the steps and guarantees).
		 \return ok == false means nothing was changed, or every change was
				 rolled back (message says which, and where the backup is if a
				 restore itself failed).
		 \note   The caller handles the editor: not during Play, refresh the
				 asset index and prefab caches for changedFiles, and drop
				 undo steps that refer to the old tag.
		 *********************************************************************/
		Result Retag(World& world, TagRegistry& registry, Options const& options,
		             std::string const& from, std::string const& to, Operation operation);
	}
}
