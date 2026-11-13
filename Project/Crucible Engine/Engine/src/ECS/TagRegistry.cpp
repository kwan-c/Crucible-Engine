/*************************************************************************
* \file:         TagRegistry.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          13 November 2026
* \version       1.0
* \brief
*                Implementation of TagRegistry - see TagRegistry.h.
*
* \history
*    -v1.0       initial registry, Tags.json load/save, ReportUnknown
*
**************************************************************************/

#include "TagRegistry.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include "AssetPipe/AssetPaths.h"
#include "Components/Components.h"   // kUntaggedTag
#include <Diagnostics/Logger.h>

namespace Crucible
{
	namespace
	{
		// Inside the Assets folder (Resources in a packaged game), next to EngineConfig.json.
		constexpr const char* kTagsRelativePath = "Config/Tags.json";

		bool IsSpace(char c) noexcept
		{
			return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
		}

		void SetError(std::string* error, std::string message)
		{
			if (error) *error = std::move(message);
		}
	}

	const char* ToString(TagError error) noexcept
	{
		switch (error)
		{
		case TagError::Ok:               return "Ok";
		case TagError::Empty:            return "a tag needs a name";
		case TagError::TooLong:          return "tag names are at most 64 characters";
		case TagError::InvalidCharacter: return "tag names can't start or end with a space or contain control characters";
		case TagError::Reserved:         return "\"Untagged\" is built in: it can't be added, renamed or deleted";
		case TagError::Duplicate:        return "that tag already exists";
		case TagError::NotFound:         return "no such tag";
		case TagError::TooMany:          return "too many tags";
		default:                         return "unknown";
		}
	}

	TagRegistry& TagRegistry::Get()
	{
		static TagRegistry registry;
		static std::once_flag loaded;
		std::call_once(loaded, [] { registry.LoadProjectFile(); });   // errors are logged inside
		return registry;
	}

	std::filesystem::path TagRegistry::ProjectFile()
	{
		if (AssetRoot().empty()) return {};
		return AssetPath(kTagsRelativePath);
	}

	TagError TagRegistry::ValidateName(std::string_view name) noexcept
	{
		if (std::all_of(name.begin(), name.end(), IsSpace)) return TagError::Empty;   // "" too
		if (name.size() > kMaxTagLength) return TagError::TooLong;
		if (IsSpace(name.front()) || IsSpace(name.back())) return TagError::InvalidCharacter;
		for (const char c : name)
		{
			const auto byte = static_cast<unsigned char>(c);
			if (byte < 0x20 || byte == 0x7F) return TagError::InvalidCharacter;   // UTF-8 (>= 0x80) is fine
		}
		return TagError::Ok;
	}

	TagRegistry::TagRegistry()
	{
		m_tags.emplace_back(kUntaggedTag);
	}

	// --- reading --------------------------------------------------------------------------------

	int TagRegistry::Find(std::string_view tag) const noexcept
	{
		for (std::size_t i = 0; i < m_tags.size(); ++i)
		{
			if (m_tags[i] == tag) return static_cast<int>(i);
		}
		return -1;
	}

	bool TagRegistry::IsDefined(std::string_view tag) const
	{
		std::shared_lock lock(m_mutex);
		return Find(tag) >= 0;
	}

	std::vector<std::string> TagRegistry::Tags() const
	{
		std::shared_lock lock(m_mutex);
		return m_tags;
	}

	std::size_t TagRegistry::Count() const
	{
		std::shared_lock lock(m_mutex);
		return m_tags.size();
	}

	int TagRegistry::IndexOf(std::string_view tag) const
	{
		std::shared_lock lock(m_mutex);
		return Find(tag);
	}

	std::uint64_t TagRegistry::Revision() const noexcept
	{
		return m_revision.load(std::memory_order_acquire);
	}

	// --- changing -------------------------------------------------------------------------------

	TagError TagRegistry::CheckNewName(std::string_view tag) const noexcept
	{
		if (tag == kUntaggedTag) return TagError::Reserved;
		const TagError name = ValidateName(tag);
		if (name != TagError::Ok) return name;
		if (Find(tag) >= 0) return TagError::Duplicate;
		return TagError::Ok;
	}

	void TagRegistry::Changed() noexcept
	{
		m_revision.fetch_add(1, std::memory_order_acq_rel);
		std::lock_guard report(m_reportMutex);
		m_reported.clear();   // a tag removed again is reported again
	}

	TagError TagRegistry::Add(std::string_view tag)
	{
		{
			std::unique_lock lock(m_mutex);
			const TagError check = CheckNewName(tag);
			if (check != TagError::Ok) return check;
			if (m_tags.size() >= kMaxTags) return TagError::TooMany;
			m_tags.emplace_back(tag);
		}
		Changed();
		return TagError::Ok;
	}

	TagError TagRegistry::Insert(std::size_t index, std::string_view tag)
	{
		{
			std::unique_lock lock(m_mutex);
			const TagError check = CheckNewName(tag);
			if (check != TagError::Ok) return check;
			if (m_tags.size() >= kMaxTags) return TagError::TooMany;
			index = std::clamp<std::size_t>(index, 1, m_tags.size());   // "Untagged" stays first
			m_tags.emplace(m_tags.begin() + static_cast<std::ptrdiff_t>(index), tag);
		}
		Changed();
		return TagError::Ok;
	}

	TagError TagRegistry::Remove(std::string_view tag)
	{
		{
			std::unique_lock lock(m_mutex);
			if (tag == kUntaggedTag) return TagError::Reserved;
			const int index = Find(tag);
			if (index < 0) return TagError::NotFound;
			m_tags.erase(m_tags.begin() + index);
		}
		Changed();
		return TagError::Ok;
	}

	TagError TagRegistry::Rename(std::string_view from, std::string_view to)
	{
		{
			std::unique_lock lock(m_mutex);
			if (from == kUntaggedTag) return TagError::Reserved;
			const int index = Find(from);
			if (index < 0) return TagError::NotFound;
			if (from == to) return TagError::Ok;   // nothing to do
			const TagError check = CheckNewName(to);
			if (check != TagError::Ok) return check;
			m_tags[static_cast<std::size_t>(index)].assign(to);
		}
		Changed();
		return TagError::Ok;
	}

	void TagRegistry::Reset()
	{
		{
			std::unique_lock lock(m_mutex);
			m_tags.assign(1, std::string(kUntaggedTag));
		}
		Changed();
	}

	// --- persistence ----------------------------------------------------------------------------

	bool TagRegistry::LoadFromString(const std::string& json, std::string* error)
	{
		rapidjson::Document document;
		document.Parse(json.c_str(), json.size());
		if (document.HasParseError())
		{
			SetError(error, std::string("not valid JSON (") + rapidjson::GetParseError_En(document.GetParseError()) +
				" at offset " + std::to_string(document.GetErrorOffset()) + ")");
			return false;
		}
		if (!document.IsObject())
		{
			SetError(error, "the file must be a JSON object");
			return false;
		}
		const auto tagsMember = document.FindMember("tags");
		if (tagsMember == document.MemberEnd() || !tagsMember->value.IsArray())
		{
			SetError(error, "\"tags\" must be an array of strings");
			return false;
		}
		const auto version = document.FindMember("version");
		if (version != document.MemberEnd() && version->value.IsInt() && version->value.GetInt() > kFileVersion)
		{
			CRU_WARN("TagRegistry: Tags.json version {} is newer than this engine's ({}) - reading the tag names only",
				version->value.GetInt(), kFileVersion);
		}

		// Built aside, so a refused file leaves the current list as it was.
		std::vector<std::string> tags{ std::string(kUntaggedTag) };
		for (const auto& entry : tagsMember->value.GetArray())
		{
			if (!entry.IsString())
			{
				CRU_WARN("TagRegistry: skipping a tag that is not a string in Tags.json");
				continue;
			}
			const std::string_view name(entry.GetString(), entry.GetStringLength());
			if (name == kUntaggedTag) continue;   // already first
			const TagError check = ValidateName(name);
			if (check != TagError::Ok)
			{
				CRU_WARN("TagRegistry: skipping tag '{}' in Tags.json - {}", name, ToString(check));
				continue;
			}
			if (std::find(tags.begin(), tags.end(), name) != tags.end())
			{
				CRU_WARN("TagRegistry: skipping duplicate tag '{}' in Tags.json", name);
				continue;
			}
			if (tags.size() >= kMaxTags)
			{
				CRU_WARN("TagRegistry: Tags.json has more than {} tags - the rest are ignored", kMaxTags);
				break;
			}
			tags.emplace_back(name);
		}

		{
			std::unique_lock lock(m_mutex);
			m_tags = std::move(tags);
		}
		Changed();
		return true;
	}

	bool TagRegistry::Load(const std::filesystem::path& file, std::string* error)
	{
		std::ifstream in(file, std::ios::binary);
		if (!in)
		{
			SetError(error, "can't open " + PathToUtf8(file));
			return false;
		}
		std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
		if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);   // UTF-8 BOM
		return LoadFromString(text, error);
	}

	std::string TagRegistry::ToJson() const
	{
		const std::vector<std::string> tags = Tags();

		rapidjson::StringBuffer buffer;
		rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
		writer.SetIndent(' ', 2);
		writer.StartObject();
		writer.Key("version");
		writer.Int(kFileVersion);
		writer.Key("tags");
		writer.StartArray();
		for (const std::string& tag : tags)
		{
			writer.String(tag.c_str(), static_cast<rapidjson::SizeType>(tag.size()));
		}
		writer.EndArray();
		writer.EndObject();
		return std::string(buffer.GetString(), buffer.GetSize()) + "\n";
	}

	bool TagRegistry::Save(const std::filesystem::path& file, std::string* error) const
	{
		if (file.empty())
		{
			SetError(error, "no asset folder to save Tags.json in");
			return false;
		}
		const std::string text = ToJson();

		std::error_code ec;
		if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);

		// Written beside it first, then swapped in: a failed write never leaves half a file.
		std::filesystem::path temporary = file;
		temporary += ".tmp";
		{
			std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
			if (!out || !out.write(text.data(), static_cast<std::streamsize>(text.size())))
			{
				SetError(error, "can't write " + PathToUtf8(temporary));
				return false;
			}
		}
		std::filesystem::rename(temporary, file, ec);
		if (ec)
		{
			std::filesystem::remove(temporary, ec);
			SetError(error, "can't replace " + PathToUtf8(file) + " (is it read-only or open elsewhere?)");
			return false;
		}
		return true;
	}

	bool TagRegistry::LoadProjectFile(std::string* error)
	{
		const std::filesystem::path file = ProjectFile();
		std::error_code ec;
		if (file.empty() || !std::filesystem::exists(file, ec))
		{
			Reset();   // no file yet: just "Untagged"
			return true;
		}

		std::string reason;
		if (!Load(file, &reason))
		{
			CRU_USER_ERROR("[Tags] {} could not be read ({}) - keeping the tags already loaded", PathToUtf8(file), reason);
			SetError(error, std::move(reason));
			return false;
		}
		return true;
	}

	bool TagRegistry::SaveProjectFile(std::string* error) const
	{
		const std::filesystem::path file = ProjectFile();
		std::string reason;

		// A file that is there but broken holds someone's hand edits: refuse rather than replace them.
		std::error_code ec;
		if (!file.empty() && std::filesystem::exists(file, ec))
		{
			TagRegistry probe;
			if (!probe.Load(file, &reason))
			{
				reason = PathToUtf8(file) + " can't be read (" + reason + ") - fix or delete it, then Reload; not overwriting it";
				CRU_USER_ERROR("[Tags] saving the project's tags failed: {}", reason);
				SetError(error, std::move(reason));
				return false;
			}
		}

		if (!Save(file, &reason))
		{
			CRU_USER_ERROR("[Tags] saving the project's tags failed: {}", reason);
			SetError(error, std::move(reason));
			return false;
		}
		return true;
	}

	// --- diagnostics ----------------------------------------------------------------------------

	void TagRegistry::ReportUnknown(std::string_view tag) const
	{
		if (IsDefined(tag)) return;
		{
			std::lock_guard report(m_reportMutex);
			if (!m_reported.emplace(tag).second) return;   // already said
		}
		CRU_USER_WARN("[Tags] an entity uses the tag '{}', which is not in Config/Tags.json - it is kept as it is. "
			"Add it back or pick another tag (Inspector > Tag > Manage Tags...).", tag);
	}
}
