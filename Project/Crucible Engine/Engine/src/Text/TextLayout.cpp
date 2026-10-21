/*************************************************************************
  \file       TextLayout.cpp
  \brief      See TextLayout.h.
*************************************************************************/
#include "Text/TextLayout.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace Crucible {

	namespace {

		constexpr float kFitSlack = 0.01f;   // so a box exactly as wide as the text still fits it, despite float rounding

		// ---- Rich text --------------------------------------------------------------------------

		// One character after the tags are taken out, with the style the tags gave it.
		struct StyledCp {
			std::uint32_t codepoint = 0;
			std::uint8_t  colour = 0;      // index into the colour table; 0 = the text's own colour
			bool          bold = false;
			bool          underline = false;
		};

		struct NamedColour { const char* name; std::uint32_t rgba; };
		constexpr NamedColour kNamedColours[] = {
			{ "black", 0x000000FFu }, { "white", 0xFFFFFFFFu }, { "red", 0xFF0000FFu }, { "green", 0x00FF00FFu },
			{ "blue", 0x0000FFFFu }, { "yellow", 0xFFFF00FFu }, { "cyan", 0x00FFFFFFu }, { "magenta", 0xFF00FFFFu },
			{ "orange", 0xFFA500FFu }, { "purple", 0x800080FFu }, { "grey", 0x808080FFu }, { "gray", 0x808080FFu },
		};

		std::string Lower(std::string text) {
			std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return text;
		}

		// "#RGB", "#RRGGBB", "#RRGGBBAA" or a name -> sRGB 0-1.
		bool ParseColour(std::string value, glm::vec4& out) {
			if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
				value = value.substr(1, value.size() - 2);
			}
			value = Lower(value);
			std::uint32_t rgba = 0;
			if (!value.empty() && value[0] == '#') {
				std::string hex = value.substr(1);
				if (hex.size() == 3) { hex = { hex[0], hex[0], hex[1], hex[1], hex[2], hex[2] }; }
				if (hex.size() == 6) { hex += "ff"; }
				if (hex.size() != 8 || hex.find_first_not_of("0123456789abcdef") != std::string::npos) { return false; }
				rgba = static_cast<std::uint32_t>(std::strtoul(hex.c_str(), nullptr, 16));
			} else {
				bool found = false;
				for (const NamedColour& named : kNamedColours) {
					if (value == named.name) { rgba = named.rgba; found = true; break; }
				}
				if (!found) { return false; }
			}
			out = glm::vec4(static_cast<float>((rgba >> 24) & 0xFF), static_cast<float>((rgba >> 16) & 0xFF),
			                static_cast<float>((rgba >> 8) & 0xFF), static_cast<float>(rgba & 0xFF)) / 255.0f;
			return true;
		}

		// Takes the tags out. colours[0] is a placeholder for "the text's own colour".
		void ParseRichText(const std::vector<std::uint32_t>& in, bool rich, bool boldAll, bool underlineAll,
		                   std::vector<StyledCp>& out, std::vector<glm::vec4>& colours) {
			out.clear();
			out.reserve(in.size());
			colours.assign(1, glm::vec4(1.0f));
			int boldDepth = 0, underlineDepth = 0;
			std::vector<std::uint8_t> colourStack;

			auto emit = [&](std::uint32_t cp) {
				out.push_back({ cp, colourStack.empty() ? std::uint8_t{ 0 } : colourStack.back(), boldAll || boldDepth > 0, underlineAll || underlineDepth > 0 });
			};

			for (std::size_t i = 0; i < in.size(); ++i) {
				if (!rich || in[i] != '<') { emit(in[i]); continue; }

				// Read up to the closing '>' (ASCII only, short, on one line), else it's just a '<'.
				std::size_t end = i + 1;
				std::string tag;
				while (end < in.size() && in[end] != '>' && in[end] != '<' && in[end] != '\n' && in[end] < 0x80 && tag.size() < 32) {
					tag += static_cast<char>(in[end]);
					++end;
				}
				if (end >= in.size() || in[end] != '>' || tag.empty()) { emit(in[i]); continue; }

				const bool closing = tag[0] == '/';
				std::string body = closing ? tag.substr(1) : tag;
				std::string value;
				if (const std::size_t eq = body.find('='); eq != std::string::npos) { value = body.substr(eq + 1); body = body.substr(0, eq); }
				body = Lower(body);

				bool handled = true;
				if (body == "b" && value.empty()) {
					if (!closing) { ++boldDepth; } else if (boldDepth > 0) { --boldDepth; }
				} else if (body == "u" && value.empty()) {
					if (!closing) { ++underlineDepth; } else if (underlineDepth > 0) { --underlineDepth; }
				} else if (body == "color" || body == "colour") {
					glm::vec4 colour;
					if (closing && value.empty()) {
						if (!colourStack.empty()) { colourStack.pop_back(); }
					} else if (!closing && ParseColour(value, colour) && colours.size() < 255) {
						colours.push_back(colour);
						colourStack.push_back(static_cast<std::uint8_t>(colours.size() - 1));
					} else {
						handled = false;
					}
				} else {
					handled = false;
				}

				if (handled) { i = end; } else { emit(in[i]); }   // not a tag we know: show the '<' and carry on after it
			}
		}

		// ---- Layout -----------------------------------------------------------------------------

		// One character, resolved against the font at one size.
		struct Char {
			std::uint32_t    codepoint = 0;       // what is drawn ('?' for a missing one)
			const FontGlyph* glyph = nullptr;     // null: nothing to draw or advance (no glyph and no '?')
			float            advance = 0.0f;      // pen advance in caller units, spacing and bold included
			bool             space = false;       // a break opportunity (space or tab)
			std::uint8_t     colour = 0;
			bool             bold = false;
			bool             underline = false;
		};

		struct Line {
			std::size_t begin = 0, end = 0;       // into the chars
			bool        ellipsis = false;         // ends with the ellipsis
		};

		struct Context {
			const FontData&                font;
			const TextLayoutSettings&      settings;
			const std::vector<StyledCp>&   text;
			const std::vector<glm::vec4>&  colours;
		};

		// Measure-only results, for best fit.
		struct Fit {
			bool fits = true;
		};

		/*
		 * Lays the text out at one size. With `out` null it only answers whether all of it fits
		 * (best fit's question); with `out` it produces the quads and underlines.
		 */
		Fit LayoutAt(const Context& ctx, float size, std::size_t group, TextLayoutResult* out) {
			const FontData& font = ctx.font;
			const TextLayoutSettings& settings = ctx.settings;

			const float boxW    = std::max(0.0f, settings.box.x);
			const float boxH    = std::max(0.0f, settings.box.y);
			const float spacing = settings.characterSpacing * size;
			const float bold    = kTextBoldWidthEm * size;

			const FontGlyph* question = font.FindGlyph('?', group);
			const FontGlyph* space    = font.FindGlyph(' ', group);

			// ---- Characters ----
			std::vector<Char> chars;
			chars.reserve(ctx.text.size());
			std::vector<std::size_t> paragraphEnds;
			for (const StyledCp& styled : ctx.text) {
				const std::uint32_t cp = styled.codepoint;
				if (cp == '\n') { paragraphEnds.push_back(chars.size()); continue; }

				Char c;
				c.codepoint = cp;
				c.colour    = styled.colour;
				c.bold      = styled.bold;
				c.underline = styled.underline;
				if (cp == '\t') {
					c.glyph   = space;
					c.space   = true;
					c.advance = space ? space->advance * 4.0f * size + spacing : 0.0f;
					chars.push_back(c);
					continue;
				}
				c.glyph = font.FindGlyph(cp, group);
				if (c.glyph == nullptr) {
					if (out && std::find(out->missing.begin(), out->missing.end(), cp) == out->missing.end()) { out->missing.push_back(cp); }
					c.glyph     = question;
					c.codepoint = '?';
				}
				c.space   = cp == ' ' || cp == 0x3000;   // ASCII and ideographic space
				c.advance = c.glyph ? c.glyph->advance * size + spacing + (c.bold && !c.space ? bold : 0.0f) : 0.0f;
				chars.push_back(c);
			}
			paragraphEnds.push_back(chars.size());

			auto kern = [&](const Char& left, const Char& right) {
				return (left.glyph && right.glyph) ? font.GetKerning(left.codepoint, right.codepoint) * size : 0.0f;
			};

			// Width of chars [begin, end) without trailing spaces (they don't count for alignment or fit).
			auto widthOf = [&](std::size_t begin, std::size_t end) {
				while (end > begin && chars[end - 1].space) { --end; }
				float width = 0.0f;
				for (std::size_t i = begin; i < end; ++i) {
					if (i > begin) { width += kern(chars[i - 1], chars[i]); }
					width += chars[i].advance;
				}
				if (end > begin) { width -= spacing; }   // no spacing after the last character
				return width;
			};

			// ---- Lines ----
			Fit fit;
			std::vector<Line> lines;
			std::size_t paragraphBegin = 0;
			for (const std::size_t paragraphEnd : paragraphEnds) {
				if (!settings.wrap || paragraphEnd == paragraphBegin) {
					lines.push_back({ paragraphBegin, paragraphEnd, false });
					if (widthOf(paragraphBegin, paragraphEnd) > boxW + kFitSlack) { fit.fits = false; }
					paragraphBegin = paragraphEnd;
					continue;
				}

				std::size_t lineBegin = paragraphBegin;
				while (lineBegin < paragraphEnd) {
					float width = 0.0f;
					std::size_t breakAt = 0;
					bool haveBreak = false;
					std::size_t end = paragraphEnd;
					std::size_t next = paragraphEnd;

					for (std::size_t i = lineBegin; i < paragraphEnd; ++i) {
						const Char& c = chars[i];
						const float step = (i > lineBegin ? kern(chars[i - 1], c) : 0.0f) + c.advance;
						if (c.space) {
							breakAt = i;
							haveBreak = true;
							width += step;
							continue;
						}
						if (width + step - spacing > boxW + kFitSlack && i > lineBegin) {
							if (haveBreak) {
								end  = breakAt;
								next = breakAt;
								while (next < paragraphEnd && chars[next].space) { ++next; }   // spaces at a wrap vanish
							} else {
								end  = i;   // one word wider than the box: split it here
								next = i;
								fit.fits = false;
							}
							break;
						}
						width += step;
					}
					if (end == paragraphEnd && widthOf(lineBegin, end) > boxW + kFitSlack) { fit.fits = false; }   // a single character wider than the box
					lines.push_back({ lineBegin, end, false });
					lineBegin = next;
				}
				paragraphBegin = paragraphEnd;
			}

			// ---- Which lines fit ----
			const float ascender  = font.metrics.ascender * size;
			const float descender = font.metrics.descender * size;
			const float lineStep  = font.metrics.lineHeight * size * settings.lineSpacing;
			const float allHeight = static_cast<float>(lines.size() - 1) * lineStep + ascender + descender;
			if (allHeight > boxH + kFitSlack) { fit.fits = false; }

			if (out == nullptr) { return fit; }   // best fit only needed the answer

			out->lineCount = lines.size();
			std::size_t shown = lines.size();
			if (settings.overflow != TextOverflow::Overflow) {
				if (boxH + kFitSlack < ascender + descender) {
					shown = 0;
				} else if (lineStep > 0.0f) {
					const float extra = (boxH + kFitSlack - ascender - descender) / lineStep;
					shown = std::min(lines.size(), static_cast<std::size_t>(std::floor(extra)) + 1);
				}
			}
			out->clipped    = shown < lines.size();
			out->shownLines = shown;
			lines.resize(shown);

			// ---- Ellipsis (styled like the character it follows) ----
			auto ellipsisFor = [&](const Char& style) {
				std::vector<Char> dots;
				Char c = style;
				c.space = false;
				if (const FontGlyph* glyph = font.FindGlyph(0x2026, group)) {
					c.codepoint = 0x2026; c.glyph = glyph; c.advance = glyph->advance * size + spacing + (c.bold ? bold : 0.0f);
					dots.push_back(c);
				} else if (const FontGlyph* dot = font.FindGlyph('.', group)) {
					c.codepoint = '.'; c.glyph = dot; c.advance = dot->advance * size + spacing + (c.bold ? bold : 0.0f);
					dots.assign(3, c);
				}
				return dots;
			};
			auto widthOfDots = [&](const std::vector<Char>& dots) {
				float width = 0.0f;
				for (std::size_t i = 0; i < dots.size(); ++i) { width += (i > 0 ? kern(dots[i - 1], dots[i]) : 0.0f) + dots[i].advance; }
				return dots.empty() ? 0.0f : width - spacing;
			};

			if (settings.overflow == TextOverflow::Ellipsis) {
				auto ellipsize = [&](Line& line) {
					const Char style = line.end > line.begin ? chars[line.end - 1] : (chars.empty() ? Char{} : chars.front());
					const float dotsWidth = widthOfDots(ellipsisFor(style));
					std::size_t end = line.end;
					while (end > line.begin && widthOf(line.begin, end) + dotsWidth > boxW + kFitSlack) { --end; }
					while (end > line.begin && chars[end - 1].space) { --end; }
					line.end = end;
					line.ellipsis = true;
					out->clipped = true;
				};
				for (std::size_t i = 0; i < lines.size(); ++i) {
					const bool lastOfCut = shown < out->lineCount && i + 1 == lines.size();
					if (lastOfCut || (!settings.wrap && widthOf(lines[i].begin, lines[i].end) > boxW + kFitSlack)) {
						ellipsize(lines[i]);
					}
				}
			}

			if (lines.empty()) { return fit; }

			// ---- Place ----
			const float blockHeight = static_cast<float>(lines.size() - 1) * lineStep + ascender + descender;
			float top = 0.0f;
			switch (settings.alignV) {
				case TextAlignV::Top:    top = 0.0f; break;
				case TextAlignV::Middle: top = (boxH - blockHeight) * 0.5f; break;
				case TextAlignV::Bottom: top = boxH - blockHeight; break;
			}

			const float underlineY         = font.metrics.underlineY * size;
			const float underlineThickness = std::max(font.metrics.underlineThickness * size, 0.0f);

			float minX = 1.0e30f, maxX = -1.0e30f;
			std::vector<Char> lineChars;
			for (std::size_t l = 0; l < lines.size(); ++l) {
				const Line& line = lines[l];

				lineChars.assign(chars.begin() + static_cast<std::ptrdiff_t>(line.begin), chars.begin() + static_cast<std::ptrdiff_t>(line.end));
				if (line.ellipsis) {
					while (!lineChars.empty() && lineChars.back().space) { lineChars.pop_back(); }
					const Char style = !lineChars.empty() ? lineChars.back()
						: (line.end > line.begin ? chars[line.end - 1] : (chars.empty() ? Char{} : chars.front()));
					const std::vector<Char> dots = ellipsisFor(style);
					lineChars.insert(lineChars.end(), dots.begin(), dots.end());
				}

				// Width for alignment: by advances, trailing spaces excluded.
				std::size_t visibleEnd = lineChars.size();
				while (visibleEnd > 0 && lineChars[visibleEnd - 1].space) { --visibleEnd; }
				float width = 0.0f;
				for (std::size_t i = 0; i < visibleEnd; ++i) {
					if (i > 0) { width += kern(lineChars[i - 1], lineChars[i]); }
					width += lineChars[i].advance;
				}
				if (visibleEnd > 0) { width -= spacing; }

				float x = 0.0f;
				switch (settings.alignH) {
					case TextAlignH::Left:   x = 0.0f; break;
					case TextAlignH::Center: x = (boxW - width) * 0.5f; break;
					case TextAlignH::Right:  x = boxW - width; break;
				}
				minX = std::min(minX, x);
				maxX = std::max(maxX, x + width);

				const float baseline = top + ascender + static_cast<float>(l) * lineStep;
				float pen = x;

				// Underline runs: consecutive underlined characters of one colour, trailing spaces left out.
				bool  inRun = false;
				float runStart = 0.0f, runEnd = 0.0f;
				std::uint8_t runColour = 0;
				auto closeRun = [&]() {
					if (inRun && runEnd > runStart) {
						TextDecoration bar;
						bar.rect = glm::vec4(runStart, baseline + underlineY - underlineThickness * 0.5f,
						                     runEnd, baseline + underlineY + underlineThickness * 0.5f);
						bar.hasColour = runColour != 0;
						bar.colour    = ctx.colours[runColour];
						out->underlines.push_back(bar);
					}
					inRun = false;
				};

				for (std::size_t i = 0; i < lineChars.size(); ++i) {
					const Char& c = lineChars[i];
					if (i > 0) { pen += kern(lineChars[i - 1], c); }
					if (c.glyph && c.glyph->HasInk()) {
						const glm::vec4& p = c.glyph->plane;
						TextGlyphQuad quad;
						quad.rect      = glm::vec4(pen + p.x * size, baseline + p.y * size, pen + p.z * size, baseline + p.w * size);
						quad.uv        = c.glyph->uv;
						quad.pen       = glm::vec2(pen, baseline);
						quad.hasColour = c.colour != 0;
						quad.colour    = ctx.colours[c.colour];
						quad.boldWidth = c.bold ? bold : 0.0f;
						quad.codepoint = c.codepoint;
						out->quads.push_back(quad);
					}

					const bool underlined = c.underline && i < visibleEnd;
					if (underlined && inRun && c.colour != runColour) { closeRun(); }
					if (underlined) {
						if (!inRun) { inRun = true; runStart = pen; runColour = c.colour; }
						runEnd = pen + c.advance - spacing;
					} else if (inRun) {
						closeRun();
					}
					pen += c.advance;
				}
				closeRun();
			}
			out->bounds = glm::vec4(minX, top, maxX, top + blockHeight);
			return fit;
		}

		std::size_t GroupFor(const FontData& font, const TextLayoutSettings& settings, float size) {
			if (font.outputType != FontOutputType::Bitmap) { return 0; }
			if (settings.pixelScale > 0.0f) { return font.ClosestGroup(size * settings.pixelScale); }
			return std::min(settings.sizeGroup, font.groups.size() - 1);
		}

	}   // anonymous namespace

	void DecodeUtf8(std::string_view utf8, std::vector<std::uint32_t>& out) {
		out.clear();
		out.reserve(utf8.size());
		const auto* s = reinterpret_cast<const unsigned char*>(utf8.data());
		const std::size_t n = utf8.size();
		std::size_t i = 0;
		while (i < n) {
			const unsigned char c = s[i];
			std::uint32_t cp = 0;
			std::size_t len = 0;
			if      (c < 0x80)           { cp = c;        len = 1; }
			else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
			else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
			else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
			else { ++i; continue; }
			if (i + len > n) { break; }
			bool ok = true;
			for (std::size_t k = 1; k < len; ++k) {
				if ((s[i + k] & 0xC0) != 0x80) { ok = false; break; }
				cp = (cp << 6) | (s[i + k] & 0x3Fu);
			}
			static constexpr std::uint32_t kMinForLength[5] = { 0, 0, 0x80, 0x800, 0x10000 };
			if (!ok || cp < kMinForLength[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { ++i; continue; }
			if (cp != '\r' && cp != 0xFEFF) { out.push_back(cp); }   // no carriage returns, no byte-order mark
			i += len;
		}
	}

	std::string StripRichTextTags(std::string_view utf8) {
		std::vector<std::uint32_t> codepoints;
		DecodeUtf8(utf8, codepoints);
		std::vector<StyledCp> styled;
		std::vector<glm::vec4> colours;
		ParseRichText(codepoints, true, false, false, styled, colours);

		std::string text;
		for (const StyledCp& c : styled) {
			const std::uint32_t cp = c.codepoint;
			if (cp < 0x80) { text += static_cast<char>(cp); }
			else if (cp < 0x800) { text += static_cast<char>(0xC0 | (cp >> 6)); text += static_cast<char>(0x80 | (cp & 0x3F)); }
			else if (cp < 0x10000) { text += static_cast<char>(0xE0 | (cp >> 12)); text += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); text += static_cast<char>(0x80 | (cp & 0x3F)); }
			else { text += static_cast<char>(0xF0 | (cp >> 18)); text += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); text += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); text += static_cast<char>(0x80 | (cp & 0x3F)); }
		}
		return text;
	}

	void LayoutText(const FontData& font, std::string_view utf8, const TextLayoutSettings& settings, TextLayoutResult& out) {

		out.quads.clear();
		out.underlines.clear();
		out.missing.clear();
		out.bounds     = glm::vec4(0.0f);
		out.fontSize   = settings.fontSize;
		out.sizeGroup  = 0;
		out.lineCount  = 0;
		out.shownLines = 0;
		out.clipped    = false;

		if (utf8.empty() || font.groups.empty()) { return; }

		std::vector<std::uint32_t> codepoints;
		DecodeUtf8(utf8, codepoints);
		std::vector<StyledCp> text;
		std::vector<glm::vec4> colours;
		ParseRichText(codepoints, settings.richText, settings.bold, settings.underline, text, colours);
		if (text.empty()) { return; }

		const Context ctx{ font, settings, text, colours };

		// ---- The size ----
		float size = settings.fontSize;
		if (settings.bestFit) {
			const float lo0 = std::max(1.0f, std::min(settings.minFontSize, settings.maxFontSize));
			const float hi0 = std::max(lo0, std::max(settings.minFontSize, settings.maxFontSize));
			if (LayoutAt(ctx, hi0, GroupFor(font, settings, hi0), nullptr).fits) {
				size = hi0;
			} else {
				// Largest size that fits, to a quarter of a unit: fitting only gets harder as the
				// size grows, so a binary search finds it in ~10 layouts.
				float lo = lo0, hi = hi0;
				bool any = LayoutAt(ctx, lo, GroupFor(font, settings, lo), nullptr).fits;
				if (any) {
					while (hi - lo > 0.25f) {
						const float mid = (lo + hi) * 0.5f;
						if (LayoutAt(ctx, mid, GroupFor(font, settings, mid), nullptr).fits) { lo = mid; } else { hi = mid; }
					}
				}
				size = std::floor(lo * 4.0f) / 4.0f;
				size = std::max(size, lo0);
			}
		}
		if (!(size > 0.0f)) { return; }

		out.fontSize  = size;
		out.sizeGroup = GroupFor(font, settings, size);
		LayoutAt(ctx, size, out.sizeGroup, &out);
	}

}   // NAMESPACE CRUCIBLE
