#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <xresource_guid.h>

#include "EngineExport.h"

namespace Crucible {
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL / xresource members in exported types; Engine.dll and its clients share one toolchain
#endif
	
	struct AssetEntry;

	struct ENGINE_API AssetMeta
	{
		xresource::instance_guid guid;
		std::uint64_t contenthash = 0;
		// No timestamp is stored. A timestamp changes whenever git rewrites the
		// working tree (checkout, pull, stash), which made every .meta get
		// rewritten on the next launch and show up as a diff for the whole team.
		// The hash alone decides whether an asset changed.
	};

	class ENGINE_API AssetRegistry
	{
	public:

		struct Report
		{
			int is_known = 0;	// Check that asset had a valid .meta
			int is_new = 0;		// Check if the asset is new
			int is_renamed = 0; // Check if the asset is renamed
			int is_edited = 0;	// Check if the asset is recently edited
			int is_removed = 0; // Orphaned .meta files deleted

			bool AnyChange() const 
			{
				return is_new || is_renamed || is_edited || is_removed; 
			}
		};

		// Gives every entry (all found under assetRoot) its guid and adds them to the
		// guid -> path map. Call Clear() first, then once per folder: a guid already in the
		// map is taken over by the later folder (Resources after Assets, so a compiled
		// .cmodel wins over the .glb it was made from) - except that a compiled file is never
		// replaced by a source file with the same guid. removeOrphans = false keeps the
		// .meta files of files left out of `entries` (a folder that is only partly indexed).
		Report Reconcile(std::vector<AssetEntry>& entries, const std::filesystem::path& assetRoot,
			bool removeOrphans = true);

		void Clear();

		const std::string* GetPath(xresource::instance_guid id) const;

		std::size_t GetCount() const 
		{
			return m_guidToPath.size();
		}

		static bool IsMetaFile(const std::filesystem::path& path);

		static std::filesystem::path MetaPathFor(const std::filesystem::path& assetPath);

		static bool ReadMeta(const std::filesystem::path& path, AssetMeta& out);

		static bool WriteMeta(const std::filesystem::path& path, 
			const AssetMeta& meta, 
			const std::string& hintPath);

		static std::uint64_t HashFileContents(const std::filesystem::path& path);

		static std::string GuidToString(xresource::instance_guid id);

		static xresource::instance_guid GuidFromString(const std::string& text);

	private:
		std::unordered_map<xresource::instance_guid, std::string> m_guidToPath;
	};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
