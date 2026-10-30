#pragma once
/*************************************************************************
  \file       TextBuilder.h
  \brief
    Turns laid-out text into the quads that draw it: one per glyph (more
    for bitmap bold), the underline bars, and a drop-shadow copy of all of
    it, with the distance-field effects (outline, glow, shadow softness,
    bold) worked out per quad. Also the layout cache both kinds of text use.
  \details
    Shared by screen-space text (UIText, UISystem.cpp) and world-space text,
    so both look the same. Everything here is in LAYOUT UNITS - the units
    the text's fontSize is in (reference pixels for UIText) - in the text
    box's own space: origin at its top-left corner, +y down, as TextLayout
    gives it. Where the quads go from there (canvas pixels, the world) is
    the caller's job, and so is anything that depends on it:
      - clipping to the box, or leaving out glyphs that poke out of it;
      - putting bitmap glyphs on whole screen pixels (TextQuad::pen);
      - making underlines at least a pixel thick.

    Effects only apply to distance-field fonts (SDF / MTSDF). The widths
    are converted into field units (see UITextEffects): the stored distance
    runs 0 -> 1 across the atlas's pixel range with the edge at 0.5, so
    everything drawn outside the edge must fit in the other 0.5. Bold comes
    first, then the outline, then the glow, each clamped to what is left of
    kTextMaxField. Bitmap fonts get no effects; their shadow is a flat copy
    and their bold is extra copies a pixel apart.

    Typical use, once per text per frame:
        TextLayoutSettings settings = TextLayoutSettingsOf(component, box);
        const TextLayoutResult& layout = cache.Get(key, id, font, component.text, settings);
        BuildTextQuads(font, layout, TextStyleOf(component), pixelsPerUnit, quads);
        for (const TextQuad& q : quads.quads) { ...place q.rect and draw it... }
*************************************************************************/

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "EngineExport.h"
#include "Graphics/UITypes.h"
#include "Text/FontData.h"
#include "Text/TextLayout.h"

namespace Crucible {

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL members in an exported class; Engine.dll and its clients share one toolchain
#endif

	/// How far outside a glyph's edge the effects may reach, in field units (the field ends at 0.5).
	inline constexpr float kTextMaxField = 0.49f;

	/// Distance-field bold moves each side of the edge out by half the extra advance <b> gets.
	inline constexpr float kTextBoldDilateEm = kTextBoldWidthEm * 0.5f;

	/*************************************************************************
	 \brief  The look of a piece of text besides its layout. Widths and the
	         shadow offset are in layout units; colours are sRGB, straight
	         alpha. The colour's alpha fades the effects and shadow too.
	 *************************************************************************/
	struct TextStyle {
		glm::vec4 colour{ 1.0f };

		bool      outline       = false;
		glm::vec4 outlineColour{ 0.0f, 0.0f, 0.0f, 1.0f };
		float     outlineWidth  = 0.0f;

		bool      glow          = false;
		glm::vec4 glowColour{ 1.0f };
		float     glowWidth     = 0.0f;
		float     glowSoftness  = 0.5f;    ///< 0 = an even band, 1 = fades out quickly.

		bool      shadow        = false;
		glm::vec4 shadowColour{ 0.0f, 0.0f, 0.0f, 0.6f };
		glm::vec2 shadowOffset{ 0.0f };    ///< +y down.
		float     shadowSoftness = 0.0f;   ///< 0 = crisp, 1 = as blurred as the field allows.
	};

	/// TextLayoutSettings from any text component with UIText's field names (UIText, WorldText).
	/// pixelScale and sizeGroup are left at 0: they depend on where the text is drawn.
	template <typename T>
	TextLayoutSettings TextLayoutSettingsOf(const T& text, glm::vec2 box) {
		TextLayoutSettings settings;
		settings.fontSize         = text.fontSize;
		settings.box              = box;
		settings.alignH           = text.horizontalAlign;
		settings.alignV           = text.verticalAlign;
		settings.wrap             = text.wrap;
		settings.overflow         = text.overflow;
		settings.lineSpacing      = text.lineSpacing;
		settings.characterSpacing = text.characterSpacing;
		settings.richText         = text.richText;
		settings.bold             = text.bold;
		settings.underline        = text.underline;
		settings.bestFit          = text.bestFit;
		settings.minFontSize      = text.minFontSize;
		settings.maxFontSize      = text.maxFontSize;
		return settings;
	}

	/// TextStyle from any text component with UIText's field names (UIText, WorldText).
	template <typename T>
	TextStyle TextStyleOf(const T& text) {
		TextStyle style;
		style.colour         = text.colour;
		style.outline        = text.outline;
		style.outlineColour  = text.outlineColour;
		style.outlineWidth   = text.outlineWidth;
		style.glow           = text.glow;
		style.glowColour     = text.glowColour;
		style.glowWidth      = text.glowWidth;
		style.glowSoftness   = text.glowSoftness;
		style.shadow         = text.shadow;
		style.shadowColour   = text.shadowColour;
		style.shadowOffset   = text.shadowOffset;
		style.shadowSoftness = text.shadowSoftness;
		return style;
	}

	enum class TextQuadKind : std::uint8_t {
		Glyph,       ///< A glyph from the font's atlas.
		BoldCopy,    ///< Bitmap bold: the glyph again, a little to the right.
		Underline    ///< A solid bar: no texture, UIDrawMode::Image.
	};

	/// One quad of a piece of text, in layout units (box-local, +y down).
	struct TextQuad {
		glm::vec4     rect{ 0.0f };                       ///< Min x, min y, max x, max y. Shadow offset and bold shift included.
		glm::vec4     uv{ 0.0f, 0.0f, 1.0f, 1.0f };       ///< Atlas rect (0,0 = top-left). Underlines: the whole texture.
		glm::vec4     colour{ 1.0f };                     ///< sRGB, straight alpha, never 0 alpha (those are left out).
		UITextEffects effects{};                          ///< Distance-field fonts only; default otherwise.
		glm::vec2     pen{ 0.0f };                        ///< Glyphs and bold copies: the glyph's pen on the baseline (+ shadow offset) - where bitmap text snaps to pixels.
		glm::vec4     glyphRect{ 0.0f };                  ///< Glyphs and bold copies: the glyph's own rect (+ shadow offset) before any bold shift - what "does it fit the box" tests. Underlines: = rect.
		TextQuadKind  kind = TextQuadKind::Glyph;
		bool          shadow = false;                     ///< Part of the drop shadow.
	};

	/// BuildTextQuads' output. Reuse one between calls: it is cleared first, its memory kept.
	struct TextQuads {
		std::vector<TextQuad> quads;    ///< In draw order: the shadow (glyphs, then underlines), then the text (the same).
		bool                  field = false;   ///< Distance-field font with a pixel range: draw glyphs with the font's pixelRange and each quad's effects.
		bool                  bitmap = false;  ///< Bitmap font: glyphs may be snapped to whole pixels.
	};

	/*************************************************************************
	 \brief  Builds the quads that draw `layout` (laid out with `font`) in `style`.
	 \param  pixelsPerUnit  Screen pixels per layout unit, if known (UIText: the
	                        canvas scale). Only bitmap bold uses it: copies at most
	                        a pixel apart. 0 = the font's baked size is assumed.
	 \param[out] out        Cleared first.
	 *************************************************************************/
	ENGINE_API void BuildTextQuads(const FontAsset& font, const TextLayoutResult& layout, const TextStyle& style,
	                               float pixelsPerUnit, TextQuads& out);

	/// Field units (see UITextEffects) per layout unit, for text of `fontSize` in this font.
	/// 0 for bitmap fonts (no field).
	ENGINE_API float TextEffectFieldPerUnit(const FontAsset& font, float fontSize);

	/// The widest outline + glow (in layout units) text of `fontSize` in this font can have:
	/// kTextMaxField of the pixel range past each edge. 0 for bitmap fonts.
	ENGINE_API float MaxTextEffectWidth(const FontAsset& font, float fontSize);

	/*************************************************************************
	 \brief  Laid-out text kept between frames, so a piece of text is only laid
	         out again when something that changes its layout changes - the
	         text, a setting, the font, or the font being reloaded.
	 \details Entries are found by a key the caller makes (KeyFor: entity +
	         bitmap size group). Call Tick once per frame (or per build of all
	         text); entries not used for m_keepTicks ticks are dropped, so
	         deleted text doesn't linger. Missing characters are warned about
	         once per font and character.
	 *************************************************************************/
	class ENGINE_API TextLayoutCache {
	public:
		/// `label` starts the missing-character warnings, e.g. "UI" -> "[UI] Entity 12: ...".
		explicit TextLayoutCache(std::string label) : m_label(std::move(label)) {}

		/// A key from an entity's index and generation and the bitmap size group used
		/// (so two views that pick different baked sizes keep separate entries).
		static std::uint64_t KeyFor(std::uint32_t index, std::uint32_t generation, std::size_t sizeGroup) {
			return (static_cast<std::uint64_t>(generation & 0xFFFFu) << 48)
			     | (static_cast<std::uint64_t>(sizeGroup & 0xFFu) << 40)
			     | static_cast<std::uint64_t>(index);
		}

		/// Advances the clock, and every 256 ticks drops entries unused for m_keepTicks.
		void Tick();

		/// The layout of `text`, from the cache or laid out now. `entityIndex` is only for warnings.
		/// The reference is valid until the next Get, Tick or Clear.
		const TextLayoutResult& Get(std::uint64_t key, std::uint32_t entityIndex, const FontAsset& font,
		                            const std::string& text, const TextLayoutSettings& settings);

		std::size_t Size() const { return m_entries.size(); }
		void        Clear() { m_entries.clear(); }

	private:
		struct Entry {
			const FontAsset*   font = nullptr;
			std::uint32_t      texture = TextureHandle::Invalid;   // changes when the font is reloaded, even at the same address
			std::string        text;
			TextLayoutSettings settings;
			TextLayoutResult   result;
			std::uint64_t      lastUse = 0;
		};

		static constexpr std::uint64_t m_keepTicks = 600;   // entries unused this long are dropped

		std::string                               m_label;
		std::unordered_map<std::uint64_t, Entry>  m_entries;
		std::uint64_t                             m_clock = 0;
		std::unordered_set<std::uint64_t>         m_warnedMissing;   // (font texture, codepoint) already reported
	};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

}   // NAMESPACE CRUCIBLE
