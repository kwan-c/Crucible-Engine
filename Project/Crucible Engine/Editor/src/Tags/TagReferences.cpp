/*************************************************************************
* \file:         TagReferences.cpp
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          13 November 2026
* \version       1.0
* \brief
*                Implementation of TagReferences - see TagReferences.h.
*
* \history
*    -v1.0       Scan, Retag (backup, rollback)
*
**************************************************************************/
#include "Tags/TagReferences.h"

#include "AssetPipe/AssetPaths.h"
#include "ECS/World.h"
#include "ECS/TagRegistry.h"
#include "ECS/Components/Components.h"
#include "Json/JsonWrapper.h"
#include "Scene/EntityIO.h"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace Crucible
{
	namespace TagReferences
	{
		namespace
		{
			namespace fs = std::filesystem;

			constexpr std::size_t kMaxScriptHits = 200;   // enough to act on; the rest is counted
			constexpr std::string_view kBom = "\xEF\xBB\xBF";

			// ---- files ------------------------------------------------------------------

			bool ReadBytes(fs::path const& file, std::string& out)
			{
				std::ifstream in(file, std::ios::binary);
				if (!in) { return false; }
				out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
				return !in.bad();
			}

			// Writes beside the file, then swaps it in: the file is either old or new, never half.
			bool ReplaceFile(fs::path const& file, std::string const& bytes, std::string& why)
			{
				fs::path temporary = file;
				temporary += ".tagtmp";
				{
					std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
					if (!out || !out.write(bytes.data(), static_cast<std::streamsize>(bytes.size())) || !out.flush()) {
						why = "could not write " + PathToUtf8(temporary);
						std::error_code ignored;
						fs::remove(temporary, ignored);
						return false;
					}
				}
				std::error_code ec;
				fs::rename(temporary, file, ec);
				if (ec) {
					why = "could not replace " + PathToUtf8(file) + " (" + ec.message() + ")";
					fs::remove(temporary, ec);
					return false;
				}
				return true;
			}

			// Read-only, locked by another program, or gone: can't be changed safely.
			bool CanWrite(fs::path const& file, std::string& why)
			{
				std::error_code ec;
				fs::file_status const status = fs::status(file, ec);
				if (ec || !fs::is_regular_file(status)) { why = "is missing"; return false; }
				if ((status.permissions() & fs::perms::owner_write) == fs::perms::none) { why = "is read-only"; return false; }
				std::fstream probe(file, std::ios::in | std::ios::out | std::ios::binary);   // opens, changes nothing
				if (!probe) { why = "can't be opened for writing (open in another program?)"; return false; }
				return true;
			}

			std::string Relative(fs::path const& file, fs::path const& root)
			{
				std::string text = PathToUtf8(file.lexically_relative(root));
				std::replace(text.begin(), text.end(), '\\', '/');
				return text;
			}

			bool SameFile(fs::path const& a, fs::path const& b)
			{
				if (a.empty() || b.empty()) { return false; }
				std::error_code ec;
				return fs::equivalent(a, b, ec);
			}

			std::string Lower(std::string text)
			{
				std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return text;
			}

			bool IsSceneOrPrefab(std::string const& extension) { return extension == ".scene" || extension == ".cprefab"; }

			// JSON assets that could hold a saved tag setting.
			bool IsJsonAsset(std::string const& extension)
			{
				return IsSceneOrPrefab(extension) || extension == ".json" || extension == ".mat" || extension == ".animcontroller"
					|| extension == ".stategraph" || extension == ".behaviorgraph" || extension == ".fontsettings";
			}

			bool IsBlank(std::string_view text)
			{
				return std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) != 0; });
			}

			// ---- JSON text, in the engine's own style -----------------------------------

			// How a file is laid out, so a rewrite keeps it (JsonWrapper writes CRLF on Windows,
			// no newline at the end; hand-made files may differ).
			struct TextStyle
			{
				bool bom = false;
				bool crlf = false;
				bool trailingNewline = false;
			};

			bool Parse(std::string const& bytes, rapidjson::Document& doc, TextStyle& style)
			{
				std::string_view text = bytes;
				style.bom = text.starts_with(kBom);
				if (style.bom) { text.remove_prefix(kBom.size()); }
				style.crlf = text.find("\r\n") != std::string_view::npos;
				style.trailingNewline = !text.empty() && text.back() == '\n';
				// Full precision: every number reads back as the exact double it was written from,
				// so writing it again gives the same digits.
				doc.Parse<rapidjson::kParseFullPrecisionFlag>(text.data(), text.size());
				return !doc.HasParseError();
			}

			bool Serialize(rapidjson::Document const& doc, TextStyle const& style, std::string& out)
			{
				rapidjson::StringBuffer buffer;
				rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);   // JsonWrapper::ToString's writer
				if (!doc.Accept(writer)) { return false; }                          // e.g. a NaN
				std::string_view const text(buffer.GetString(), buffer.GetSize());
				out.clear();
				out.reserve(text.size() + text.size() / 16 + 8);
				if (style.bom) { out += kBom; }
				for (char const c : text) {
					if (c == '\n' && style.crlf) { out += '\r'; }
					out += c;
				}
				if (style.trailingNewline) { out += style.crlf ? "\r\n" : "\n"; }
				return true;
			}

			// ---- walking documents ------------------------------------------------------

			std::string_view View(rapidjson::Value const& value)
			{
				return std::string_view(value.GetString(), value.GetStringLength());
			}

			// A member that holds tag NAMES: tag, tags, obstacleTag(s), tagFilter... ("stage" is not).
			bool IsTagKey(std::string_view key)
			{
				if (key == "tag" || key == "tags") { return true; }
				if (key.ends_with("Tag") || key.ends_with("Tags")) { return true; }
				return key.size() > 3 && key.starts_with("tag") && std::isupper(static_cast<unsigned char>(key[3]));
			}

			bool NamesTag(rapidjson::Value const& value, std::string_view tag)
			{
				if (value.IsString()) { return View(value) == tag; }
				if (value.IsArray()) {
					for (auto const& item : value.GetArray()) {
						if (item.IsString() && View(item) == tag) { return true; }
					}
				}
				return false;
			}

			struct Found
			{
				std::size_t              entityTags = 0;
				bool                     flowField = false;
				std::vector<std::string> settings;
			};

			void Walk(rapidjson::Value const& value, std::string_view tag, std::string_view parentKey, bool insideEntityTag, Found& found)
			{
				if (value.IsObject()) {
					for (auto member = value.MemberBegin(); member != value.MemberEnd(); ++member) {
						std::string_view const key = View(member->name);
						if (key == "FlowFieldSurface") { found.flowField = true; }
						if (key == ComponentTraits<EntityTag>::name && member->value.IsObject()) {
							auto const field = member->value.FindMember("tag");
							if (field != member->value.MemberEnd() && field->value.IsString() && View(field->value) == tag) { ++found.entityTags; }
						}
						bool const ownTagField = insideEntityTag && key == "tag";   // the assignment itself, counted above
						if (!ownTagField && IsTagKey(key) && NamesTag(member->value, tag)) {
							found.settings.push_back((parentKey.empty() ? std::string{} : std::string(parentKey) + ".") + std::string(key));
						}
						Walk(member->value, tag, key, key == ComponentTraits<EntityTag>::name, found);
					}
				}
				else if (value.IsArray()) {
					for (auto const& item : value.GetArray()) { Walk(item, tag, parentKey, false, found); }
				}
			}

			std::size_t ReplaceEntityTags(rapidjson::Value& value, std::string_view from, std::string_view to,
			                              rapidjson::Document::AllocatorType& allocator)
			{
				std::size_t replaced = 0;
				if (value.IsObject()) {
					for (auto member = value.MemberBegin(); member != value.MemberEnd(); ++member) {
						if (View(member->name) == ComponentTraits<EntityTag>::name && member->value.IsObject()) {
							auto const field = member->value.FindMember("tag");
							if (field != member->value.MemberEnd() && field->value.IsString() && View(field->value) == from) {
								field->value.SetString(to.data(), static_cast<rapidjson::SizeType>(to.size()), allocator);
								++replaced;
							}
						}
						replaced += ReplaceEntityTags(member->value, from, to, allocator);
					}
				}
				else if (value.IsArray()) {
					for (auto& item : value.GetArray()) { replaced += ReplaceEntityTags(item, from, to, allocator); }
				}
				return replaced;
			}

			void ScanScript(std::string const& bytes, std::string const& display, std::string_view tag, Report& report)
			{
				std::string const literal = "\"" + std::string(tag) + "\"";
				std::size_t line = 1, start = 0;
				while (start <= bytes.size()) {
					std::size_t end = bytes.find('\n', start);
					if (end == std::string::npos) { end = bytes.size(); }
					std::string_view text(bytes.data() + start, end - start);
					if (text.find(literal) != std::string_view::npos && report.scripts.size() < kMaxScriptHits) {
						while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) { text.remove_prefix(1); }
						while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) { text.remove_suffix(1); }
						report.scripts.push_back({ display, line, std::string(text.substr(0, 160)) });
					}
					start = end + 1;
					++line;
				}
			}

			// ---- one file to rewrite ------------------------------------------------------

			struct Rewrite
			{
				fs::path    path;
				std::string display;
				std::string original;   // bytes as read
				std::string updated;    // bytes to write
				bool        written = false;
			};

			// The new bytes for one file, checked to differ from the old ones only in the tags.
			bool PrepareRewrite(Rewrite& file, std::string const& from, std::string const& to, bool& reformatted, std::string& why)
			{
				rapidjson::Document doc;
				TextStyle style;
				if (!Parse(file.original, doc, style)) { why = file.display + " is not valid JSON"; return false; }

				std::string unchanged;
				reformatted = !Serialize(doc, style, unchanged) || unchanged != file.original;

				if (ReplaceEntityTags(doc, from, to, doc.GetAllocator()) == 0) { why = file.display + " no longer uses the tag"; return false; }
				if (!Serialize(doc, style, file.updated)) { why = file.display + " has a value JSON can't hold (NaN/infinity)"; return false; }

				// Read both back: the new file must be the old document with only those tags changed.
				rapidjson::Document expected, written;
				TextStyle ignored;
				Parse(file.original, expected, ignored);
				ReplaceEntityTags(expected, from, to, expected.GetAllocator());
				if (!Parse(file.updated, written, ignored) || !(written == expected)) {
					why = file.display + " did not read back as expected - not changed";
					return false;
				}
				return true;
			}

			std::string FolderSafe(std::string_view text)
			{
				std::string out;
				for (char const c : text) { out += std::isalnum(static_cast<unsigned char>(c)) ? c : '_'; }
				return out.empty() ? std::string("tag") : out.substr(0, 40);
			}

			// Puts back every file already replaced. Returns the ones that could not be restored.
			std::vector<std::string> RollBack(std::vector<Rewrite>& files)
			{
				std::vector<std::string> failed;
				for (auto it = files.rbegin(); it != files.rend(); ++it) {
					if (!it->written) { continue; }
					std::string why;
					if (ReplaceFile(it->path, it->original, why)) { it->written = false; }
					else { failed.push_back(it->display + ": " + why); }
				}
				return failed;
			}
		}

		// =======================================================================
		//  Report
		// =======================================================================

		std::size_t Report::SceneFiles() const
		{
			return static_cast<std::size_t>(std::count_if(files.begin(), files.end(), [](FileUsage const& f) { return !f.prefab; }));
		}

		std::size_t Report::PrefabFiles() const
		{
			return static_cast<std::size_t>(std::count_if(files.begin(), files.end(), [](FileUsage const& f) { return f.prefab; }));
		}

		std::size_t Report::Entities() const
		{
			std::size_t total = liveEntities;
			for (FileUsage const& f : files) {
				if (!f.openScene) { total += f.entities; }   // the open scene is counted from memory
			}
			return total;
		}

		// =======================================================================
		//  Scan
		// =======================================================================

		Report Scan(World& world, std::string_view tag, fs::path const& assetsRoot, fs::path const& openSceneFile,
		            fs::path const& registryFile, bool forWrite)
		{
			Report report;
			report.tag = std::string(tag);

			// 1. The open scene as it is in memory: its entities, and any tag settings on them.
			std::vector<Entity> entities;
			world.CreateQuery<const EntityTag>().ForEach([&](Entity entity, EntityTag const&) { entities.push_back(entity); });
			bool liveFlowField = false;
			for (Entity const entity : entities) {
				if (world.CompareTag(entity, tag)) { ++report.liveEntities; }
				JsonWrapper doc;
				EntityIO::WriteComponents(world, entity, doc, "components");
				Found found;
				Walk(doc.GetDocument(), tag, {}, false, found);
				liveFlowField |= found.flowField;
				for (std::string& key : found.settings) { report.settings.push_back({ "open scene", std::move(key) }); }
			}
			if (liveFlowField) { report.navigationScenes.push_back("open scene"); }

			// 2. Every asset on disk.
			std::error_code ec;
			if (assetsRoot.empty() || !fs::is_directory(assetsRoot, ec)) {
				report.problems.push_back("the Assets folder was not found, so saved scenes and prefabs can't be checked");
				return report;
			}
			fs::recursive_directory_iterator it(assetsRoot, fs::directory_options::none, ec), end;
			if (ec) { report.problems.push_back("could not list the Assets folder (" + ec.message() + ")"); }
			for (; !ec && it != end; it.increment(ec)) {
				std::string const name = PathToUtf8(it->path().filename());
				if (!name.empty() && name.front() == '.') {   // .git, .vs...
					if (it->is_directory(ec)) { it.disable_recursion_pending(); }
					continue;
				}
				std::error_code fileEc;
				if (!it->is_regular_file(fileEc)) { continue; }
				std::string const extension = Lower(PathToUtf8(it->path().extension()));
				bool const script = extension == ".cs";
				if (!script && !IsJsonAsset(extension)) { continue; }
				if (SameFile(it->path(), registryFile)) { continue; }   // the tag list itself

				std::string const display = Relative(it->path(), assetsRoot);
				std::string bytes;
				if (!ReadBytes(it->path(), bytes)) {
					report.problems.push_back(display + " can't be read");
					continue;
				}
				++report.scannedFiles;
				if (script) { ScanScript(bytes, display, tag, report); continue; }
				if (IsBlank(std::string_view(bytes).substr(bytes.starts_with(kBom) ? kBom.size() : 0))) { continue; }   // empty: nothing in it

				rapidjson::Document doc;
				TextStyle style;
				if (!Parse(bytes, doc, style)) {
					report.problems.push_back(display + " is not valid JSON, so it can't be checked");
					continue;
				}
				Found found;
				Walk(doc, tag, {}, false, found);
				if (extension == ".scene" && found.flowField) { report.navigationScenes.push_back(display); }
				for (std::string& key : found.settings) { report.settings.push_back({ display, std::move(key) }); }
				if (IsSceneOrPrefab(extension) && found.entityTags > 0) {
					report.files.push_back({ it->path(), display, found.entityTags, extension == ".cprefab", SameFile(it->path(), openSceneFile) });
				}
			}
			if (ec) { report.problems.push_back("listing the Assets folder stopped early (" + ec.message() + ")"); }

			// 3. Everything a change would write must be writable.
			if (forWrite) {
				for (FileUsage const& file : report.files) {
					std::string why;
					if (!CanWrite(file.path, why)) { report.problems.push_back(file.display + " " + why); }
				}
				std::string why;
				if (!registryFile.empty() && fs::exists(registryFile, ec) && !CanWrite(registryFile, why)) {
					report.problems.push_back("Tags.json " + why);
				}
			}
			return report;
		}

		// =======================================================================
		//  Retag
		// =======================================================================

		Result Retag(World& world, TagRegistry& registry, Options const& options,
		             std::string const& from, std::string const& to, Operation operation)
		{
			Result result;
			auto fail = [&](std::string message, std::vector<std::string> details = {}) {
				result.ok = false;
				result.message = std::move(message);
				result.details = std::move(details);
				return result;
			};

			// ---- 0. what is asked ----
			if (from == kUntaggedTag) { return fail("\"Untagged\" is built in: it can't be renamed or deleted."); }
			if (!registry.IsDefined(from)) { return fail("'" + from + "' is not a project tag."); }
			if (from == to) { return fail("The replacement must be a different tag."); }
			if (operation == Operation::Delete && !registry.IsDefined(to)) { return fail("'" + to + "' is not a project tag."); }
			if (operation == Operation::Rename) {
				TagError const check = to == kUntaggedTag ? TagError::Reserved
				                     : registry.IsDefined(to) ? TagError::Duplicate : TagRegistry::ValidateName(to);
				if (check != TagError::Ok) { return fail("Can't rename to '" + to + "': " + ToString(check) + "."); }
			}

			// ---- 1. a fresh scan, with write checks ----
			Report const report = Scan(world, from, options.assetsRoot, options.openSceneFile, options.registryFile, true);
			if (!report.Complete()) { return fail("Nothing was changed: the project scan is incomplete.", report.problems); }
			if (!report.settings.empty()) {
				std::vector<std::string> details;
				for (SettingUsage const& s : report.settings) { details.push_back(s.file + ": " + s.key); }
				return fail("Nothing was changed: saved tag settings name '" + from + "' and aren't updated automatically. "
				            "Change them first.", std::move(details));
			}

			// ---- 2. every new file, built and checked in memory ----
			std::vector<Rewrite> files;
			files.reserve(report.files.size());
			for (FileUsage const& usage : report.files) {
				Rewrite file;
				file.path = usage.path;
				file.display = usage.display;
				if (!ReadBytes(file.path, file.original)) { return fail("Nothing was changed: " + usage.display + " can't be read."); }
				bool reformatted = false;
				std::string why;
				if (!PrepareRewrite(file, from, to, reformatted, why)) { return fail("Nothing was changed: " + why + "."); }
				if (reformatted) { ++result.reformattedFiles; }
				files.push_back(std::move(file));
			}

			// ---- 3. backups (and a manifest) before anything is written ----
			std::string registryBefore;
			std::error_code ec;
			bool const registryFileExisted = !options.registryFile.empty() && fs::exists(options.registryFile, ec);
			if (registryFileExisted && !ReadBytes(options.registryFile, registryBefore)) {
				return fail("Nothing was changed: Tags.json can't be read.");
			}
			std::string const listBefore = registry.ToJson();

			if (!files.empty() || registryFileExisted) {
				if (options.backupRoot.empty()) { return fail("Nothing was changed: there is no folder to keep backups in."); }
				std::string const stamp = std::format("{:%Y%m%d-%H%M%S}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
				fs::path folder = options.backupRoot / (stamp + "-" + (operation == Operation::Rename ? "rename-" : "delete-") + FolderSafe(from));
				for (int n = 2; fs::exists(folder, ec); ++n) { folder = options.backupRoot / (stamp + "-" + std::to_string(n)); }
				fs::create_directories(folder, ec);
				if (ec) { return fail("Nothing was changed: could not create the backup folder " + PathToUtf8(folder) + "."); }

				std::string manifest = std::string(operation == Operation::Rename ? "Rename tag '" : "Delete tag '") + from +
					"', its uses becoming '" + to + "'.\nOriginals of every file changed, under the same paths relative to Assets.\n"
					"To undo by hand, copy them back over the project's files.\n\n";
				std::string why;
				for (Rewrite const& file : files) {
					fs::path const copy = folder / fs::path(file.display);
					fs::create_directories(copy.parent_path(), ec);
					if (!ReplaceFile(copy, file.original, why)) { return fail("Nothing was changed: backing up failed - " + why + "."); }
					manifest += file.display + "\n";
				}
				if (registryFileExisted) {
					if (!ReplaceFile(folder / "Tags.json", registryBefore, why)) { return fail("Nothing was changed: backing up failed - " + why + "."); }
					manifest += "Tags.json (Config/Tags.json)\n";
				}
				if (!ReplaceFile(folder / "manifest.txt", manifest, why)) { return fail("Nothing was changed: backing up failed - " + why + "."); }
				result.backupFolder = folder;
			}

			// ---- 4. the files ----
			for (Rewrite& file : files) {
				std::string why;
				std::string current;
				bool ok = true;
				if (!ReadBytes(file.path, current) || current != file.original) {   // saved by someone since the scan
					ok = false;
					why = file.display + " changed on disk during the operation";
				}
				else if (options.beforeWrite && !options.beforeWrite(file.path)) {
					ok = false;
					why = "writing " + file.display + " was refused";
				}
				else if (!ReplaceFile(file.path, file.updated, why)) {
					ok = false;
				}
				if (!ok) {
					std::vector<std::string> notRestored = RollBack(files);
					if (notRestored.empty()) { return fail("Nothing was changed: " + why + ". Every file already written was put back."); }
					notRestored.insert(notRestored.begin(), "Restore these from " + PathToUtf8(result.backupFolder) + ":");
					return fail("FAILED: " + why + ", and some files could not be put back.", std::move(notRestored));
				}
				file.written = true;
				result.changedFiles.push_back(file.path);
			}

			// ---- 5. the tag list ----
			TagError const listChange = operation == Operation::Rename ? registry.Rename(from, to) : registry.Remove(from);
			std::string saveError;
			if (listChange != TagError::Ok || !registry.Save(options.registryFile, &saveError)) {
				std::string why = listChange != TagError::Ok ? std::string("the tag list refused the change (") + ToString(listChange) + ")"
				                                             : "Tags.json could not be saved (" + saveError + ")";
				std::string restoreError;
				if (!registry.LoadFromString(listBefore, &restoreError)) { why += "; the list in memory could not be restored"; }
				if (registryFileExisted) {
					std::string ignored;
					ReplaceFile(options.registryFile, registryBefore, ignored);
				}
				std::vector<std::string> notRestored = RollBack(files);
				result.changedFiles.clear();
				if (notRestored.empty()) { return fail("Nothing was changed: " + why + ". Every file was put back."); }
				notRestored.insert(notRestored.begin(), "Restore these from " + PathToUtf8(result.backupFolder) + ":");
				return fail("FAILED: " + why + ", and some files could not be put back.", std::move(notRestored));
			}

			// ---- 6. the open scene, in memory ----
			// `to` is in `registry` now (checked above); written through the restore path so this
			// doesn't depend on which registry World consults.
			for (Entity const entity : world.FindAllWithTag(from)) {
				world.AddComponent<EntityTag>(entity, EntityTag{ to });
				++result.liveEntitiesChanged;
			}

			result.ok = true;
			result.message = std::string(operation == Operation::Rename ? "Renamed '" : "Deleted '") + from + "'" +
				(operation == Operation::Rename ? " to '" + to + "'" : "; its uses are now '" + to + "'") + ": " +
				std::to_string(result.changedFiles.size()) + " file(s), " + std::to_string(result.liveEntitiesChanged) +
				" entities in the open scene.";
			for (Rewrite const& file : files) { result.details.push_back(file.display); }
			return result;
		}
	}
}
