/*!
* @file			Cook.cpp
* @author		Tok Duan Jie Nelson
* @par			t.duanjienelson@digipen.edu
* @date			30 October 2026
*
* @brief		Cook: fills the Resources folder (see Cook.h).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "Cook.h"

#include "AssetPipe/AssetManager.h"
#include "AssetPipe/AssetRegistry.h"
#include "Resources/CompiledSky.h"       // IsSkyboxSource, CompiledSkyExtension
#include "Resources/CompiledTexture.h"   // CompiledTextureExtension

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <regex>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace Crucible::Cook {

	namespace {

		std::string Utf8(fs::path const& path) {
			std::u8string const text = path.generic_u8string();
			return std::string(reinterpret_cast<char const*>(text.data()), text.size());
		}

		// Hidden files and folders (.git, .vs, editor temp files) are never assets.
		bool IsHidden(fs::path const& relative) {
			for (fs::path const& part : relative) {
				std::string const name = part.string();
				if (!name.empty() && name.front() == '.') return true;
			}
			return false;
		}

		std::string Lower(std::string text) { return AssetManager::ToLower(text); }

		bool SameBytes(fs::path const& a, fs::path const& b) {
			std::error_code ec;
			if (!fs::exists(b, ec) || fs::file_size(a, ec) != fs::file_size(b, ec) || ec) return false;
			std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
			if (!fa || !fb) return false;
			return std::equal(std::istreambuf_iterator<char>(fa), std::istreambuf_iterator<char>(),
			                  std::istreambuf_iterator<char>(fb));
		}

		// Copies `from` over `to` unless it already holds the same bytes. True if it copied.
		bool CopyIfDifferent(fs::path const& from, fs::path const& to, Report& report) {
			if (SameBytes(from, to)) return false;
			std::error_code ec;
			fs::create_directories(to.parent_path(), ec);
			fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
			if (ec) report.errors.push_back("Could not copy " + Utf8(from) + ": " + ec.message());
			return !ec;
		}

		void Remove(fs::path const& file, Report& report) {
			std::error_code ec;
			fs::remove(file, ec);
			if (ec) report.errors.push_back("Could not remove " + Utf8(file) + ": " + ec.message());
		}

		// The compiled file a source needs, or "" if it needs none.
		std::string CompiledFor(std::string const& virtualPath, std::string const& extension) {
			const char* compiledExtension = nullptr;
			if (extension == ".glb") compiledExtension = ".cmodel";
			else if (extension == ".ttf" || extension == ".otf") compiledExtension = ".cfont";
			else if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp" || extension == ".hdr")
				compiledExtension = IsSkyboxSource(virtualPath) ? CompiledSkyExtension : CompiledTextureExtension;
			if (!compiledExtension) return {};
			fs::path compiled(virtualPath);
			compiled.replace_extension(compiledExtension);
			return compiled.generic_string();
		}

		const char* HowToCompile(std::string const& compiled) {
			const std::string extension = fs::path(compiled).extension().string();
			if (extension == ".cmodel") return " (build the CompiledModels target)";
			if (extension == ".bill" || extension == ".sky") return " (build the CompiledTextures target)";
			return " (open the Font Importer and press Compile)";
		}

		// Whether a .cfont was compiled before atlases were Billet-packed: its header's atlas
		// encoding byte (offset 10, after "CFNT", the version, output type and channels) is 1, PNG.
		bool IsPngFont(fs::path const& file) {
			if (Lower(file.extension().string()) != ".cfont") return false;
			std::ifstream in(file, std::ios::binary);
			char header[11] = {};
			return in.read(header, sizeof(header)) && std::string_view(header, 4) == "CFNT" && header[10] == 1;
		}

		// The skybox GUIDs a scene or prefab names: "skyboxAsset": <number> (Skybox component).
		std::vector<uint64_t> SkyboxGuidsIn(fs::path const& file) {
			std::ifstream in(file, std::ios::binary);
			const std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
			std::vector<uint64_t> guids;
			static const std::regex field(R"re("skyboxAsset"\s*:\s*"?([0-9]+))re");
			for (std::sregex_iterator it(text.begin(), text.end(), field), end; it != end; ++it) {
				const uint64_t guid = std::strtoull((*it)[1].str().c_str(), nullptr, 10);
				if (guid != 0) guids.push_back(guid);
			}
			return guids;
		}

	}

	bool ShouldCopy(std::string const& virtualPath) {
		std::string const extension = Lower(fs::path(virtualPath).extension().string());
		if (AssetManager::IsCompiledExtension(extension)) return false;   // written by the compilers, never copied (or cleaned up)
		switch (AssetManager::ClassifyExtension(extension)) {
			case AssetType::Scene:
			case AssetType::Prefab:
			case AssetType::Material:
			case AssetType::Audio:
			case AssetType::AnimatorController:
				return true;
			case AssetType::Text:
				// Only the engine's config is read by the game (font sample texts are editor-only).
				return virtualPath.rfind("Config/", 0) == 0;
			default:
				return false;
		}
	}

	Report Run(fs::path const& assets, fs::path const& resources) {
		Report report;
		std::error_code ec;
		if (!fs::is_directory(assets, ec) || !fs::is_directory(resources, ec)) {
			report.errors.push_back("Assets (" + Utf8(assets) + ") and Resources (" + Utf8(resources) + ") must both exist");
			return report;
		}

		// 1. Copy what the game reads, and check what needs compiling. Also note every image's
		//    GUID and every scene/prefab, for the skybox check (3).
		std::unordered_map<uint64_t, std::string> imageByGuid;
		std::vector<std::pair<fs::path, std::string>> scenes;
		for (fs::recursive_directory_iterator it(assets, fs::directory_options::skip_permission_denied, ec), end;
			!ec && it != end; it.increment(ec))
		{
			fs::path const relative = it->path().lexically_relative(assets);
			if (IsHidden(relative)) { if (it->is_directory(ec)) it.disable_recursion_pending(); continue; }
			if (!it->is_regular_file(ec) || AssetRegistry::IsMetaFile(it->path())) continue;

			std::string const virtualPath = relative.generic_string();
			std::string const extension   = Lower(it->path().extension().string());
			AssetType const   type        = AssetManager::ClassifyExtension(extension);

			if (type == AssetType::Texture) {
				AssetMeta meta;
				if (AssetRegistry::ReadMeta(AssetRegistry::MetaPathFor(it->path()), meta)) imageByGuid[meta.guid.m_Value] = virtualPath;
			}
			if (type == AssetType::Scene || type == AssetType::Prefab) scenes.emplace_back(it->path(), virtualPath);

			// Sources (models, fonts, images) ship only as their compiled files.
			if (std::string const compiled = CompiledFor(virtualPath, extension); !compiled.empty()) {
				if (!fs::is_regular_file(resources / compiled, ec))
					report.missing.push_back(compiled + HowToCompile(compiled));
				else if (IsPngFont(resources / compiled))
					report.missing.push_back(compiled + " still has a PNG atlas, which the game cannot read (open the Font Importer and press Compile)");
				continue;
			}

			if (!ShouldCopy(virtualPath)) continue;

			fs::path const meta = AssetRegistry::MetaPathFor(it->path());
			if (!fs::is_regular_file(meta, ec)) {
				report.warnings.push_back(virtualPath + " has no .meta (GUID) yet - open the editor once so it gets one; not copied");
				continue;
			}

			bool const copied = CopyIfDifferent(it->path(), resources / relative, report);
			CopyIfDifferent(meta, AssetRegistry::MetaPathFor(resources / relative), report);
			copied ? ++report.copied : ++report.unchanged;
		}
		if (ec) report.errors.push_back("Could not read all of Assets: " + ec.message());
		ec.clear();

		// 2. Remove copies whose original is gone, and the image copies older Cooks made (images
		//    ship only as .bill / .sky now). Only files Cook itself copies are touched, so
		//    compiled files (and their .meta) are never removed here.
		std::vector<fs::path> stale;
		for (fs::recursive_directory_iterator it(resources, fs::directory_options::skip_permission_denied, ec), end;
			!ec && it != end; it.increment(ec))
		{
			if (!it->is_regular_file(ec)) continue;
			fs::path relative = it->path().lexically_relative(resources);
			if (IsHidden(relative)) continue;
			bool const isMeta = AssetRegistry::IsMetaFile(it->path());
			if (isMeta) relative.replace_extension();   // "Scenes/a.scene.meta" -> "Scenes/a.scene"
			std::string const extension = Lower(relative.extension().string());
			bool const oldImageCopy = AssetManager::ClassifyExtension(extension) == AssetType::Texture && !AssetManager::IsCompiledExtension(extension);
			if (!oldImageCopy && (!ShouldCopy(relative.generic_string()) || fs::exists(assets / relative, ec))) continue;
			stale.push_back(it->path());
			if (!isMeta) ++report.removed;
		}
		for (fs::path const& file : stale) Remove(file, report);

		// 3. Every image a scene or prefab uses as its skybox must be compiled as a sky, or the
		//    game shows the default sky there: its name needs "_skybox".
		std::unordered_set<uint64_t> reported;
		for (auto const& [file, virtualPath] : scenes) {
			for (uint64_t guid : SkyboxGuidsIn(file)) {
				auto const image = imageByGuid.find(guid);
				if (image == imageByGuid.end() || IsSkyboxSource(image->second) || !reported.insert(guid).second) continue;
				std::string const name = fs::path(image->second).stem().string(), ext = fs::path(image->second).extension().string();
				report.missing.push_back(image->second + " is the skybox in " + virtualPath + ", but its name has no \"_skybox\", so it is not "
				                         "compiled as a sky (rename it to " + name + "_skybox" + ext + "; its .meta keeps the GUID)");
			}
		}

		return report;
	}

}
