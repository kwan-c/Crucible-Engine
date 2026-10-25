#pragma once
/*!
* @file			FontCompileJob.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			21 October 2026
*
* @brief		Connects the font compiler to the project's files: reading a font's
*				.fontsettings and sample texts, checking whether its .cfont is up to date,
*				and compiling it on a worker thread.
*
* Files, for "Fonts/Arial.ttf":
*	Fonts/Arial.ttf               the source font (AssetType::Font)
*	Fonts/Arial.ttf.fontsettings  its import settings (FontImportSettings.h); optional
*	Fonts/Arial.cfont             the compiled font - what the engine loads
*
* A compile (FontCompileJob):
*	Start()  main thread: reads the font, settings and sample texts (AssetManager is not
*	         thread-safe), then hands the bytes to a worker thread, which runs CompileFont and
*	         encodes the .cfont.
*	Poll()   main thread, every frame: once the worker is done, writes the .cfont and returns
*	         the report. The caller then re-indexes the assets (so the new file gets a .meta)
*	         and calls ResourceManager::ReloadFont - see FontImporter.
*/

#include <chrono>
#include <cstdint>
#include <future>
#include <optional>
#include <string>
#include <vector>

#include "Fonts/FontCompiler.h"

namespace Crucible {

	class AssetManager;

	namespace FontAssets {

		/// ".ttf" / ".otf" (any case).
		bool IsSourceFontPath(std::string const& virtualPath);

		/// "Fonts/Arial.cfont" -> "Fonts/Arial.ttf" (or .otf), if that file is indexed; "" if not.
		std::string SourceFontFor(AssetManager const& assets, std::string const& compiledPath);

		/// A font's settings as saved on disk (the defaults when it has no .fontsettings file).
		struct SavedSettings {
			FontImportSettings       settings;
			bool                     fileExists = false;
			std::vector<std::string> warnings;   ///< Values the file had that were fixed up.
			std::string              error;      ///< The file exists but isn't valid JSON (settings = defaults then).
		};
		SavedSettings LoadSettings(AssetManager& assets, std::string const& fontPath);

		/// Writes "<font>.fontsettings". false (and `error`) on failure.
		bool SaveSettings(AssetManager& assets, std::string const& fontPath, FontImportSettings const& settings, std::string& error);

		/// The contents of settings.sampleTextFiles, in order. A file that can't be read gives "" and a warning.
		std::vector<std::string> ReadSampleTexts(AssetManager& assets, FontImportSettings const& settings, std::vector<std::string>& warnings);

		enum class FontState {
			NoSource,      ///< The font file isn't in the asset index.
			NotCompiled,   ///< No .cfont yet.
			UpToDate,
			OutOfDate,     ///< The font, its saved settings or a sample text changed since the compile.
			Unreadable,    ///< The .cfont is broken or from another format version.
		};
		char const* ToString(FontState state);

		struct FontStatus {
			FontState    state = FontState::NoSource;
			std::string  detail;          ///< Why it is out of date / unreadable.
			bool         hasInfo = false; ///< `info` is the .cfont's header.
			FontFileInfo info;
		};

		/// Compares the .cfont's recorded hashes with the font file and `savedSettings` (+ its sample texts).
		FontStatus GetStatus(AssetManager& assets, std::string const& fontPath, FontImportSettings const& savedSettings);

	}	// namespace FontAssets

	class FontCompileJob {
	public:
		struct Report {
			bool                     ok = false;
			std::string              fontPath;
			std::string              compiledPath;
			std::string              error;
			FontOutputType           outputType = FontOutputType::Bitmap;
			std::size_t              glyphCount = 0;
			std::size_t              groupCount = 0;
			std::size_t              kernCount = 0;
			std::uint32_t            atlasWidth = 0, atlasHeight = 0;
			std::size_t              fileBytes = 0;
			double                   compileSeconds = 0.0;   ///< Just the compiler.
			double                   totalSeconds = 0.0;     ///< Start() to written.
			std::vector<std::uint32_t> missing;
			std::vector<std::string>   warnings;
		};

		FontCompileJob() = default;
		FontCompileJob(FontCompileJob const&) = delete;
		FontCompileJob& operator=(FontCompileJob const&) = delete;
		~FontCompileJob();   // waits for a running compile (a few seconds at most)

		/*!
		* @brief	Reads the inputs and starts compiling on a worker thread.
		* @return	false if one is already running or the font can't be read (`error` says why).
		*/
		bool Start(AssetManager& assets, std::string const& fontPath, FontImportSettings const& settings, std::string& error);

		bool               IsRunning() const { return m_future.valid(); }
		std::string const& FontPath() const { return m_fontPath; }
		double             ElapsedSeconds() const;

		/// Main thread, every frame. The frame the worker finishes: writes the .cfont and returns the report.
		std::optional<Report> Poll(AssetManager& assets);

	private:
		struct Output {
			bool                      ok = false;
			std::string               error;
			FontCompileResult         result;
			std::vector<std::uint8_t> file;    ///< The encoded .cfont.
		};

		std::future<Output>                   m_future;
		std::string                           m_fontPath;
		std::chrono::steady_clock::time_point m_started{};
	};

}	// NAMESPACE CRUCIBLE
