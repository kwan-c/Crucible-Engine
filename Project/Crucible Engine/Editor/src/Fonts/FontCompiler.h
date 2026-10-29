#pragma once
/*!
* @file			FontCompiler.h
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			21 October 2026
*
* @brief		Bakes a source font (.ttf/.otf bytes) into a FontData, ready for
*				WriteFont() -> .cfont. Editor only: the engine just loads .cfont files.
*
* Per output type:
*	Bitmap	FreeType renders every glyph at each of settings.bitmapSizes (anti-aliased
*			coverage), packed into one 1-channel atlas, one FontSizeGroup per size.
*	SDF		msdf-atlas-gen: one-channel signed distance field at settings.glyphSize.
*	MTSDF	msdf-atlas-gen: edge-coloured multi-channel field + true distance in alpha.
* Everything is converted to FontData's conventions (ems, +y down, top-left UVs, top
* row first) - see Text/FontData.h.
*
* Which characters: ASCII (if includeAscii) + settings.ranges + every character in the
* sample texts. Ones the font doesn't have are listed in FontCompileResult::missing
* (the compile still succeeds). Kerning comes from the font's 'kern' table; fonts that
* only have GPOS kerning get none (that needs HarfBuzz).
*
* Thread-safe: each call has its own FreeType instance, so it can run on a worker
* thread while the editor keeps drawing. Pure function - no files, no AssetManager:
* the caller reads the font and sample texts and writes the result.
*/

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "Fonts/FontImportSettings.h"
#include "Text/FontData.h"

namespace Crucible {

	struct FontCompileResult {
		FontData                   font;        ///< Valid on success. settingsHash is set; sourceHash is left for the caller.
		std::vector<std::uint32_t> missing;     ///< Requested codepoints the font doesn't have.
		std::vector<std::string>   warnings;    ///< Settings that were fixed up, skipped glyphs, no kerning, ...
		double                     seconds = 0.0;
	};

	/*!
	* @brief	Compiles a font.
	* @param	fontBytes    The .ttf/.otf file. Must stay alive for the call.
	* @param	settings     Import settings (sanitized internally; fixes go to warnings).
	* @param	sampleTexts  The contents (UTF-8) of settings.sampleTextFiles, same order.
	* @param	out          The result. Cleared first.
	* @param	error        Why it failed, e.g. "not a font file", "atlas would be 8192x8192".
	* @return	true on success.
	*/
	bool CompileFont(std::span<const std::uint8_t> fontBytes, FontImportSettings settings,
	                 std::span<const std::string> sampleTexts, FontCompileResult& out, std::string& error);

	/// The sorted, de-duplicated codepoints the settings ask for (no control characters).
	std::vector<std::uint32_t> CollectCodepoints(FontImportSettings const& settings, std::span<const std::string> sampleTexts,
	                                             std::vector<std::string>* warnings = nullptr);

	/// "U+4E00 '中', U+00E9 'é', ... (+12 more)" for logs and the Inspector.
	std::string DescribeCodepoints(std::span<const std::uint32_t> codepoints, std::size_t maxShown = 12);

}	// NAMESPACE CRUCIBLE
