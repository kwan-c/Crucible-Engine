/*!
* @file			FontCompileJob.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			21 October 2026
*
* @brief		Font settings / status files and the background compile. See FontCompileJob.h.
*/

#include "Fonts/FontCompileJob.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

#include "AssetPipe/Assetmanager.h"

namespace Crucible {

	namespace FontAssets {

		namespace {
			std::string LowerExtension(std::string const& path) {
				const std::size_t slash = path.find_last_of('/');
				const std::size_t dot   = path.find_last_of('.');
				if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) { return {}; }
				std::string ext = path.substr(dot);
				std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return ext;
			}

			// AssetManager::ReadBytes prints an error for a missing file; a missing .cfont or
			// .fontsettings is normal here, so check first.
			bool Exists(AssetManager const& assets, std::string const& virtualPath) {
				std::error_code ec;
				return std::filesystem::is_regular_file(assets.Resolve(virtualPath), ec);
			}
		}

		bool IsSourceFontPath(std::string const& virtualPath) {
			const std::string ext = LowerExtension(virtualPath);
			return ext == ".ttf" || ext == ".otf";
		}

		std::string SourceFontFor(AssetManager const& assets, std::string const& compiledPath) {
			const std::size_t dot = compiledPath.find_last_of('.');
			const std::string stem = dot == std::string::npos ? compiledPath : compiledPath.substr(0, dot);
			for (char const* ext : { ".ttf", ".otf", ".TTF", ".OTF" }) {
				if (assets.Find(stem + ext) != nullptr) { return stem + ext; }
			}
			return {};
		}

		SavedSettings LoadSettings(AssetManager& assets, std::string const& fontPath) {
			SavedSettings saved;
			const std::string path = FontSettingsPathFor(fontPath);
			std::vector<unsigned char> bytes;
			// ReadBytes, not the index: the file may have been written since the last scan.
			if (!Exists(assets, path) || !assets.ReadBytes(path, bytes)) { return saved; }   // no file: defaults
			saved.fileExists = true;
			const std::string json(bytes.begin(), bytes.end());
			if (!ParseFontImportSettings(json, saved.settings, saved.warnings, saved.error)) {
				saved.error = path + ": " + saved.error;
			}
			return saved;
		}

		bool SaveSettings(AssetManager& assets, std::string const& fontPath, FontImportSettings const& settings, std::string& error) {
			error.clear();
			const std::string path = FontSettingsPathFor(fontPath);
			if (!assets.WriteText(path, SerializeFontImportSettings(settings))) {
				error = "could not write " + path;
				return false;
			}
			return true;
		}

		std::vector<std::string> ReadSampleTexts(AssetManager& assets, FontImportSettings const& settings, std::vector<std::string>& warnings) {
			std::vector<std::string> texts;
			texts.reserve(settings.sampleTextFiles.size());
			for (std::string const& file : settings.sampleTextFiles) {
				std::vector<unsigned char> bytes;
				if (Exists(assets, file) && assets.ReadBytes(file, bytes)) {
					texts.emplace_back(bytes.begin(), bytes.end());
				} else {
					warnings.push_back("sample text '" + file + "' could not be read - skipped");
					texts.emplace_back();   // keep the order, so the hash stays comparable
				}
			}
			return texts;
		}

		char const* ToString(FontState state) {
			switch (state) {
				case FontState::NoSource:    return "Font file not found";
				case FontState::NotCompiled: return "Not compiled";
				case FontState::UpToDate:    return "Up to date";
				case FontState::OutOfDate:   return "Out of date";
				case FontState::Unreadable:  return "Compiled font unreadable";
			}
			return "Unknown";
		}

		FontStatus GetStatus(AssetManager& assets, std::string const& fontPath, FontImportSettings const& savedSettings) {
			FontStatus status;
			AssetEntry const* source = assets.Find(fontPath);
			if (!source) { status.state = FontState::NoSource; status.detail = fontPath; return status; }

			std::vector<unsigned char> bytes;
			const std::string compiled = CompiledFontPathFor(fontPath);
			if (!Exists(assets, compiled) || !assets.ReadBytes(compiled, bytes)) { status.state = FontState::NotCompiled; return status; }

			std::string error;
			status.hasInfo = ReadFontInfo(std::span<const std::uint8_t>(bytes.data(), bytes.size()), status.info, error);
			if (!status.hasInfo) {
				status.state  = FontState::Unreadable;
				status.detail = error + " - compile it again";
				return status;
			}

			std::vector<std::string> ignored;
			const std::vector<std::string> texts = ReadSampleTexts(assets, savedSettings, ignored);
			if (status.info.sourceHash != source->contentHash) {
				status.state  = FontState::OutOfDate;
				status.detail = "the font file changed since it was compiled";
			} else if (status.info.settingsHash != ComputeFontSettingsHash(savedSettings, texts)) {
				status.state  = FontState::OutOfDate;
				status.detail = "the import settings or a sample text changed since it was compiled";
			} else {
				status.state = FontState::UpToDate;
			}
			return status;
		}

	}	// namespace FontAssets

	// ----------------------------------------------------------------------------------------- //

	FontCompileJob::~FontCompileJob() {
		if (m_future.valid()) { m_future.wait(); }
	}

	double FontCompileJob::ElapsedSeconds() const {
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_started).count();
	}

	bool FontCompileJob::Start(AssetManager& assets, std::string const& fontPath, FontImportSettings const& settings, std::string& error) {
		error.clear();
		if (IsRunning()) { error = "a font is already compiling (" + m_fontPath + ")"; return false; }

		AssetEntry const* entry = assets.Find(fontPath);
		if (!entry) { error = "'" + fontPath + "' is not in the asset index - refresh the Asset Browser"; return false; }

		std::vector<unsigned char> fontBytes;
		if (!assets.ReadBytes(fontPath, fontBytes) || fontBytes.empty()) { error = "could not read '" + fontPath + "'"; return false; }

		std::vector<std::string> readWarnings;
		std::vector<std::string> texts = FontAssets::ReadSampleTexts(assets, settings, readWarnings);
		const std::uint64_t sourceHash = entry->contentHash;

		m_fontPath = fontPath;
		m_started  = std::chrono::steady_clock::now();
		m_future   = std::async(std::launch::async,
			[bytes = std::move(fontBytes), settings, texts = std::move(texts), readWarnings = std::move(readWarnings), sourceHash]() {
				Output out;
				out.ok = CompileFont(std::span<const std::uint8_t>(bytes.data(), bytes.size()), settings, texts, out.result, out.error);
				out.result.warnings.insert(out.result.warnings.begin(), readWarnings.begin(), readWarnings.end());
				if (out.ok) {
					out.result.font.m_sourceHash = sourceHash;
					out.ok = WriteFont(out.result.font, out.file, out.error);   // PNG-encodes the atlas: keep it off the main thread too
				}
				out.result.font.m_atlas.pixels.clear();   // written; the report doesn't need them
				out.result.font.m_atlas.pixels.shrink_to_fit();
				return out;
			});
		return true;
	}

	std::optional<FontCompileJob::Report> FontCompileJob::Poll(AssetManager& assets) {
		if (!m_future.valid() || m_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) { return std::nullopt; }

		Output out = m_future.get();   // leaves m_future invalid: IsRunning() is false from here

		Report report;
		report.fontPath       = m_fontPath;
		report.compiledPath   = CompiledFontPathFor(m_fontPath);
		report.ok             = out.ok;
		report.error          = out.error;
		report.missing        = std::move(out.result.missing);
		report.warnings       = std::move(out.result.warnings);
		report.compileSeconds = out.result.seconds;

		if (out.ok) {
			FontData const& font = out.result.font;
			report.outputType  = font.m_outputType;
			report.glyphCount  = font.m_glyphs.size();
			report.groupCount  = font.m_groups.size();
			report.kernCount   = font.m_kerning.size();
			report.atlasWidth  = font.m_atlas.width;
			report.atlasHeight = font.m_atlas.height;
			report.fileBytes   = out.file.size();

			// WriteText writes the bytes as they are (binary), whatever is in them.
			if (!assets.WriteText(report.compiledPath, std::string(out.file.begin(), out.file.end()))) {
				report.ok    = false;
				report.error = "could not write '" + report.compiledPath + "' (is it open in another program?)";
			}
		}
		report.totalSeconds = ElapsedSeconds();
		return report;
	}

}	// NAMESPACE CRUCIBLE
