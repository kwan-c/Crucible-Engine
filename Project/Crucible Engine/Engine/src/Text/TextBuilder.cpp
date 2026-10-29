/*************************************************************************
  \file       TextBuilder.cpp
  \brief      See TextBuilder.h.
*************************************************************************/
#include "Text/TextBuilder.h"

#include <algorithm>
#include <cmath>

#include "Diagnostics/Logger.h"

namespace Crucible {

	namespace {

		bool SameSettings(const TextLayoutSettings& a, const TextLayoutSettings& b) {
			return a.fontSize == b.fontSize && a.box == b.box && a.alignH == b.alignH && a.alignV == b.alignV
			    && a.wrap == b.wrap && a.overflow == b.overflow && a.lineSpacing == b.lineSpacing
			    && a.characterSpacing == b.characterSpacing && a.sizeGroup == b.sizeGroup
			    && a.richText == b.richText && a.bold == b.bold && a.underline == b.underline && a.bestFit == b.bestFit
			    && a.minFontSize == b.minFontSize && a.maxFontSize == b.maxFontSize && a.pixelScale == b.pixelScale;
		}

		glm::vec4 Shift(const glm::vec4& rect, glm::vec2 by) { return rect + glm::vec4(by, by); }

	}   // namespace

	// ------------------------------------------------------------------------------------------- //
	//  Quads
	// ------------------------------------------------------------------------------------------- //

	void BuildTextQuads(const FontAsset& font, const TextLayoutResult& layout, const TextStyle& style,
	                    float pixelsPerUnit, TextQuads& out) {
		out.quads.clear();
		out.bitmap = font.data.m_outputType == FontOutputType::Bitmap;
		out.field  = !out.bitmap && font.data.m_atlas.pixelRange > 0.0f;
		if (layout.quads.empty() && layout.underlines.empty()) { return; }

		// ---- Effects, from layout units into field units. Bold first, then the outline, then the
		// glow, each out of what is left of kTextMaxField.
		const bool  field      = out.field;
		const float toField    = field ? TextEffectFieldPerUnit(font, layout.fontSize) : 0.0f;
		const float boldDilate = field ? kTextBoldDilateEm * layout.fontSize * toField : 0.0f;
		bool anyBold = false;
		for (const TextGlyphQuad& glyph : layout.quads) { anyBold = anyBold || glyph.boldWidth > 0.0f; }
		float budget = kTextMaxField - (anyBold ? boldDilate : 0.0f);
		const float outlineWidth = (field && style.outline) ? std::clamp(style.outlineWidth * toField, 0.0f, budget) : 0.0f;
		budget -= outlineWidth;
		const float glowWidth    = (field && style.glow) ? std::clamp(style.glowWidth * toField, 0.0f, budget) : 0.0f;

		UITextEffects effects;
		if (outlineWidth > 0.0f) {
			effects.outlineColour = PackUIColour(style.outlineColour);
			effects.outlineWidth  = outlineWidth;
		}
		if (glowWidth > 0.0f) {
			effects.glowColour   = PackUIColour(style.glowColour);
			effects.glowWidth    = glowWidth;
			effects.glowSoftness = std::clamp(style.glowSoftness, 0.0f, 1.0f);
		}

		// Bitmap bold: copies at most a screen pixel apart, up to boldWidth to the right.
		float bitmapPixelsPerUnit = pixelsPerUnit;
		if (!(bitmapPixelsPerUnit > 0.0f) && out.bitmap && layout.fontSize > 0.0f && layout.sizeGroup < font.data.m_groups.size()) {
			bitmapPixelsPerUnit = font.data.m_groups[layout.sizeGroup].pixelSize / layout.fontSize;   // as baked
		}

		const glm::vec4 shadowColour(glm::vec3(style.shadowColour), style.shadowColour.a * style.colour.a);

		// Every glyph and underline once. `shadow`: as the drop shadow (flat colour, the shape with
		// its outline, maybe blurred), moved by `offset`.
		auto addPass = [&](glm::vec2 offset, bool shadow) {
			for (const TextGlyphQuad& glyph : layout.quads) {
				// <color>: its colour, with the text's alpha on top (so fading the text fades it too).
				const glm::vec4 colour = shadow ? shadowColour
					: glyph.hasColour ? glm::vec4(glm::vec3(glyph.colour), glyph.colour.a * style.colour.a) : style.colour;
				if (colour.a <= 0.0f) { continue; }

				TextQuad quad;
				quad.rect      = Shift(glyph.rect, offset);
				quad.uv        = glyph.uv;
				quad.colour    = colour;
				quad.pen       = glyph.pen + offset;
				quad.glyphRect = quad.rect;
				quad.kind      = TextQuadKind::Glyph;
				quad.shadow    = shadow;
				const bool bold = glyph.boldWidth > 0.0f;

				if (field) {
					// Bold: the edge moved outward (no extra quads), centred in the wider advance it got.
					quad.effects = effects;
					if (bold) {
						quad.effects.dilate = boldDilate;
						quad.rect = Shift(quad.rect, glm::vec2(glyph.boldWidth * 0.5f, 0.0f));
					}
					if (shadow) {
						quad.effects.glowColour     = 0;
						quad.effects.glowWidth      = 0.0f;
						quad.effects.outlineColour  = 0;
						quad.effects.shadowPass     = true;
						const float edge = 0.5f - quad.effects.dilate - quad.effects.outlineWidth;   // the shape's outer edge
						quad.effects.shadowSoftness = std::clamp(style.shadowSoftness, 0.0f, 1.0f) * std::max(edge - (0.5f - kTextMaxField), 0.0f);
					}
					out.quads.push_back(quad);
				} else {
					out.quads.push_back(quad);
					if (bold) {
						const float width = glyph.boldWidth * bitmapPixelsPerUnit;   // in pixels
						const int   steps = std::max(1, static_cast<int>(std::ceil(width)));
						for (int step = 1; step <= steps; ++step) {
							TextQuad copy = quad;
							copy.kind = TextQuadKind::BoldCopy;
							copy.rect = Shift(quad.rect, glm::vec2(glyph.boldWidth * static_cast<float>(step) / static_cast<float>(steps), 0.0f));
							out.quads.push_back(copy);
						}
					}
				}
			}

			for (const TextDecoration& bar : layout.underlines) {
				const glm::vec4 colour = shadow ? shadowColour
					: bar.hasColour ? glm::vec4(glm::vec3(bar.colour), bar.colour.a * style.colour.a) : style.colour;
				if (colour.a <= 0.0f) { continue; }

				TextQuad quad;
				quad.rect      = Shift(bar.rect, offset);
				quad.colour    = colour;
				quad.glyphRect = quad.rect;
				quad.kind      = TextQuadKind::Underline;
				quad.shadow    = shadow;
				out.quads.push_back(quad);
			}
		};

		// The shadow first (any font: bitmap ones get a flat offset copy), then the text over it.
		if (style.shadow && style.shadowColour.a > 0.0f) {
			addPass(style.shadowOffset, true);
		}
		addPass(glm::vec2(0.0f), false);
	}

	float TextEffectFieldPerUnit(const FontAsset& font, float fontSize) {
		const float range = font.data.m_atlas.pixelRange;
		if (font.data.m_outputType == FontOutputType::Bitmap || range <= 0.0f || fontSize <= 0.0f || font.data.m_groups.empty()) { return 0.0f; }
		// One layout unit = (glyphSize / fontSize) atlas texels; one field unit = `range` texels.
		return font.data.m_groups.front().pixelSize / (fontSize * range);
	}

	float MaxTextEffectWidth(const FontAsset& font, float fontSize) {
		const float perUnit = TextEffectFieldPerUnit(font, fontSize);
		return perUnit > 0.0f ? kTextMaxField / perUnit : 0.0f;
	}

	// ------------------------------------------------------------------------------------------- //
	//  TextLayoutCache
	// ------------------------------------------------------------------------------------------- //

	void TextLayoutCache::Tick() {
		++m_clock;
		if (m_clock % 256 != 0) { return; }
		for (auto it = m_entries.begin(); it != m_entries.end();) {
			if (m_clock - it->second.lastUse > m_keepTicks) { it = m_entries.erase(it); } else { ++it; }
		}
	}

	const TextLayoutResult& TextLayoutCache::Get(std::uint64_t key, std::uint32_t entityIndex, const FontAsset& font,
	                                             const std::string& text, const TextLayoutSettings& settings) {
		Entry& entry = m_entries[key];
		entry.lastUse = m_clock;
		if (entry.font != &font || entry.texture != font.texture.id || entry.text != text || !SameSettings(entry.settings, settings)) {
			entry.font     = &font;
			entry.texture  = font.texture.id;
			entry.text     = text;
			entry.settings = settings;
			LayoutText(font.data, text, settings, entry.result);

			for (const std::uint32_t cp : entry.result.missing) {
				const std::uint64_t warnKey = (static_cast<std::uint64_t>(font.texture.id) << 32) | cp;
				if (m_warnedMissing.insert(warnKey).second) {
					CRU_USER_WARN("[{}] Entity {}: the font has no character U+{:04X}, drawn as '?' - add it in the Font Importer",
					              m_label, entityIndex, cp);
				}
			}
		}
		return entry.result;
	}

}   // NAMESPACE CRUCIBLE
