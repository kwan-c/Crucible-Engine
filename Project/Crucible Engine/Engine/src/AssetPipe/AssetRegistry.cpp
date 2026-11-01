#include "AssetRegistry.h"
#include "AssetManager.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace
{
	constexpr const char* META_EXTENSION = ".meta";

	constexpr std::size_t HASH_BLOCK_BYTES = 64 * 1024;
};

namespace Crucible
{
	// helpers

	bool AssetRegistry::IsMetaFile(const fs::path& path)
	{
		return path.extension() == META_EXTENSION;
	}

	fs::path AssetRegistry::MetaPathFor(const fs::path& assetPath)
	{
		fs::path meta = assetPath;
		meta += META_EXTENSION;
		return meta;
	}

	std::uint64_t AssetRegistry::HashFileContents(const fs::path& path)
	{
		std::ifstream file{ path, std::ios::binary };
		if (!file.is_open())
		{
			return 0;
		}

		std::uint64_t hash = 1469598103934665603ull;   // FNV-1a 64-bit offset basis
		std::vector<char> buffer(HASH_BLOCK_BYTES);

		// The hash ignores line endings: every "\r\n" is hashed as a single "\n".
		//
		// Git rewrites line endings on checkout depending on each person's settings
		// (core.autocrlf, .gitattributes), so the SAME text file - a .scene, a .mat,
		// an .obj - can be CRLF on one teammate's machine and LF on another's. Hashing
		// raw bytes gave those two copies different hashes, each engine "corrected"
		// the committed .meta to its own value, and the .meta flipped on every pull.
		//
		// Binary files are unaffected in practice: git never converts them, so every
		// machine has the same bytes and therefore the same hash. A file with no "\r"
		// at all hashes exactly as it did before this change. A lone "\r" (not
		// followed by "\n") is still hashed as itself.
		const auto mix = [&hash](unsigned char byte)
		{
			hash ^= byte;
			hash *= 1099511628211ull;   // FNV prime
		};

		bool pendingCR = false;   // a '\r' whose next byte has not been seen yet (it may be in the next block)

		while (file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || file.gcount() > 0)
		{
			const std::streamsize read = file.gcount();
			for (std::streamsize i = 0; i < read; ++i)
			{
				const auto byte = static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]);

				if (pendingCR)
				{
					pendingCR = false;
					if (byte != '\n')
					{
						mix('\r');   // a lone CR: keep it
					}
				}

				if (byte == '\r')
				{
					pendingCR = true;   // decide once the next byte is known
					continue;
				}

				mix(byte);
			}
		}

		if (pendingCR)
		{
			mix('\r');   // the file ended on a lone CR
		}

		return hash;
	}

	bool AssetRegistry::ReadMeta(const fs::path& path, AssetMeta& out)
	{
		std::ifstream file{ path };
		if (!file.is_open())
		{
			return false;
		}

		out = AssetMeta{};

		std::string line;
		while (std::getline(file, line))
		{
			const std::size_t split = line.find('=');
			if (split == std::string::npos)
			{
				continue;
			}

			const std::string key = line.substr(0, split);
			const std::string value = line.substr(split + 1);

			if (key == "guid")
			{
				out.guid = GuidFromString(value);
			}
			else if (key == "hash")
			{
				out.contenthash = std::strtoull(value.c_str(), nullptr, 16);
			}
			// "path" is a human-readable hint only and is deliberately ignored.
			// "time" was written by older builds; it is ignored too, and disappears
			// the next time this .meta is legitimately rewritten.
		}

		return out.guid.m_Value != 0;
	}

	bool AssetRegistry::WriteMeta(const fs::path& path,
		const AssetMeta& meta,
		const std::string& hintPath)
	{
		std::ofstream file{ path, std::ios::trunc };
		if (!file.is_open())
		{
			std::cout << "[AssetRegistry] Could not write '" << path.string() << "'.\n ";
			return false;
		}

		file << "guid=" << GuidToString(meta.guid) << "\n"
			<< std::hex << std::uppercase
			<< "hash=" << meta.contenthash << "\n"
			<< std::dec
			<< "path=" << hintPath << "\n";

		return file.good();
	}

	// Guid text

	std::string AssetRegistry::GuidToString(xresource::instance_guid id)
	{
		char buffer[32] = {};
		std::snprintf(buffer, sizeof(buffer), "%016llX",
			static_cast<unsigned long long>(id.m_Value));
		return std::string(buffer);
	}

	xresource::instance_guid AssetRegistry::GuidFromString(const std::string& text)
	{
		return xresource::instance_guid{ std::strtoull(text.c_str(), nullptr, 16) };
	}

	// Reconcile

	void AssetRegistry::Clear()
	{
		m_guidToPath.clear();
	}

	const std::string* AssetRegistry::GetPath(xresource::instance_guid id) const
	{
		if (id.m_Value == 0)
			return nullptr;

		const auto it = m_guidToPath.find(id);
		return (it == m_guidToPath.end()) ? nullptr : &it->second;
	}

	AssetRegistry::Report AssetRegistry::Reconcile(std::vector<AssetEntry>& entries, const fs::path& assetRoot,
		bool removeOrphans)

	{
		Report report;
		std::error_code ec;

		if (assetRoot.empty())
		{
			return report;
		}

		std::unordered_map<std::string, AssetMeta> orphans;

		fs::recursive_directory_iterator it{ assetRoot, fs::directory_options::skip_permission_denied, ec };

		if (ec)
		{
			std::cout << "[AssetRegistry] Could not scan for .meta files: "
				<< ec.message() << "\n";
			return report;
		}

		const fs::recursive_directory_iterator end{};
		for (; it != end; it.increment(ec))
		{
			if (ec) { ec.clear(); continue; }

			const fs::path& p = it->path();
			if (!IsMetaFile(p) || !it->is_regular_file(ec))
			{
				ec.clear();
				continue;
			}

			AssetMeta meta;
			if (ReadMeta(p, meta))
			{
				orphans.emplace(AssetManager::NormalizeSeparators(p.string()), meta);
			}
		}

		// Pass 1: every asset looks for its own .meta
		std::vector<AssetEntry*> newcomers;

		for (AssetEntry& entry : entries)
		{
			const fs::path metaPath = MetaPathFor(fs::path{ entry.absolutePath });
			const std::string metaKey = AssetManager::NormalizeSeparators(metaPath.string());

			const auto found = orphans.find(metaKey);
			if (found == orphans.end())
			{
				newcomers.push_back(&entry);
				continue;
			}

			entry.guid = found->second.guid;
			++report.is_known;

			// With no stored timestamp, hashing is the only way to tell whether
			// the file changed. The .meta is rewritten only when the bytes really
			// differ - a git checkout gives every file a fresh mtime but identical
			// content, so nothing is written and nobody sees a spurious diff.
			const std::uint64_t hash = HashFileContents(fs::path{ entry.absolutePath });

			if (hash == 0)
			{
				// Unreadable right now (locked by another program, for instance).
				// Keep the recorded hash rather than overwriting a good one with a
				// failure, which would break rename detection for this asset.
				entry.contentHash = found->second.contenthash;
			}
			else
			{
				entry.contentHash = hash;

				if (hash != found->second.contenthash)
				{
					WriteMeta(metaPath, { entry.guid, entry.contentHash }, entry.virtualPath);
					++report.is_edited;
				}
			}

			orphans.erase(found);
		}

		for (AssetEntry* entry : newcomers)
		{
			entry->contentHash = HashFileContents(fs::path{ entry->absolutePath });

			std::string matchKey;
			int matchCount = 0;

			if (entry->contentHash != 0)
			{
				for (const auto& [key, meta] : orphans)
				{
					if (meta.contenthash == entry->contentHash)
					{
						matchKey = key;
						if (++matchCount > 1)
						{
							break;
						}
					}
				}
			}

			if (matchCount == 1)
			{
				entry->guid = orphans[matchKey].guid;

				fs::remove(fs::path{ matchKey }, ec);
				ec.clear();
				orphans.erase(matchKey);
				++report.is_renamed;

				std::cout << "[AssetRegistry] Renamed -> " << entry->virtualPath
					<< " (guid " << GuidToString(entry->guid) << " kept)\n";
			}
			else
			{
				entry->guid = xresource::instance_guid::GenerateGUIDCopy();
				++report.is_new;
			}

			WriteMeta(MetaPathFor(fs::path{ entry->absolutePath }),
				{ entry->guid, entry->contentHash }, entry->virtualPath);
		}

		// Pass 3: whatever is left is described a deleted asset
		for (const auto& [key, meta] : orphans)
		{
			if (!removeOrphans)
			{
				break;
			}
			(void)meta;
			fs::remove(fs::path{ key }, ec);
			ec.clear();
			++report.is_removed;
		}

		// Building the guid -> path map (a later folder takes over a guid, see the header).
		// A compiled file always keeps the guid it shares with its source, whichever is
		// seen first - in an installed game both sit in Resources (Cook copies some sources).
		m_guidToPath.reserve(m_guidToPath.size() + entries.size());
		for (const AssetEntry& entry : entries)
		{
			if (entry.guid.m_Value == 0)
			{
				continue;
			}
			const auto existing = m_guidToPath.find(entry.guid);
			const bool keepExisting = existing != m_guidToPath.end()
				&& AssetManager::IsCompiledExtension(AssetManager::ToLower(fs::path{ existing->second }.extension().string()))
				&& !AssetManager::IsCompiledExtension(entry.extension);
			if (!keepExisting)
			{
				m_guidToPath.insert_or_assign(entry.guid, entry.virtualPath);
			}
		}

		if (report.AnyChange())
		{
			std::cout << "[AssetRegistry] " << report.is_known << " known";
			if (report.is_new) std::cout << ", " << report.is_new << " new";
			if (report.is_renamed) std::cout << ", " << report.is_renamed << " renamed";
			if (report.is_edited) std::cout << ", " << report.is_edited << " edited";
			if (report.is_removed) std::cout << ", " << report.is_removed << " orphan(s) removed";
			std::cout << "\n";
		}

		return report;
	}
}
