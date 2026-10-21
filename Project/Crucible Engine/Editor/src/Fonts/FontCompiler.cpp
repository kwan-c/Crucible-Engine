/*!
* @file			FontCompiler.cpp
* @author		Tan Chun Kwang Xavier
* @par			chunkwangxavier.tan@digipen.edu
* @date			21 October 2026
*
* @brief		FontCompiler: Bitmap (FreeType) and SDF/MTSDF (msdf-atlas-gen) baking.
*/

#include "Fonts/FontCompiler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <set>
#include <thread>
#include <unordered_map>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <msdf-atlas-gen/msdf-atlas-gen.h>

namespace Crucible {

	namespace {

		constexpr std::size_t   kMaxCodepoints  = 65536;   // a whole CJK block is ~21k; past this it's a mistake
		constexpr std::size_t   kMaxKerningSet  = 2048;    // kerning is checked for every pair: n^2 lookups
		constexpr std::uint32_t kAsciiFirst     = 0x20;
		constexpr std::uint32_t kAsciiLast      = 0x7E;

		bool IsDrawable(std::uint32_t cp) {
			if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) { return false; }   // C0 / DEL / C1 controls
			if (cp >= 0xD800 && cp <= 0xDFFF)            { return false; }   // surrogates
			if (cp == 0xFEFF || cp > 0x10FFFF)           { return false; }   // BOM, out of range
			return true;
		}

		// UTF-8 -> codepoints. Malformed bytes are skipped and counted.
		void DecodeUtf8(std::string const& text, std::set<std::uint32_t>& out, std::size_t& invalid) {
			const auto* s = reinterpret_cast<unsigned char const*>(text.data());
			const std::size_t n = text.size();
			std::size_t i = 0;
			while (i < n) {
				const unsigned char c = s[i];
				std::uint32_t cp = 0;
				std::size_t len = 0;
				if      (c < 0x80)           { cp = c;        len = 1; }
				else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
				else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
				else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
				else { ++invalid; ++i; continue; }

				if (i + len > n) { ++invalid; break; }
				bool ok = true;
				for (std::size_t k = 1; k < len; ++k) {
					if ((s[i + k] & 0xC0) != 0x80) { ok = false; break; }
					cp = (cp << 6) | (s[i + k] & 0x3F);
				}
				// Reject overlong forms (e.g. 0xC0 0x80 for NUL).
				static constexpr std::uint32_t kMinForLength[5] = { 0, 0, 0x80, 0x800, 0x10000 };
				if (!ok || cp < kMinForLength[len]) { ++invalid; ++i; continue; }

				if (IsDrawable(cp)) { out.insert(cp); }
				i += len;
			}
		}

		// Frees FreeType objects on every return path.
		struct FreeTypeFace {
			FT_Library library = nullptr;
			FT_Face    face    = nullptr;
			~FreeTypeFace() {
				if (face)    { FT_Done_Face(face); }
				if (library) { FT_Done_FreeType(library); }
			}
		};

		struct MsdfFont {
			msdfgen::FreetypeHandle* freetype = nullptr;
			msdfgen::FontHandle*     font     = nullptr;
			~MsdfFont() {
				if (font)     { msdfgen::destroyFont(font); }
				if (freetype) { msdfgen::deinitializeFreetype(freetype); }
			}
		};

		// Smallest power of two >= v (v >= 1).
		int NextPow2(int v) { int p = 1; while (p < v) { p <<= 1; } return p; }

		// ---- SDF / MTSDF ------------------------------------------------------------------------

		template <int N, void (*Generator)(const msdfgen::BitmapSection<float, N>&, const msdf_atlas::GlyphGeometry&, const msdf_atlas::GeneratorAttributes&)>
		void GenerateAtlas(std::vector<msdf_atlas::GlyphGeometry>& glyphs, int width, int height, FontAtlas& atlas) {
			msdf_atlas::GeneratorAttributes attributes;
			// Built without Skia, so overlapping contours (common in variable fonts) aren't merged
			// beforehand: handle them in the generator and fix the signs with a scanline pass,
			// exactly what msdf-atlas-gen's own tool does in this configuration.
			attributes.config.overlapSupport = true;
			attributes.scanlinePass = true;

			msdf_atlas::ImmediateAtlasGenerator<float, N, Generator, msdf_atlas::BitmapAtlasStorage<msdf_atlas::byte, N>> generator(width, height);
			generator.setAttributes(attributes);
			generator.setThreadCount(static_cast<int>(std::max(1u, std::thread::hardware_concurrency())));
			generator.generate(glyphs.data(), static_cast<int>(glyphs.size()));

			const msdfgen::BitmapConstRef<msdf_atlas::byte, N> bitmap = generator.atlasStorage();
			atlas.width    = static_cast<std::uint32_t>(width);
			atlas.height   = static_cast<std::uint32_t>(height);
			atlas.channels = static_cast<std::uint32_t>(N);
			atlas.pixels.resize(std::size_t{ atlas.width } * atlas.height * N);
			// msdfgen bitmaps are bottom row first; FontAtlas is top row first.
			for (int y = 0; y < height; ++y) {
				const msdf_atlas::byte* src = bitmap(0, height - 1 - y);
				std::copy(src, src + std::size_t{ static_cast<std::size_t>(width) } * N,
				          atlas.pixels.begin() + static_cast<std::ptrdiff_t>(std::size_t{ static_cast<std::size_t>(y) } * width * N));
			}
		}

		bool CompileDistanceField(std::span<const std::uint8_t> bytes, FontImportSettings const& settings,
		                          std::vector<std::uint32_t> const& codepoints, FontCompileResult& out, std::string& error) {
			MsdfFont handle;
			handle.freetype = msdfgen::initializeFreetype();
			if (!handle.freetype) { error = "FreeType failed to start"; return false; }
			handle.font = msdfgen::loadFontData(handle.freetype, bytes.data(), static_cast<int>(bytes.size()));
			if (!handle.font) { error = "not a font file FreeType can read"; return false; }

			msdf_atlas::Charset charset;
			for (std::uint32_t cp : codepoints) { charset.add(cp); }

			const bool kerning = codepoints.size() <= kMaxKerningSet;
			if (!kerning) {
				out.warnings.push_back("kerning skipped: more than " + std::to_string(kMaxKerningSet) + " characters");
			}

			std::vector<msdf_atlas::GlyphGeometry> glyphs;
			msdf_atlas::FontGeometry geometry(&glyphs);
			// Scale 1.0 = glyph geometry in ems. No Skia, so no geometry preprocessing (see GenerateAtlas).
			if (geometry.loadCharset(handle.font, 1.0, charset, false, kerning) < 0) {
				error = "could not read the font's metrics";
				return false;
			}

			// What the font didn't have.
			std::set<std::uint32_t> loaded;
			for (auto const& glyph : glyphs) { loaded.insert(glyph.getCodepoint()); }
			for (std::uint32_t cp : codepoints) { if (!loaded.count(cp)) { out.missing.push_back(cp); } }
			if (glyphs.empty()) { error = "the font has none of the requested characters"; return false; }

			const bool multiChannel = settings.outputType == FontOutputType::Mtsdf;
			if (multiChannel) {
				// Ink-trap colouring keeps corners sharp; 3 rad is msdf-atlas-gen's default angle.
				for (auto& glyph : glyphs) { glyph.edgeColoring(&msdfgen::edgeColoringInkTrap, 3.0, 0); }
			}

			msdf_atlas::TightAtlasPacker packer;
			packer.setDimensionsConstraint(msdf_atlas::DimensionsConstraint::POWER_OF_TWO_SQUARE);
			packer.setScale(settings.glyphSize);
			packer.setPixelRange(msdfgen::Range(settings.pixelRange));
			packer.setMiterLimit(1.0);
			packer.setSpacing(settings.padding);
			if (packer.pack(glyphs.data(), static_cast<int>(glyphs.size())) != 0) {
				error = "the glyphs could not be packed into an atlas";
				return false;
			}
			int width = 0, height = 0;
			packer.getDimensions(width, height);
			if (width <= 0 || height <= 0) { error = "the glyphs could not be packed into an atlas"; return false; }
			if (width > settings.atlasMaxSize || height > settings.atlasMaxSize) {
				error = "the atlas would be " + std::to_string(width) + "x" + std::to_string(height) + ", over the "
				      + std::to_string(settings.atlasMaxSize) + " limit - lower the glyph size or the number of characters";
				return false;
			}

			FontData& font = out.font;
			font.outputType = settings.outputType;
			if (multiChannel) { GenerateAtlas<4, msdf_atlas::mtsdfGenerator>(glyphs, width, height, font.atlas); }
			else              { GenerateAtlas<1, msdf_atlas::sdfGenerator>(glyphs, width, height, font.atlas); }
			font.atlas.pixelRange = settings.pixelRange;

			msdfgen::FontMetrics const& m = geometry.getMetrics();
			font.metrics.lineHeight         = static_cast<float>(m.lineHeight);
			font.metrics.ascender           = static_cast<float>(m.ascenderY);
			font.metrics.descender          = static_cast<float>(-m.descenderY);    // msdfgen: negative below the baseline
			font.metrics.underlineY         = static_cast<float>(-m.underlineY);    // y up -> y down
			font.metrics.underlineThickness = static_cast<float>(m.underlineThickness);

			const double w = width, h = height;
			std::unordered_map<int, std::uint32_t> codepointOf;   // font glyph index -> codepoint (for kerning)
			font.glyphs.reserve(glyphs.size());
			for (auto const& glyph : glyphs) {
				FontGlyph g;
				g.codepoint = glyph.getCodepoint();
				g.advance   = static_cast<float>(glyph.getAdvance());
				if (!glyph.isWhitespace()) {
					double l = 0, b = 0, r = 0, t = 0;
					glyph.getQuadPlaneBounds(l, b, r, t);   // ems, y up
					g.plane = { static_cast<float>(l), static_cast<float>(-t), static_cast<float>(r), static_cast<float>(-b) };
					glyph.getQuadAtlasBounds(l, b, r, t);   // atlas pixels, y up from the bottom row
					g.uv = { static_cast<float>(l / w), static_cast<float>((h - t) / h),
					         static_cast<float>(r / w), static_cast<float>((h - b) / h) };
				}
				font.glyphs.push_back(g);
				codepointOf[glyph.getIndex()] = g.codepoint;
			}
			std::sort(font.glyphs.begin(), font.glyphs.end(), [](FontGlyph const& a, FontGlyph const& b) { return a.codepoint < b.codepoint; });
			font.groups.push_back({ static_cast<float>(settings.glyphSize), 0, static_cast<std::uint32_t>(font.glyphs.size()) });

			for (auto const& [pair, adjust] : geometry.getKerning()) {
				auto const left = codepointOf.find(pair.first), right = codepointOf.find(pair.second);
				if (left != codepointOf.end() && right != codepointOf.end() && adjust != 0.0) {
					font.kerning.push_back({ left->second, right->second, static_cast<float>(adjust) });
				}
			}
			return true;
		}

		// ---- Bitmap -----------------------------------------------------------------------------

		struct RenderedGlyph {
			std::size_t               group = 0;
			FontGlyph                 glyph;
			int                       left = 0, top = 0;   // FreeType bearing, pixels (top = up from the baseline)
			int                       width = 0, height = 0;
			std::vector<std::uint8_t> coverage;            // width * height, top row first
		};

		bool CompileBitmap(std::span<const std::uint8_t> bytes, FontImportSettings const& settings,
		                   std::vector<std::uint32_t> const& codepoints, FontCompileResult& out, std::string& error) {
			FreeTypeFace ft;
			if (FT_Init_FreeType(&ft.library) != 0) { error = "FreeType failed to start"; return false; }
			if (FT_New_Memory_Face(ft.library, bytes.data(), static_cast<FT_Long>(bytes.size()), 0, &ft.face) != 0) {
				error = "not a font file FreeType can read";
				return false;
			}
			FT_Face face = ft.face;
			if (!FT_IS_SCALABLE(face) || face->units_per_EM == 0) {
				error = "only outline fonts (TrueType/OpenType) are supported, not bitmap-only fonts";
				return false;
			}
			const double em = face->units_per_EM;

			// Which requested characters the font has.
			std::vector<std::pair<std::uint32_t, FT_UInt>> present;
			for (std::uint32_t cp : codepoints) {
				const FT_UInt index = FT_Get_Char_Index(face, cp);
				if (index == 0) { out.missing.push_back(cp); } else { present.emplace_back(cp, index); }
			}
			if (present.empty()) { error = "the font has none of the requested characters"; return false; }

			// Render every glyph at every size.
			std::vector<RenderedGlyph> rendered;
			rendered.reserve(present.size() * settings.bitmapSizes.size());
			std::size_t failed = 0;
			for (std::size_t group = 0; group < settings.bitmapSizes.size(); ++group) {
				const int size = settings.bitmapSizes[group];
				if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(size)) != 0) {
					error = "the font can't be rendered at " + std::to_string(size) + " px";
					return false;
				}
				for (auto const& [cp, index] : present) {
					if (FT_Load_Glyph(face, index, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) != 0) { ++failed; continue; }
					FT_GlyphSlot slot = face->glyph;
					FT_Bitmap const& bitmap = slot->bitmap;

					RenderedGlyph r;
					r.group = group;
					r.glyph.codepoint = cp;
					r.glyph.advance   = static_cast<float>(slot->advance.x / 64.0 / size);   // hinted: whole pixels at this size
					r.left   = slot->bitmap_left;
					r.top    = slot->bitmap_top;
					r.width  = static_cast<int>(bitmap.width);
					r.height = static_cast<int>(bitmap.rows);
					if (r.width > 0 && r.height > 0) {
						r.coverage.resize(std::size_t{ static_cast<std::size_t>(r.width) } * r.height);
						for (int y = 0; y < r.height; ++y) {
							const unsigned char* row = bitmap.buffer + static_cast<std::ptrdiff_t>(y) * bitmap.pitch;
							for (int x = 0; x < r.width; ++x) {
								const std::uint8_t value = bitmap.pixel_mode == FT_PIXEL_MODE_MONO
									? static_cast<std::uint8_t>(((row[x >> 3] >> (7 - (x & 7))) & 1) ? 255 : 0)
									: row[x];
								r.coverage[static_cast<std::size_t>(y) * r.width + x] = value;
							}
						}
					}
					rendered.push_back(std::move(r));
				}
			}
			if (failed > 0) { out.warnings.push_back(std::to_string(failed) + " glyph render(s) failed and were skipped"); }

			// Pack the inked glyphs. Each box is the glyph plus `padding` on its left and top, so
			// neighbours are always `padding` apart. Try power-of-two sizes from the smallest that
			// could hold the total area, growing until it fits.
			const int pad = settings.padding;
			std::vector<std::size_t> inked;
			std::size_t area = 0;
			for (std::size_t i = 0; i < rendered.size(); ++i) {
				if (rendered[i].width > 0 && rendered[i].height > 0) {
					inked.push_back(i);
					area += std::size_t{ static_cast<std::size_t>(rendered[i].width + pad) } * (rendered[i].height + pad);
				}
			}

			int width = 0, height = 0;
			std::vector<msdf_atlas::Rectangle> boxes(inked.size());
			int side = std::max(64, NextPow2(static_cast<int>(std::ceil(std::sqrt(static_cast<double>(area))))));
			bool packed = inked.empty();
			for (int attempt = 0; !packed && attempt < 16; ++attempt) {
				// Alternate: w x w/2, then w x w, then 2w x w ...
				const int w = side, h = (attempt % 2 == 0) ? side / 2 : side;
				if (w > settings.atlasMaxSize || h > settings.atlasMaxSize) { break; }
				if (std::size_t{ static_cast<std::size_t>(w) } * h >= area) {
					for (std::size_t k = 0; k < inked.size(); ++k) {
						boxes[k] = { 0, 0, rendered[inked[k]].width + pad, rendered[inked[k]].height + pad };
					}
					msdf_atlas::RectanglePacker packer(w - pad, h - pad);   // keep `pad` free on the right/bottom edge too
					if (packer.pack(boxes.data(), static_cast<int>(boxes.size())) == 0) { width = w; height = h; packed = true; break; }
				}
				if (attempt % 2 == 1) { side *= 2; }
			}
			if (!packed) {
				error = "the glyphs don't fit in a " + std::to_string(settings.atlasMaxSize) + "x" + std::to_string(settings.atlasMaxSize)
				      + " atlas - use fewer sizes or characters";
				return false;
			}
			if (inked.empty()) { width = 4; height = 4; }   // only whitespace: a tiny empty atlas keeps FontData valid

			FontData& font = out.font;
			font.outputType       = FontOutputType::Bitmap;
			font.atlas.width      = static_cast<std::uint32_t>(width);
			font.atlas.height     = static_cast<std::uint32_t>(height);
			font.atlas.channels   = 1;
			font.atlas.pixelRange = 0.0f;
			font.atlas.pixels.assign(std::size_t{ font.atlas.width } * font.atlas.height, 0);

			for (std::size_t k = 0; k < inked.size(); ++k) {
				RenderedGlyph& r = rendered[inked[k]];
				const int x0 = boxes[k].x + pad, y0 = boxes[k].y + pad;
				for (int y = 0; y < r.height; ++y) {
					std::copy_n(r.coverage.begin() + static_cast<std::ptrdiff_t>(y) * r.width, r.width,
					            font.atlas.pixels.begin() + static_cast<std::ptrdiff_t>(y0 + y) * width + x0);
				}
				const float size = static_cast<float>(settings.bitmapSizes[r.group]);
				const float left = static_cast<float>(r.left) / size, top = -static_cast<float>(r.top) / size;
				r.glyph.plane = { left, top, left + r.width / size, top + r.height / size };
				r.glyph.uv    = { static_cast<float>(x0) / width, static_cast<float>(y0) / height,
				                  static_cast<float>(x0 + r.width) / width, static_cast<float>(y0 + r.height) / height };
			}

			// Glyphs grouped by size (already in that order), codepoint order inside each group.
			for (std::size_t group = 0; group < settings.bitmapSizes.size(); ++group) {
				FontSizeGroup g{ static_cast<float>(settings.bitmapSizes[group]), static_cast<std::uint32_t>(font.glyphs.size()), 0 };
				for (RenderedGlyph const& r : rendered) {
					if (r.group == group) { font.glyphs.push_back(r.glyph); ++g.glyphCount; }
				}
				font.groups.push_back(g);
			}

			font.metrics.ascender           = static_cast<float>(face->ascender / em);
			font.metrics.descender          = static_cast<float>(-face->descender / em);   // FreeType: negative below
			font.metrics.lineHeight         = static_cast<float>(face->height / em);
			if (!(font.metrics.lineHeight > 0.0f)) { font.metrics.lineHeight = font.metrics.ascender + font.metrics.descender; }
			font.metrics.underlineY         = static_cast<float>(-face->underline_position / em);
			font.metrics.underlineThickness = static_cast<float>(face->underline_thickness / em);

			// Kerning (the 'kern' table), in ems, shared by every size.
			if (FT_HAS_KERNING(face)) {
				if (present.size() <= kMaxKerningSet) {
					for (auto const& [leftCp, leftIndex] : present) {
						for (auto const& [rightCp, rightIndex] : present) {
							FT_Vector delta{};
							if (FT_Get_Kerning(face, leftIndex, rightIndex, FT_KERNING_UNSCALED, &delta) == 0 && delta.x != 0) {
								font.kerning.push_back({ leftCp, rightCp, static_cast<float>(delta.x / em) });
							}
						}
					}
				} else {
					out.warnings.push_back("kerning skipped: more than " + std::to_string(kMaxKerningSet) + " characters");
				}
			}
			return true;
		}

	}	// anonymous namespace

	// ----------------------------------------------------------------------------------------- //

	std::vector<std::uint32_t> CollectCodepoints(FontImportSettings const& settings, std::span<const std::string> sampleTexts,
	                                             std::vector<std::string>* warnings) {
		std::set<std::uint32_t> set;
		if (settings.includeAscii) {
			for (std::uint32_t cp = kAsciiFirst; cp <= kAsciiLast; ++cp) { set.insert(cp); }
		}
		for (auto const& [first, last] : settings.ranges) {
			for (std::uint32_t cp = first; cp <= last && set.size() <= kMaxCodepoints; ++cp) {
				if (IsDrawable(cp)) { set.insert(cp); }
			}
		}
		std::size_t invalid = 0;
		for (std::string const& text : sampleTexts) { DecodeUtf8(text, set, invalid); }
		if (invalid > 0 && warnings) {
			warnings->push_back(std::to_string(invalid) + " invalid UTF-8 byte(s) in the sample text were skipped");
		}
		return { set.begin(), set.end() };
	}

	std::string DescribeCodepoints(std::span<const std::uint32_t> codepoints, std::size_t maxShown) {
		std::string text;
		const std::size_t shown = std::min(maxShown, codepoints.size());
		for (std::size_t i = 0; i < shown; ++i) {
			const std::uint32_t cp = codepoints[i];
			char hex[16] = {};
			std::snprintf(hex, sizeof(hex), "U+%04X", static_cast<unsigned>(cp));
			if (!text.empty()) { text += ", "; }
			text += hex;
			// The character itself, as UTF-8.
			std::string utf8;
			if (cp < 0x80) { utf8 += static_cast<char>(cp); }
			else if (cp < 0x800) { utf8 += static_cast<char>(0xC0 | (cp >> 6)); utf8 += static_cast<char>(0x80 | (cp & 0x3F)); }
			else if (cp < 0x10000) { utf8 += static_cast<char>(0xE0 | (cp >> 12)); utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); utf8 += static_cast<char>(0x80 | (cp & 0x3F)); }
			else { utf8 += static_cast<char>(0xF0 | (cp >> 18)); utf8 += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); utf8 += static_cast<char>(0x80 | (cp & 0x3F)); }
			text += " '" + utf8 + "'";
		}
		if (codepoints.size() > shown) { text += ", ... (+" + std::to_string(codepoints.size() - shown) + " more)"; }
		return text;
	}

	bool CompileFont(std::span<const std::uint8_t> fontBytes, FontImportSettings settings,
	                 std::span<const std::string> sampleTexts, FontCompileResult& out, std::string& error) {
		const auto start = std::chrono::steady_clock::now();
		out = FontCompileResult{};
		error.clear();

		for (std::string& change : settings.Sanitize()) { out.warnings.push_back(std::move(change)); }
		if (fontBytes.empty()) { error = "the font file is empty"; return false; }

		const std::vector<std::uint32_t> codepoints = CollectCodepoints(settings, sampleTexts, &out.warnings);
		if (codepoints.empty()) { error = "no characters to compile - turn on ASCII, add a range or a sample text file"; return false; }
		if (codepoints.size() > kMaxCodepoints) {
			error = std::to_string(codepoints.size()) + " characters requested; the limit is " + std::to_string(kMaxCodepoints);
			return false;
		}

		const bool ok = settings.outputType == FontOutputType::Bitmap
			? CompileBitmap(fontBytes, settings, codepoints, out, error)
			: CompileDistanceField(fontBytes, settings, codepoints, out, error);
		if (!ok) { out.font = FontData{}; return false; }

		out.font.settingsHash = ComputeFontSettingsHash(settings, sampleTexts);
		out.font.BuildLookup();
		std::string invalid;
		if (!out.font.Validate(invalid)) {   // a compiler bug, not a user mistake
			error = "internal error, the compiled font is invalid: " + invalid;
			out.font = FontData{};
			return false;
		}
		if (out.font.kerning.empty() && out.font.glyphs.size() > 1) {
			out.warnings.push_back("no kerning: the font has no 'kern' table (GPOS kerning isn't read)");
		}

		out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		return true;
	}

}	// NAMESPACE CRUCIBLE
