#pragma once
/*************************************************************************
  \file       TextLayout.h
  \brief
    Lays out a string in a box with a compiled font: line breaks, word
    wrap, alignment, overflow, kerning, rich-text tags, underline and best
    fit. Produces one quad per visible glyph plus the underline bars.
  \details
    Pure: no ECS, no GPU, no allocation beyond the result. UIText drawing
    (UISystem.cpp) and anything else that needs text measured use it.

    Space and units (matching the UI, see UITypes.h):
      - Box-local, origin at the box's top-left corner, +y DOWN.
      - Everything in the caller's units: pass fontSize and box in reference
        pixels and the quads come back in reference pixels.

    Lines:
      - '\n' always breaks ('\r' is ignored, so Windows text works).
      - With wrap, a line breaks at the last space that still fits; a single
        word wider than the box is split between characters. The spaces at a
        wrap point are dropped, so wrapped lines align cleanly.
      - Line height = font lineHeight * fontSize * lineSpacing. The block is
        (lines - 1) line heights plus one ascender + descender, so Middle
        centres the actual letters, not the empty space above them.

    Overflow (what happens to text that doesn't fit):
      - Overflow: everything is laid out, past the box if need be.
      - Truncate: only the lines that fit in the box's height are kept; the
        caller clips what runs past the sides (UIPass's clip rect).
      - Ellipsis: like Truncate, and a cut line - the last kept one when lines
        were dropped, or any line too wide when wrap is off - ends with "..."
        (U+2026 if the font has it, otherwise three dots).

    Rich text (settings.richText), Unity's tags, case-insensitive, nestable:
      <b>bold</b>            synthetic bold: the glyph is drawn again a little
                             to the right (TextGlyphQuad::boldWidth) and takes
                             that much more room. Works with every font type.
      <u>underline</u>       a bar under the text, in the text's colour.
      <color=#RRGGBB>..</color>, also #RGB, #RRGGBBAA and names: black, white,
                             red, green, blue, yellow, cyan, magenta, orange,
                             purple, grey / gray. Replaces the colour; the alpha
                             multiplies the component's alpha.
      Anything that isn't one of these (a lone '<', "<3", an unknown tag) is
      shown as typed. A closing tag with nothing open is ignored.

    Best fit (settings.bestFit): the font size is the largest between
    minFontSize and maxFontSize at which all of the text fits the box - every
    line inside the height, no line wider than the box, no word split
    across lines. If even minFontSize doesn't fit, minFontSize is used and
    the overflow mode decides. TextLayoutResult::fontSize says which size.

    Bitmap fonts: pass pixelScale (screen pixels per caller unit) and the
    baked size nearest the on-screen size is picked for whatever size is
    used - including each size best fit tries. Without it, sizeGroup is used.

    Characters the font doesn't have are drawn as '?' (if it has that), and
    listed in TextLayoutResult::missing.
*************************************************************************/

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "EngineExport.h"
#include "Text/FontData.h"

namespace Crucible {

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL members in an exported struct; Engine.dll and its clients share one toolchain
#endif

	enum class TextAlignH : std::uint8_t { Left, Center, Right };
	enum class TextAlignV : std::uint8_t { Top, Middle, Bottom };
	enum class TextOverflow : std::uint8_t { Overflow, Truncate, Ellipsis };

	/// How much wider <b> makes a glyph, in ems (also the extra advance it gets).
	inline constexpr float kTextBoldWidthEm = 0.045f;

	struct TextLayoutSettings {
		float        fontSize         = 16.0f;            ///< Em size, in the caller's units. Ignored with bestFit.
		glm::vec2    box{ 100.0f, 30.0f };                ///< Width, height.
		TextAlignH   alignH           = TextAlignH::Left;
		TextAlignV   alignV           = TextAlignV::Top;
		bool         wrap             = true;
		TextOverflow overflow         = TextOverflow::Overflow;
		float        lineSpacing      = 1.0f;             ///< Multiplies the font's line height.
		float        characterSpacing = 0.0f;             ///< Extra advance after each character, in ems.
		bool         richText         = false;            ///< Read <b>, <u>, <color> tags (see the file header).
		bool         underline        = false;            ///< Underline everything (as if wrapped in <u>).
		bool         bold             = false;            ///< Bold everything (as if wrapped in <b>).
		bool         bestFit          = false;            ///< Pick the size (see the file header).
		float        minFontSize      = 10.0f;            ///< Best fit's smallest size.
		float        maxFontSize      = 40.0f;            ///< Best fit's largest size.
		float        pixelScale       = 0.0f;             ///< Bitmap fonts: screen pixels per caller unit; > 0 picks the size group.
		std::size_t  sizeGroup        = 0;                ///< Bitmap fonts when pixelScale is 0: which baked size. 0 otherwise.
	};

	struct TextGlyphQuad {
		glm::vec4     rect{ 0.0f };          ///< Box-local: min x, min y, max x, max y.
		glm::vec4     uv{ 0.0f };            ///< Atlas rect (FontGlyph::uv).
		glm::vec2     pen{ 0.0f };           ///< Pen position on the baseline (bitmap fonts snap this to whole pixels).
		glm::vec4     colour{ 1.0f };        ///< From a <color> tag, when hasColour.
		bool          hasColour = false;     ///< false: use the text's own colour.
		float         boldWidth = 0.0f;      ///< > 0: <b> - draw it again shifted up to this far right (caller units).
		std::uint32_t codepoint = 0;
	};

	/// An underline bar: draw as a solid rect.
	struct TextDecoration {
		glm::vec4 rect{ 0.0f };              ///< Box-local.
		glm::vec4 colour{ 1.0f };            ///< When hasColour.
		bool      hasColour = false;
	};

	struct TextLayoutResult {
		std::vector<TextGlyphQuad>  quads;           ///< Visible glyphs with ink, in reading order.
		std::vector<TextDecoration> underlines;      ///< Underline bars, one per run of underlined text on a line.
		glm::vec4                   bounds{ 0.0f };  ///< The laid-out lines' box (by advances and line metrics), box-local.
		float                       fontSize = 0.0f; ///< The size used (= settings.fontSize unless best fit chose another).
		std::size_t                 sizeGroup = 0;   ///< The size group used.
		std::size_t                 lineCount = 0;   ///< Lines after wrapping.
		std::size_t                 shownLines = 0;  ///< Lines kept (fewer than lineCount when truncated).
		bool                        clipped = false; ///< Something didn't fit (lines dropped or a line ellipsized).
		std::vector<std::uint32_t>  missing;         ///< Codepoints the font doesn't have (each once).
	};

	/*************************************************************************
	 \brief  Lays out UTF-8 text. Invalid UTF-8 bytes are skipped.
	 \param[out] out  Cleared first.
	 *************************************************************************/
	ENGINE_API void LayoutText(const FontData& font, std::string_view utf8, const TextLayoutSettings& settings, TextLayoutResult& out);

	/// Decodes UTF-8 into codepoints, skipping invalid bytes and '\r'.
	ENGINE_API void DecodeUtf8(std::string_view utf8, std::vector<std::uint32_t>& out);

	/// The text with its rich-text tags removed (what LayoutText would show, as UTF-8).
	ENGINE_API std::string StripRichTextTags(std::string_view utf8);

#ifdef _MSC_VER
#pragma warning(pop)
#endif

}   // NAMESPACE CRUCIBLE
