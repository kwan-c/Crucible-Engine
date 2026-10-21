/*************************************************************************
  \file       UISystem.cpp
  \brief      See UISystem.h.
*************************************************************************/
#include "UI/UISystem.h"

#include "ECS/Components/Components.h"
#include "ECS/Systems/EventBus/EventWriter.h"   // ctx.events.EmitEngine (SystemContext only forward-declares it)
#include "Input/InputSystem.h"
#include "UI/UIEvents.h"
#include "AssetPipe/Assetmanager.h"
#include "Diagnostics/Logger.h"
#include "ECS/World.h"
#include "Graphics/RenderSystem.h"
#include "Resources/ResourceManager.h"
#include "Scene/SceneManager.h"
#include "Text/TextLayout.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Crucible {

	namespace {

		// 2D transforms as 3x3 matrices (column vectors, translation in the last column).
		glm::mat3 Translate(Math::vec2 t) { glm::mat3 m(1.0f); m[2] = glm::vec3(t, 1.0f); return m; }

		// Counter-clockwise ON SCREEN by `degrees`. Screen y points down, so this is the usual
		// rotation matrix with the angle negated: +90 turns "right" into "up".
		glm::mat3 RotateOnScreen(float degrees) {
			const float a = glm::radians(degrees), c = std::cos(a), s = std::sin(a);
			glm::mat3 m(1.0f);
			m[0][0] = c;  m[0][1] = -s;   // where the x axis goes
			m[1][0] = s;  m[1][1] = c;    // where the y axis goes
			return m;
		}

		// One element and everything under it. Returns early for an entity without a UIRect:
		// its subtree is not part of the UI (see the file header). parentRect is in the parent's
		// own space, and parentToScreen maps that space to the screen.
		void LayoutNode(World& world, Entity entity, const Math::vec4& parentRect, const glm::mat3& parentToScreen, float scale,
		                std::vector<UI::LayoutItem>& out, bool writeComputed) {
			const UIRect* rect = world.Read<UIRect>(entity);
			if (rect == nullptr) { return; }

			const Math::vec2 parentMin(parentRect.x, parentRect.y);
			const Math::vec2 parentSize(parentRect.z - parentRect.x, parentRect.w - parentRect.y);

			// The anchor box inside the parent. Anchors together: a point, so size is the size.
			// Anchors apart: the box stretches with the parent and size is added on top.
			const Math::vec2 anchorMin = parentMin + parentSize * rect->anchorMin;
			const Math::vec2 anchorMax = parentMin + parentSize * rect->anchorMax;
			const Math::vec2 size      = (anchorMax - anchorMin) + rect->size * scale;
			const Math::vec2 pivotAt   = glm::mix(anchorMin, anchorMax, rect->pivot) + rect->position * scale;
			const Math::vec2 min       = pivotAt - size * rect->pivot;
			const Math::vec4 placed(min.x, min.y, min.x + size.x, min.y + size.y);

			// Upright in the parent's space, then turned around its own pivot. No rotation: the
			// parent's transform as it is, so un-rotated UI costs no extra maths.
			const glm::mat3 toScreen = rect->rotation == 0.0f
				? parentToScreen
				: parentToScreen * Translate(pivotAt) * RotateOnScreen(rect->rotation) * Translate(-pivotAt);

			UI::LayoutItem item{ .entity = entity, .rect = placed, .pivot = pivotAt, .toScreen = toScreen,
			                     .parentToScreen = parentToScreen, .scale = scale };

			// Only when it changed: a mutable access stamps the component as changed.
			if (writeComputed) {
				const Math::vec4 bounds = item.Bounds();
				if (rect->computed != bounds) {
					if (UIRect* writable = world.Write<UIRect>(entity)) { writable->computed = bounds; }
				}
			}
			out.push_back(item);

			world.ForEachChild(entity, [&](Entity child) { LayoutNode(world, child, placed, toScreen, scale, out, writeComputed); });
		}

		// ---- Sliders ------------------------------------------------------------------------------ //
		// Everything is worked out in the slider's own upright space (its UIRect's rect), so a turned
		// slider is just the same parts drawn through its transform.

		bool IsVertical(UISlider::Direction d) { return d == UISlider::Direction::BottomToTop || d == UISlider::Direction::TopToBottom; }

		// Reversed: the value grows towards smaller x / y. Screen y points down, so "min at the
		// bottom" (BottomToTop) is the reversed vertical one.
		bool IsReversed(UISlider::Direction d) { return d == UISlider::Direction::RightToLeft || d == UISlider::Direction::BottomToTop; }

		float SliderClamp(const UISlider& slider, float value) {
			const float lo = std::min(slider.minValue, slider.maxValue), hi = std::max(slider.minValue, slider.maxValue);
			value = std::clamp(value, lo, hi);
			return slider.wholeNumbers ? std::round(value) : value;
		}

		// 0 at min, 1 at max (0 when min == max).
		float SliderFraction(const UISlider& slider) {
			const float range = slider.maxValue - slider.minValue;
			if (std::abs(range) < 1.0e-6f) { return 0.0f; }
			return std::clamp((SliderClamp(slider, slider.value) - slider.minValue) / range, 0.0f, 1.0f);
		}

		struct SliderGeometry {
			float start;       // where the slider begins along its axis
			float length;      // its length along the axis
			float handle;      // the handle's length along the axis
			float crossMin;    // its extent across the axis
			float crossMax;
		};

		SliderGeometry MeasureSlider(const Math::vec4& r, const UISlider& slider, float scale) {
			const bool vertical = IsVertical(slider.direction);
			SliderGeometry g{};
			g.start    = vertical ? r.y : r.x;
			g.length   = vertical ? r.w - r.y : r.z - r.x;
			g.crossMin = vertical ? r.x : r.y;
			g.crossMax = vertical ? r.z : r.w;
			g.handle   = std::clamp(slider.handleSize * scale, 0.0f, std::max(g.length, 0.0f));
			return g;
		}

		struct SliderParts { Math::vec4 track, fill, handle; };

		// The track runs the full length; the handle's centre travels between half a handle in from
		// each end, so it never hangs off the slider; the fill runs from the min end to the handle.
		SliderParts LayoutSlider(const Math::vec4& r, const UISlider& slider, float scale) {
			const bool vertical = IsVertical(slider.direction);
			const SliderGeometry g = MeasureSlider(r, slider, scale);

			float t = SliderFraction(slider);
			if (IsReversed(slider.direction)) { t = 1.0f - t; }
			const float centre = g.start + g.handle * 0.5f + (g.length - g.handle) * t;

			const float crossMid  = (g.crossMin + g.crossMax) * 0.5f;
			const float thickness = (g.crossMax - g.crossMin) * std::clamp(slider.trackThickness, 0.0f, 1.0f);
			const float trackMin  = crossMid - thickness * 0.5f, trackMax = crossMid + thickness * 0.5f;

			// Along-axis spans, turned into rects for the slider's orientation.
			auto span = [&](float a0, float a1, float c0, float c1) {
				return vertical ? Math::vec4(c0, a0, c1, a1) : Math::vec4(a0, c0, a1, c1);
			};
			const float end = g.start + g.length;
			SliderParts parts;
			parts.track  = span(g.start, end, trackMin, trackMax);
			parts.fill   = IsReversed(slider.direction) ? span(centre, end, trackMin, trackMax) : span(g.start, centre, trackMin, trackMax);
			parts.handle = span(centre - g.handle * 0.5f, centre + g.handle * 0.5f, g.crossMin, g.crossMax);
			return parts;
		}

		// The value under a point in the slider's own space (pressing jumps there, dragging follows).
		float SliderValueAt(const Math::vec4& r, const UISlider& slider, float scale, Math::vec2 local) {
			const SliderGeometry g = MeasureSlider(r, slider, scale);
			const float along  = IsVertical(slider.direction) ? local.y : local.x;
			const float usable = g.length - g.handle;
			float t = usable > 1.0e-6f ? std::clamp((along - g.start - g.handle * 0.5f) / usable, 0.0f, 1.0f) : 0.0f;
			if (IsReversed(slider.direction)) { t = 1.0f - t; }
			return SliderClamp(slider, slider.minValue + t * (slider.maxValue - slider.minValue));
		}

		// Handle tint for hover / drag, like a button's; a disabled slider fades as a whole.
		Math::vec4 SliderHandleTint(const UISlider& slider) {
			switch (slider.state) {
				case UIButton::State::Hovered: return Math::vec4(0.9f, 0.9f, 0.9f, 1.0f);
				case UIButton::State::Pressed: return Math::vec4(0.75f, 0.75f, 0.75f, 1.0f);
				default:                       return Math::vec4(1.0f);
			}
		}
		Math::vec4 SliderFade(const UISlider& slider) {
			return slider.interactable ? Math::vec4(1.0f) : Math::vec4(0.6f, 0.6f, 0.6f, 0.5f);
		}

		// The colour a button's current state multiplies its image by.
		// ---- Text ------------------------------------------------------------------------------

		// Laid-out text, kept between frames: a label is only laid out again when something that
		// changes its layout changes. Keyed by entity + bitmap size group (the game and the UI
		// Editor tab can pick different baked sizes for the same label). Layout is in reference
		// pixels, so both views share an entry otherwise.
		struct TextCacheEntry {
			const FontAsset*   font = nullptr;
			std::uint32_t      texture = TextureHandle::Invalid;   // changes when the font is reloaded, even at the same address
			std::string        text;
			TextLayoutSettings settings;
			TextLayoutResult   result;
			std::uint64_t      lastUse = 0;
		};
		std::unordered_map<std::uint64_t, TextCacheEntry> g_textCache;
		std::uint64_t                                     g_textClock = 0;     // one tick per AppendDrawList call
		std::unordered_set<std::uint64_t>                 g_warnedMissing;     // (font, codepoint) already reported

		constexpr std::uint64_t kTextCacheKeepTicks = 600;   // entries unused this long are dropped (deleted labels)

		bool SameSettings(const TextLayoutSettings& a, const TextLayoutSettings& b) {
			return a.fontSize == b.fontSize && a.box == b.box && a.alignH == b.alignH && a.alignV == b.alignV
			    && a.wrap == b.wrap && a.overflow == b.overflow && a.lineSpacing == b.lineSpacing
			    && a.characterSpacing == b.characterSpacing && a.sizeGroup == b.sizeGroup
			    && a.richText == b.richText && a.bold == b.bold && a.underline == b.underline && a.bestFit == b.bestFit
			    && a.minFontSize == b.minFontSize && a.maxFontSize == b.maxFontSize && a.pixelScale == b.pixelScale;
		}

		const TextLayoutResult& LayoutCached(Entity entity, const FontAsset& font, const std::string& text, const TextLayoutSettings& settings) {
			const std::uint64_t key = (static_cast<std::uint64_t>(entity.generation & 0xFFFFu) << 48)
			                        | (static_cast<std::uint64_t>(settings.sizeGroup & 0xFFu) << 40)
			                        | static_cast<std::uint64_t>(entity.index);
			TextCacheEntry& entry = g_textCache[key];
			entry.lastUse = g_textClock;
			if (entry.font != &font || entry.texture != font.texture.id || entry.text != text || !SameSettings(entry.settings, settings)) {
				entry.font     = &font;
				entry.texture  = font.texture.id;
				entry.text     = text;
				entry.settings = settings;
				LayoutText(font.data, text, settings, entry.result);

				for (const std::uint32_t cp : entry.result.missing) {
					const std::uint64_t warnKey = (static_cast<std::uint64_t>(font.texture.id) << 32) | cp;
					if (g_warnedMissing.insert(warnKey).second) {
						CRU_USER_WARN("[UI] Entity {}: the font has no character U+{:04X}, drawn as '?' - add it in the Font Importer",
						              entity.index, cp);
					}
				}
			}
			return entry.result;
		}

		constexpr float kMaxField      = 0.49f;                    // how far outside the edge effects may reach, field units (the field ends at 0.5)
		constexpr float kBoldDilateEm  = kTextBoldWidthEm * 0.5f;  // distance-field bold: each side moves out by half the extra advance

		void PruneTextCache() {
			if (g_textClock % 256 != 0) { return; }
			for (auto it = g_textCache.begin(); it != g_textCache.end();) {
				if (g_textClock - it->second.lastUse > kTextCacheKeepTicks) { it = g_textCache.erase(it); } else { ++it; }
			}
		}

		Math::vec4 ButtonTint(const UIButton& button) {
			if (!button.interactable || button.state == UIButton::State::Disabled) { return button.disabled; }
			switch (button.state) {
				case UIButton::State::Pressed: return button.pressed;
				case UIButton::State::Hovered: return button.hovered;
				default:                       return button.normal;
			}
		}

	}   // namespace

	// ------------------------------------------------------------------------------------------- //
	//  LAYOUT AND DRAWING (shared with the editor)
	// ------------------------------------------------------------------------------------------- //

	float UI::CanvasScale(const UICanvas& canvas, Math::vec2 screenSize) {
		if (!canvas.scaleWithScreenSize) { return 1.0f; }
		if (canvas.referenceResolution.x <= 0.0f || canvas.referenceResolution.y <= 0.0f) { return 1.0f; }
		return std::min(screenSize.x / canvas.referenceResolution.x, screenSize.y / canvas.referenceResolution.y);
	}

	void UI::LayoutScreenCanvases(World& world, Math::vec2 screenSize, std::vector<LayoutItem>& out, bool writeComputed) {
		out.clear();
		if (screenSize.x <= 0.0f || screenSize.y <= 0.0f) { return; }

		// Collect first, lay out after: layout writes components, which isn't allowed while a
		// query is iterating. A canvas under another canvas belongs to that canvas's tree.
		struct Root { int sortOrder; Entity entity; float scale; };
		std::vector<Root> roots;
		world.CreateQuery<const UICanvas>().ForEach([&](Entity entity, const UICanvas& canvas) {
			if (canvas.renderMode != UICanvas::RenderMode::ScreenSpace || !canvas.enabled) { return; }   // hidden: not drawn, no clicks
			roots.push_back({ canvas.sortOrder, entity, CanvasScale(canvas, screenSize) });
		});
		std::erase_if(roots, [&](const Root& root) {
			for (Entity p = world.GetParent(root.entity); p.IsValid(); p = world.GetParent(p)) {
				if (world.HasComponent<UICanvas>(p)) { return true; }
			}
			return false;
		});
		std::stable_sort(roots.begin(), roots.end(), [](const Root& a, const Root& b) { return a.sortOrder < b.sortOrder; });

		const Math::vec4 screenRect(0.0f, 0.0f, screenSize.x, screenSize.y);
		for (const Root& root : roots) {
			world.ForEachChild(root.entity, [&](Entity child) { LayoutNode(world, child, screenRect, glm::mat3(1.0f), root.scale, out, writeComputed); });
		}
	}

	void UI::AppendDrawList(World& world, const std::vector<LayoutItem>& items, ResourceManager* resources,
	                        std::vector<UIVertex>& vertices, std::vector<std::uint32_t>& indices,
	                        std::vector<UIDrawCommand>& commands) {

		++g_textClock;
		PruneTextCache();

		// One quad: `local` is a rect in the element's own (upright) space, so it goes through the
		// element's transform and a turned element draws turned. Merged into the previous draw when
		// the texture, mode, clip and range match and it follows straight on - so a whole label is
		// one draw. `uv` is the texture rect (0,0 = top-left); `clip` is in screen (canvas) pixels.
		const Math::vec4 noClip = UIDrawCommand{}.clipRect;
		auto quad = [&](const LayoutItem& item, const Math::vec4& local, const Math::vec4& colour, TextureHandle texture,
		                const Math::vec4& uv = Math::vec4(0.0f, 0.0f, 1.0f, 1.0f), UIDrawMode mode = UIDrawMode::Image,
		                const Math::vec4* clip = nullptr, float pixelRange = 0.0f, const UITextEffects& effects = UITextEffects{}) {
			if (colour.a <= 0.0f || local.z <= local.x || local.w <= local.y) { return; }   // invisible / empty

			const std::uint32_t packed = PackUIColour(colour);
			const std::uint32_t base   = static_cast<std::uint32_t>(vertices.size());
			vertices.push_back({ item.ToScreen({ local.x, local.y }), { uv.x, uv.y }, packed });
			vertices.push_back({ item.ToScreen({ local.z, local.y }), { uv.z, uv.y }, packed });
			vertices.push_back({ item.ToScreen({ local.z, local.w }), { uv.z, uv.w }, packed });
			vertices.push_back({ item.ToScreen({ local.x, local.w }), { uv.x, uv.w }, packed });

			const std::uint32_t first = static_cast<std::uint32_t>(indices.size());
			for (std::uint32_t i : { 0u, 1u, 2u, 0u, 2u, 3u }) { indices.push_back(base + i); }

			const UIDrawCommand fresh{ .texture = texture, .mode = mode, .clipRect = clip ? *clip : noClip,
			                           .firstIndex = first, .indexCount = 6, .pixelRange = pixelRange, .effects = effects };
			if (!commands.empty()) {
				UIDrawCommand& last = commands.back();
				if (last.texture == texture && last.mode == fresh.mode && last.clipRect == fresh.clipRect &&
				    last.pixelRange == fresh.pixelRange && last.effects == fresh.effects &&
				    last.vertexOffset == 0 && last.firstIndex + last.indexCount == first) {
					last.indexCount += 6;
					return;
				}
			}
			commands.push_back(fresh);
		};

		for (const LayoutItem& item : items) {
			// The image (also a button's picture, or a background behind a slider).
			if (const UIImage* image = world.Read<UIImage>(item.entity)) {
				Math::vec4 colour = image->colour;
				if (const UIButton* button = world.Read<UIButton>(item.entity)) { colour *= ButtonTint(*button); }

				// textureAsset is what's saved; the handle is resolved here every frame, like MeshRenderer.
				TextureHandle texture = image->texture;
				if (resources != nullptr) {
					texture = image->textureAsset != 0 ? resources->GetTexture(image->textureAsset, true) : TextureHandle{};
					if (!(texture == image->texture)) {
						if (UIImage* writable = world.Write<UIImage>(item.entity)) { writable->texture = texture; }
					}
				}
				quad(item, item.rect, colour, texture);
			}

			// A slider draws its own parts on top: track, the filled part, then the handle.
			if (const UISlider* slider = world.Read<UISlider>(item.entity)) {
				const SliderParts parts = LayoutSlider(item.rect, *slider, item.scale);
				const Math::vec4 fade = SliderFade(*slider);   // whole slider, when disabled
				quad(item, parts.track,  slider->trackColour * fade, TextureHandle{});
				quad(item, parts.fill,   slider->fillColour * fade, TextureHandle{});
				quad(item, parts.handle, slider->handleColour * fade * SliderHandleTint(*slider), TextureHandle{});
			}

			// Text last, so a button's label sits on its picture.
			const UIText* text = world.Read<UIText>(item.entity);
			if (text == nullptr || resources == nullptr || text->text.empty() || text->colour.a <= 0.0f || !(item.scale > 0.0f)) { continue; }

			const FontAsset* font = text->fontAsset != 0 ? resources->GetFont(text->fontAsset) : nullptr;
			if (font == nullptr) { font = resources->GetDefaultFont(); }   // no font picked, or it can't be loaded
			if (font == nullptr || !font->texture.IsValid()) { continue; }

			// Laid out in reference pixels (the rect divided by the canvas scale), then scaled back up,
			// so the layout is the same at any screen size.
			const float scale = item.scale;
			TextLayoutSettings settings;
			settings.fontSize         = text->fontSize;
			settings.box              = { (item.rect.z - item.rect.x) / scale, (item.rect.w - item.rect.y) / scale };
			settings.alignH           = text->horizontalAlign;
			settings.alignV           = text->verticalAlign;
			settings.wrap             = text->wrap;
			settings.overflow         = text->overflow;
			settings.lineSpacing      = text->lineSpacing;
			settings.characterSpacing = text->characterSpacing;
			settings.richText         = text->richText;
			settings.bold             = text->bold;
			settings.underline        = text->underline;
			settings.bestFit          = text->bestFit;
			settings.minFontSize      = text->minFontSize;
			settings.maxFontSize      = text->maxFontSize;
			const bool bitmap         = font->data.outputType == FontOutputType::Bitmap;
			settings.pixelScale       = bitmap ? scale : 0.0f;   // bitmap: the baked size nearest the on-screen size, for whatever size is used
			// Only part of the cache key (so the game and the UI Editor tab, at different scales, keep
			// separate entries instead of taking turns); LayoutText picks the group from pixelScale.
			settings.sizeGroup        = bitmap ? font->data.ClosestGroup((text->bestFit ? text->maxFontSize : text->fontSize) * scale) : 0;

			const TextLayoutResult& layout = LayoutCached(item.entity, *font, text->text, settings);
			if (layout.quads.empty() && layout.underlines.empty()) { continue; }

			// Upright (not turned): the clip rect can cut glyphs at the rect's edges exactly, and bitmap
			// glyphs can be put on whole screen pixels so they stay sharp.
			const glm::mat3& m   = item.toScreen;
			const bool upright   = std::fabs(m[0][1]) < 1.0e-5f && std::fabs(m[1][0]) < 1.0e-5f;
			const bool cut       = text->overflow != TextOverflow::Overflow;
			const Math::vec4 box = upright && cut ? item.Bounds() : noClip;
			const Math::vec2 origin(item.rect.x, item.rect.y);

			// ---- Effects (distance-field fonts), converted from reference pixels into field units:
			// the stored distance goes 0 -> 1 across the atlas's pixel range, edge at 0.5, so
			// everything outside the edge has to fit in 0.5 (kept a little under, at kMaxField).
			const bool  field      = !bitmap && font->data.atlas.pixelRange > 0.0f;
			const float toField    = field ? TextEffectFieldPerPixel(*font, layout.fontSize) : 0.0f;
			const float boldDilate = field ? kBoldDilateEm * layout.fontSize * toField : 0.0f;   // bold widens each side by half its extra advance
			bool anyBold = false;
			for (const TextGlyphQuad& glyph : layout.quads) { anyBold = anyBold || glyph.boldWidth > 0.0f; }
			float budget = kMaxField - (anyBold ? boldDilate : 0.0f);
			const float outlineWidth = (field && text->outline) ? std::clamp(text->outlineWidth * toField, 0.0f, budget) : 0.0f;
			budget -= outlineWidth;
			const float glowWidth    = (field && text->glow) ? std::clamp(text->glowWidth * toField, 0.0f, budget) : 0.0f;

			UITextEffects effects;
			if (outlineWidth > 0.0f) {
				effects.outlineColour = PackUIColour(text->outlineColour);
				effects.outlineWidth  = outlineWidth;
			}
			if (glowWidth > 0.0f) {
				effects.glowColour   = PackUIColour(text->glowColour);
				effects.glowWidth    = glowWidth;
				effects.glowSoftness = std::clamp(text->glowSoftness, 0.0f, 1.0f);
			}

			// Draws every glyph and underline once. `offset` is in element-local units; `shadow` draws
			// them as the drop shadow (flat colour, the shape with its outline, maybe blurred).
			auto drawText = [&](Math::vec2 offset, bool shadow) {
				const Math::vec4 shadowColour(Math::vec3(text->shadowColour), text->shadowColour.a * text->colour.a);

				for (const TextGlyphQuad& glyph : layout.quads) {
					Math::vec4 local(origin.x + glyph.rect.x * scale + offset.x, origin.y + glyph.rect.y * scale + offset.y,
					                 origin.x + glyph.rect.z * scale + offset.x, origin.y + glyph.rect.w * scale + offset.y);

					// Turned: no clip rect can follow the rect, so a glyph that pokes out is left out whole.
					if (cut && !upright && (local.x < item.rect.x - 0.5f || local.z > item.rect.z + 0.5f ||
					                        local.y < item.rect.y - 0.5f || local.w > item.rect.w + 0.5f)) {
						continue;
					}
					if (bitmap && upright && m[0][0] != 0.0f && m[1][1] != 0.0f) {
						const Math::vec2 pen    = item.ToScreen(origin + glyph.pen * scale + offset);
						const Math::vec2 snap   = (glm::round(pen) - pen) / Math::vec2(m[0][0], m[1][1]);
						local += Math::vec4(snap, snap);
					}

					// <color>: its colour, with the component's alpha on top (so fading the text fades it too).
					const Math::vec4 colour = shadow ? shadowColour
						: glyph.hasColour ? Math::vec4(Math::vec3(glyph.colour), glyph.colour.a * text->colour.a) : text->colour;
					const bool bold = glyph.boldWidth > 0.0f;

					if (field) {
						// Bold: the edge moved outward (no extra quads), centred in the wider advance it got.
						UITextEffects fx = effects;
						if (bold) {
							fx.dilate = boldDilate;
							const float dx = glyph.boldWidth * scale * 0.5f;
							local += Math::vec4(dx, 0.0f, dx, 0.0f);
						}
						if (shadow) {
							fx.glowColour     = 0;
							fx.glowWidth      = 0.0f;
							fx.outlineColour  = 0;
							fx.shadowPass     = true;
							const float edge  = 0.5f - fx.dilate - fx.outlineWidth;   // the shape's outer edge
							fx.shadowSoftness = std::clamp(text->shadowSoftness, 0.0f, 1.0f) * std::max(edge - (0.5f - kMaxField), 0.0f);
						}
						quad(item, local, colour, font->texture, glyph.uv, font->drawMode, &box, font->data.atlas.pixelRange, fx);
					} else {
						quad(item, local, colour, font->texture, glyph.uv, font->drawMode, &box);
						// Bitmap bold: the glyph again, in steps of at most one pixel up to boldWidth to the right.
						if (bold) {
							const float width = glyph.boldWidth * scale;
							const int   steps = std::max(1, static_cast<int>(std::ceil(width)));
							for (int step = 1; step <= steps; ++step) {
								const float dx = width * static_cast<float>(step) / static_cast<float>(steps);
								quad(item, local + Math::vec4(dx, 0.0f, dx, 0.0f), colour, font->texture, glyph.uv, font->drawMode, &box);
							}
						}
					}
				}

				// Underlines: solid bars (no texture), at least one screen pixel thick, on whole pixels when upright.
				for (const TextDecoration& bar : layout.underlines) {
					Math::vec4 local(origin.x + bar.rect.x * scale + offset.x, origin.y + bar.rect.y * scale + offset.y,
					                 origin.x + bar.rect.z * scale + offset.x, origin.y + bar.rect.w * scale + offset.y);
					const float thickness = std::max(local.w - local.y, 1.0f);
					if (upright && m[1][1] != 0.0f) {
						const float top = item.ToScreen({ local.x, local.y }).y;
						local.y += (std::round(top) - top) / m[1][1];
					}
					local.w = local.y + thickness;
					if (cut && !upright && (local.x < item.rect.x - 0.5f || local.z > item.rect.z + 0.5f ||
					                        local.y < item.rect.y - 0.5f || local.w > item.rect.w + 0.5f)) {
						continue;
					}
					const Math::vec4 colour = shadow ? shadowColour
						: bar.hasColour ? Math::vec4(Math::vec3(bar.colour), bar.colour.a * text->colour.a) : text->colour;
					quad(item, local, colour, TextureHandle{}, Math::vec4(0.0f, 0.0f, 1.0f, 1.0f), UIDrawMode::Image, &box);
				}
			};

			// The shadow first (any font: bitmap ones get a flat offset copy), then the text over it.
			if (text->shadow && text->shadowColour.a > 0.0f) {
				drawText(Math::vec2(text->shadowOffset) * scale, true);
			}
			drawText(Math::vec2(0.0f), false);
		}
	}

	float UI::TextEffectFieldPerPixel(const FontAsset& font, float fontSize) {
		const float range = font.data.atlas.pixelRange;
		if (font.data.outputType == FontOutputType::Bitmap || range <= 0.0f || fontSize <= 0.0f || font.data.groups.empty()) { return 0.0f; }
		// One reference pixel = (glyphSize / fontSize) atlas texels; one field unit = `range` texels.
		return font.data.groups.front().pixelSize / (fontSize * range);
	}

	float UI::MaxTextEffectWidth(const FontAsset& font, float fontSize) {
		const float perPixel = TextEffectFieldPerPixel(font, fontSize);
		return perPixel > 0.0f ? kMaxField / perPixel : 0.0f;
	}

	// ------------------------------------------------------------------------------------------- //
	//  UIRenderSystem
	// ------------------------------------------------------------------------------------------- //

	void UIRenderSystem::Describe(SystemDesc& desc) const {
		desc.Reads<UICanvas, UIButton, UISlider, UIText, HierarchyNode>()   // canvases, button / slider states, text, the tree
			.Writes<UIRect, UIImage>()                      // UIRect::computed, UIImage::texture
			.Affinity(SystemAffinity::MainThread);          // touches the Renderer
	}

	void UIRenderSystem::Update(World& world, [[maybe_unused]] CommandBuffer& commands) {
		const glm::uvec2 screen = m_renderer.GetGameScreenSize();
		if (screen.x == 0 || screen.y == 0) { return; }   // no Game view: nothing to draw into

		UI::LayoutScreenCanvases(world, Math::vec2(screen), m_layout, true);

		m_vertices.clear();
		m_indices.clear();
		m_commands.clear();
		UI::AppendDrawList(world, m_layout, m_resources, m_vertices, m_indices, m_commands);

		if (!m_commands.empty()) {
			m_renderer.SubmitUI(UIDrawList{ .vertices = m_vertices, .indices = m_indices, .commands = m_commands });
		}
	}

	// ------------------------------------------------------------------------------------------- //
	//  UIMenuSystem
	// ------------------------------------------------------------------------------------------- //

	namespace {
		// One menu state for the whole program. Kept here (not in the header) so the Editor and Game
		// executables share Engine.dll's copy through IsMenuOpen / SetMenuOpen.
		bool g_menuOpen      = false;
		bool g_menuRequested = false;   // SetMenuOpen was called: apply g_menuRequest next Update
		bool g_menuRequest   = false;
		bool g_cursorNeeded  = false;   // UI::IsCursorNeeded
	}

	bool UI::IsCursorNeeded() { return g_cursorNeeded; }

	bool UI::IsMenuOpen() { return g_menuOpen; }

	void UI::SetMenuOpen(bool open) {
		g_menuRequested = true;
		g_menuRequest   = open;
	}

	void UIMenuSystem::Describe(SystemDesc& desc) const {
		desc.Writes<UICanvas>()                   // UICanvas::enabled on the menu canvases
			.SimulationOnly()                     // Play mode only
			.RunBefore<UIInputSystem>()           // a menu opened this frame is hoverable this frame
			.Affinity(SystemAffinity::MainThread);  // touches Input (the cursor)
	}

	void UIMenuSystem::Update(World& world, [[maybe_unused]] CommandBuffer& commands) {

		// Play or Resume just started (the tick jumped while this system was off): start closed.
		// The cursor is left alone here - ECSPlayerControllerSystem locks it as Play starts.
		const std::uint64_t tick = world.CurrentTick();
		const bool sessionStarted = tick != m_lastTick + 1;
		m_lastTick = tick;

		bool open = g_menuOpen;
		if (sessionStarted) { open = false; g_menuRequested = false; m_cursorNeeded = false; }
		if (g_menuRequested) { open = g_menuRequest; g_menuRequested = false; }
		if (Input::IsPressed(KeyCode::Escape)) { open = !open; }
		g_menuOpen = open;

		// Every menu canvas follows the menu (checked every frame: a scene loaded mid-Play brings its
		// menu canvases in visible). Meanwhile, does any canvas that will be shown want the cursor?
		// Collected first, written after: no writes mid-query.
		bool showsCursor = false;
		m_menus.clear();
		world.CreateQuery<const UICanvas>().ForEach([&](Entity entity, const UICanvas& canvas) {
			const bool shown = canvas.toggleWithEscape ? open : canvas.enabled;
			if (canvas.toggleWithEscape && canvas.enabled != open) { m_menus.push_back(entity); }
			if (shown && canvas.showsCursor && canvas.renderMode == UICanvas::RenderMode::ScreenSpace) { showsCursor = true; }
		});
		for (Entity entity : m_menus) {
			if (UICanvas* canvas = world.Write<UICanvas>(entity)) { canvas->enabled = open; }
		}

		// The cursor: free while something needs it; locked once, when that stops (back to
		// mouse-look). Not forced locked every frame, so the editor's own Esc can still free it.
		const bool needed = open || showsCursor;
		if (needed && Input::IsMouseLocked())   { Input::SetMouseLocked(false); }
		else if (!needed && m_cursorNeeded)     { Input::SetMouseLocked(true); }
		m_cursorNeeded = needed;
		g_cursorNeeded = needed;
	}

	// ------------------------------------------------------------------------------------------- //
	//  UIInputSystem
	// ------------------------------------------------------------------------------------------- //

	void UIInputSystem::Describe(SystemDesc& desc) const {
		desc.Reads<UICanvas, UIRect, HierarchyNode>()   // the layout and the tree
			.Writes<UIButton, UISlider>()                 // states, UISlider::value
			.SimulationOnly()                             // Edit-mode clicks belong to the editor
			.Affinity(SystemAffinity::MainThread);        // reads the Renderer and Input
	}

	void UIInputSystem::Update(World& world, SystemContext& ctx) {

		// Where the mouse is on the game screen. Nowhere while the cursor is locked (mouse-look:
		// the hidden cursor still moves, and must not hover or press anything).
		glm::vec2 pointer(0.0f);
		const bool onScreen = !Input::IsMouseLocked() && m_renderer.WindowToGameScreen(Input::GetMousePosition(), pointer);

		// The topmost raycast target under it: the draw order backwards (last drawn = on top).
		Entity target = kNullEntity;
		if (onScreen) {
			const glm::uvec2 screen = m_renderer.GetGameScreenSize();
			UI::LayoutScreenCanvases(world, Math::vec2(screen), m_layout, false);
			for (auto it = m_layout.rbegin(); it != m_layout.rend(); ++it) {
				const UIRect* rect = world.Read<UIRect>(it->entity);
				if (rect && rect->raycastTarget && it->Contains(pointer)) { target = it->entity; break; }
			}
		}

		// The button under the mouse, if the target is one that can be used. A covering panel or a
		// disabled button still blocks (target stays set for IsPointerOverUI), but presses nothing.
		Entity hoveredButton = kNullEntity;
		if (target.IsValid()) {
			if (const UIButton* button = world.Read<UIButton>(target); button && button->interactable) { hoveredButton = target; }
		}

		// The slider under the mouse, if it can be used.
		Entity hoveredSlider = kNullEntity;
		if (target.IsValid()) {
			if (const UISlider* slider = world.Read<UISlider>(target); slider && slider->interactable) { hoveredSlider = target; }
		}

		// Press and click. A press that started on nothing (or on a blocker) clicks nothing.
		const bool down = Input::IsDown(KeyCode::MouseLeft);
		if (Input::IsPressed(KeyCode::MouseLeft)) { m_pressed = hoveredButton; m_dragSlider = hoveredSlider; }
		if (m_dragSlider.IsValid() && (!down || !world.IsAlive(m_dragSlider))) { m_dragSlider = kNullEntity; }   // released / destroyed

		// Slider drag: the value follows the mouse - from the press (a jump to where it landed) until
		// release, even outside the slider. Off the game screen it just holds its value.
		if (m_dragSlider.IsValid() && onScreen) {
			const UISlider* slider = world.Read<UISlider>(m_dragSlider);
			const UI::LayoutItem* item = nullptr;
			for (const UI::LayoutItem& candidate : m_layout) { if (candidate.entity == m_dragSlider) { item = &candidate; break; } }
			if (slider && item) {
				const Math::vec2 local = Math::vec2(glm::inverse(item->toScreen) * glm::vec3(pointer, 1.0f));
				const float value = SliderValueAt(item->rect, *slider, item->scale, local);
				if (value != slider->value) {
					if (UISlider* writable = world.Write<UISlider>(m_dragSlider)) { writable->value = value; }
					ctx.events.EmitEngine(UIEvents::SliderChanged{ m_dragSlider, value });
				}
			}
		}
		if (m_pressed.IsValid() && !world.IsAlive(m_pressed)) { m_pressed = kNullEntity; }   // destroyed mid-press
		if (Input::IsReleased(KeyCode::MouseLeft)) {
			if (m_pressed.IsValid() && m_pressed == hoveredButton) {
				ctx.events.EmitEngine(UIEvents::ButtonClicked{ m_pressed });
				RunClickAction(world, m_pressed);
			}
			m_pressed = kNullEntity;
		}

		// Every button's state. Collected first and written after the loop, and only where it
		// changed, so steady frames stamp nothing as changed.
		m_stateChanges.clear();
		world.CreateQuery<const UIButton>().ForEach([&](Entity entity, const UIButton& button) {
			UIButton::State state = UIButton::State::Normal;
			if (!button.interactable)          { state = UIButton::State::Disabled; }
			else if (entity == hoveredButton)  { state = (entity == m_pressed && down) ? UIButton::State::Pressed : UIButton::State::Hovered; }
			if (button.state != state) { m_stateChanges.emplace_back(entity, state); }
		});
		for (const auto& [entity, state] : m_stateChanges) {
			if (UIButton* button = world.Write<UIButton>(entity)) { button->state = state; }
		}

		// Every slider's state: Pressed while dragged (wherever the mouse is), Hovered under it.
		m_stateChanges.clear();
		world.CreateQuery<const UISlider>().ForEach([&](Entity entity, const UISlider& slider) {
			UIButton::State state = UIButton::State::Normal;
			if (!slider.interactable)          { state = UIButton::State::Disabled; }
			else if (entity == m_dragSlider)   { state = UIButton::State::Pressed; }
			else if (entity == hoveredSlider)  { state = UIButton::State::Hovered; }
			if (slider.state != state) { m_stateChanges.emplace_back(entity, state); }
		});
		for (const auto& [entity, state] : m_stateChanges) {
			if (UISlider* slider = world.Write<UISlider>(entity)) { slider->state = state; }
		}

		// Gameplay asks this before acting on a click (Input::IsPointerOverUI).
		Input::SetPointerOverUI(target.IsValid() || (m_pressed.IsValid() && down) || m_dragSlider.IsValid());
	}

	void UIInputSystem::RunClickAction(World& world, Entity entity) {
		const UIButton* button = world.Read<UIButton>(entity);
		if (!button) { return; }

		switch (button->onClick) {
			case UIButton::ClickAction::None:
				break;

			case UIButton::ClickAction::CloseMenu:
				UI::SetMenuOpen(false);
				break;

			case UIButton::ClickAction::LoadScene: {
				if (!m_scenes || !m_assets) {
					CRU_USER_WARN("UI button (entity {}): can't load a scene - no scene manager", entity.index);
					break;
				}
				if (button->sceneAsset == 0) {
					CRU_USER_WARN("UI button (entity {}): onClick is LoadScene but no scene is picked", entity.index);
					break;
				}
				const AssetEntry* scene = m_assets->FindByGuid(xresource::instance_guid{ button->sceneAsset });
				if (!scene) {
					CRU_USER_WARN("UI button (entity {}): its scene ({:X}) is no longer in the project", entity.index, button->sceneAsset);
					break;
				}
				// Applied at the next frame's safe point (SceneManager::ApplyPending), never mid-system.
				// Close the menu first, so the new level starts in mouse-look (UIMenuSystem locks the
				// cursor when nothing needs it any more).
				UI::SetMenuOpen(false);
				if (!m_scenes->RequestLoad(scene->absolutePath)) {
					CRU_USER_WARN("UI button (entity {}): scene file '{}' is missing", entity.index, scene->virtualPath);
				}
				break;
			}
		}
	}

}   // NAMESPACE CRUCIBLE
