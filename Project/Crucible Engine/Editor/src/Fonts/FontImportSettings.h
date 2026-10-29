#pragma once
/*!
* @file			FontImportSettings.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			21 October 2026
*
* @brief		How a source font (.ttf/.otf) is compiled into a .cfont: the
*				import settings, and their .fontsettings JSON file.
*
* The settings live next to the font, e.g. "Fonts/Arial.ttf" ->
* "Fonts/Arial.ttf.fontsettings", so they are committed with it. A font with
* no settings file uses the defaults: a small Bitmap font (ASCII at 16 and 32
* px), so adding a .ttf to the project costs almost nothing until someone
* picks SDF/MTSDF for it.
*
* Example file (every key is optional; missing keys keep their default):
*	{
*	  "outputType": "MTSDF",              // "Bitmap" | "SDF" | "MTSDF"
*	  "bitmapSizes": [16, 32],            // Bitmap: pixel sizes to bake
*	  "glyphSize": 48,                    // SDF/MTSDF: pixels per em in the atlas
*	  "pixelRange": 8,                    // SDF/MTSDF: distance range in atlas pixels
*	  "padding": 2,                       // empty atlas pixels between glyphs
*	  "includeAscii": true,               // the 95 printable ASCII characters
*	  "ranges": ["00C0-00FF", "20AC"],    // extra characters, hex codepoints
*	  "sampleTextFiles": ["Text/Credits.txt"],  // every character in these, too
*	  "atlasMaxSize": 4096                // fail instead of making a bigger atlas
*	}
*
* No ImGui or font libraries here: the Inspector panel and FontCompiler both use it.
*/

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Text/FontData.h"   // FontOutputType

namespace Crucible {

	/// A source font's import settings. Defaults = what a font with no .fontsettings file gets.
	struct FontImportSettings {
		FontOutputType   outputType   = FontOutputType::Bitmap;
		std::vector<int> bitmapSizes  = { 16, 32 };   ///< Bitmap only. Pixels per em, ascending, no repeats.
		int              glyphSize    = 48;           ///< SDF/MTSDF only. Bigger = sharper corners, bigger atlas.
		float            pixelRange   = 8.0f;         ///< SDF/MTSDF only. How far the field reaches; also the outline/glow limit
		                                              ///< (effects reach pixelRange / 2 atlas pixels past the edge).
		int              padding      = 2;            ///< Empty atlas pixels around every glyph, so filtering never reads a neighbour.
		bool             includeAscii = true;
		std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;   ///< Inclusive codepoint ranges.
		std::vector<std::string> sampleTextFiles;     ///< Virtual paths (e.g. "Text/Credits.txt"), read as UTF-8.
		int              atlasMaxSize = 4096;         ///< Largest atlas side allowed.

		/// Clamps every value into its allowed range, sorts and de-duplicates the lists.
		/// Returns a line per change it made (empty = everything was already valid).
		std::vector<std::string> Sanitize();
	};

	/// "Fonts/Arial.ttf" -> "Fonts/Arial.ttf.fontsettings".
	std::string FontSettingsPathFor(std::string const& fontVirtualPath);

	/// "Fonts/Arial.ttf" -> "Fonts/Arial.cfont" (the compiled font ResourceManager::GetFont reads).
	std::string CompiledFontPathFor(std::string const& fontVirtualPath);

	/*!
	* @brief	Reads a .fontsettings file's text. Unknown keys are ignored and bad values
	*			are replaced by their defaults (each one reported in `warnings`), so an old or
	*			hand-edited file still loads.
	* @return	false only if the text isn't a JSON object (`error` says why); `out` = defaults then.
	*/
	bool ParseFontImportSettings(std::string const& json, FontImportSettings& out,
	                             std::vector<std::string>& warnings, std::string& error);

	/// The settings as pretty-printed JSON (always every key), for the .fontsettings file.
	std::string SerializeFontImportSettings(FontImportSettings const& settings);

	/*!
	* @brief	What FontData::settingsHash holds: changes whenever anything that affects the
	*			compiled font changes - a setting, or the text of a sample file.
	* @param	sampleTexts  The contents of settings.sampleTextFiles, in the same order.
	*/
	std::uint64_t ComputeFontSettingsHash(FontImportSettings const& settings, std::span<const std::string> sampleTexts);

	/// "00C0-00FF", "20AC", "U+20AC" or "0x20AC" (hex, inclusive) -> a range. false if it isn't one.
	bool ParseCodepointRange(std::string const& text, std::pair<std::uint32_t, std::uint32_t>& out);

	/// The reverse: "00C0-00FF", or "20AC" for a single codepoint.
	std::string FormatCodepointRange(std::pair<std::uint32_t, std::uint32_t> const& range);

	/// Parses "FontOutputType" names ("Bitmap", "SDF", "MTSDF", any case). false if unknown.
	bool ParseFontOutputType(std::string const& text, FontOutputType& out);

}	// NAMESPACE CRUCIBLE
