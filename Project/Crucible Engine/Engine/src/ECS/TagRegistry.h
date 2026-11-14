/*************************************************************************
* \file:         TagRegistry.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          13 November 2026
* \version       1.0
* \brief
*                The project's list of entity tags (Unity's Tag Manager):
*                which tags exist, shared by every scene and prefab.
*
* \details
*                Entities hold their tag in the mandatory EntityTag
*                component (Components.h); this list only says which tags
*                are DEFINED. World::SetEntityTag refuses a tag that is not
*                in it. The list lives in Assets/Config/Tags.json (cooked to
*                Resources/Config/Tags.json for a packaged game):
*
*                    {
*                      "version": 1,
*                      "tags": [ "Untagged", "Player", "Obstacle" ]
*                    }
*
*                Rules:
*                  - "Untagged" (kUntaggedTag) always exists, always first,
*                    and cannot be added again, renamed or removed.
*                  - Tags are case-sensitive and unique (exact match).
*                  - A name is 1..kMaxTagLength bytes, with no control
*                    characters and no leading/trailing whitespace.
*                  - Removing or renaming a tag here never touches entities:
*                    an entity keeps a tag that is no longer defined, and
*                    ReportUnknown logs it (once per name) when it is
*                    loaded. The editor's Tag Manager decides what happens
*                    to the entities (Editor/src/Tags/TagEditor.h).
*
*                Changes are in memory until SaveProjectFile(); the editor
*                saves after each change. Thread-safe: every member locks.
*
* \history
*    -v1.0       initial registry, Tags.json load/save, ReportUnknown
*
**************************************************************************/
#pragma once

#include "EngineExport.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Crucible
{
	/*************************************************************************
	 \brief  Why a TagRegistry change (or a proposed tag name) was refused.
	 *************************************************************************/
	enum class TagError : std::uint8_t
	{
		Ok,
		Empty,              // "" or only whitespace
		TooLong,            // more than TagRegistry::kMaxTagLength bytes
		InvalidCharacter,   // a control character, or leading/trailing whitespace
		Reserved,           // "Untagged" cannot be added again, renamed or removed
		Duplicate,          // already defined (exact, case-sensitive)
		NotFound,           // not defined
		TooMany             // TagRegistry::kMaxTags reached
	};

	/*************************************************************************
	 \brief  Human-readable reason for a TagError (for the editor and logs).
	 *************************************************************************/
	ENGINE_API const char* ToString(TagError error) noexcept;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)
#endif

	/*************************************************************************
	 \brief
		An ordered list of defined tags, "Untagged" first. One project-wide
		instance (Get()); standalone instances are for tests and tools.
	 \note
		Non-copyable (owns its locks).
	 *************************************************************************/
	class ENGINE_API TagRegistry
	{
	public:
		static constexpr std::size_t kMaxTagLength = 64;     // bytes (UTF-8)
		static constexpr std::size_t kMaxTags      = 1024;   // "Untagged" included
		static constexpr int         kFileVersion  = 1;      // "version" in Tags.json

		/*********************************************************************
		 \brief  The project's registry. Loads ProjectFile() on first use (a
				 missing file leaves just "Untagged").
		 *********************************************************************/
		static TagRegistry& Get();

		/*********************************************************************
		 \brief  Assets/Config/Tags.json (Resources/... in a packaged game),
				 or an empty path if no asset folder was found.
		 *********************************************************************/
		static std::filesystem::path ProjectFile();

		/*********************************************************************
		 \brief  Whether name may be a tag at all (ignores what is defined).
		 \return Ok, Empty, TooLong or InvalidCharacter.
		 *********************************************************************/
		static TagError ValidateName(std::string_view name) noexcept;

		/*********************************************************************
		 \brief  A registry holding only "Untagged".
		 *********************************************************************/
		TagRegistry();
		TagRegistry(const TagRegistry&) = delete;
		TagRegistry& operator=(const TagRegistry&) = delete;

		// ---- reading ----------------------------------------------------------

		/// Whether tag is defined (exact, case-sensitive). No allocation.
		bool IsDefined(std::string_view tag) const;
		/// Every defined tag, in order ("Untagged" first). A copy: safe to keep.
		std::vector<std::string> Tags() const;
		/// Number of defined tags (>= 1).
		std::size_t Count() const;
		/// Position of tag in Tags(), or -1 if it is not defined.
		int IndexOf(std::string_view tag) const;
		/// Bumped by every change (add, remove, rename, load, reset): cheap "did it change?".
		std::uint64_t Revision() const noexcept;

		// ---- changing (in memory; SaveProjectFile() writes them) ---------------

		/// Appends tag. Ok, or the reason nothing changed.
		TagError Add(std::string_view tag);
		/// Inserts tag at index (clamped to 1..Count(): "Untagged" stays first). For undo of Remove.
		TagError Insert(std::size_t index, std::string_view tag);
		/// Removes tag. Entities that use it keep it (now undefined).
		TagError Remove(std::string_view tag);
		/// Renames in place (same position). Entities that use `from` keep it (now undefined).
		TagError Rename(std::string_view from, std::string_view to);
		/// Back to just "Untagged".
		void Reset();

		// ---- persistence ------------------------------------------------------

		/*********************************************************************
		 \brief  Replaces the list with Tags.json text.
		 \return false (list unchanged, *error set) if it is not JSON, not an
				 object, or has no "tags" array. Entries that are not valid
				 names, or repeat an earlier one, are skipped with a warning;
				 "Untagged" is put first whether or not the file has it.
		 *********************************************************************/
		bool LoadFromString(const std::string& json, std::string* error = nullptr);

		/// LoadFromString on a file's contents. false (unchanged) if it can't be read.
		bool Load(const std::filesystem::path& file, std::string* error = nullptr);

		/// The list as Tags.json text (pretty-printed).
		std::string ToJson() const;

		/// Writes ToJson() to file, creating folders as needed.
		bool Save(const std::filesystem::path& file, std::string* error = nullptr) const;

		/*********************************************************************
		 \brief  Load(ProjectFile()). A missing file is not an error: the list
				 becomes just "Untagged" (the editor writes the file on the
				 first change).
		 *********************************************************************/
		bool LoadProjectFile(std::string* error = nullptr);

		/// Save(ProjectFile()). false if there is no asset folder, it can't be written, or the
		/// file there can't be parsed (hand edits gone wrong are never overwritten).
		bool SaveProjectFile(std::string* error = nullptr) const;

		// ---- diagnostics ------------------------------------------------------

		/*********************************************************************
		 \brief  Logs a warning if tag is not defined - once per tag name until
				 the list next changes. Called by World when an entity's tag is
				 restored (scene/prefab load, undo, paste).
		 *********************************************************************/
		void ReportUnknown(std::string_view tag) const;

	private:
		// The index of tag in m_tags, or -1. Caller holds m_mutex.
		int Find(std::string_view tag) const noexcept;
		// Validation shared by Add/Insert/Rename's new name. Caller holds m_mutex.
		TagError CheckNewName(std::string_view tag) const noexcept;
		// After every change: new revision, unknown tags may be reported again.
		void Changed() noexcept;

		mutable std::shared_mutex m_mutex;
		std::vector<std::string>  m_tags;            // m_tags[0] == kUntaggedTag
		std::atomic<std::uint64_t> m_revision{ 1 };   // read without the lock

		mutable std::mutex                      m_reportMutex;
		mutable std::unordered_set<std::string> m_reported;   // undefined tags already logged
	};

#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
