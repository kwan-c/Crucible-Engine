/*************************************************************************
  \file       FontData.cpp
  \brief
    FontData queries and the .cfont reader / writer. See FontData.h.
  \details
    .cfont, version 1, little-endian, no padding (written field by field):

      Header (92 bytes)
        char[4]  magic "CFNT"
        u32      version (1)
        u8       outputType      (FontOutputType)
        u8       atlasChannels   (1 or 4)
        u8       atlasEncoding   (FontAtlasEncoding)
        u8       reserved (0)
        u32      atlasWidth, atlasHeight
        f32      atlasPixelRange
        f32      lineHeight, ascender, descender, underlineY, underlineThickness
        u64      sourceHash, settingsHash
        u32      groupCount, glyphCount, kernCount, atlasByteCount
        u32      reserved[4] (0)
      groupCount   x { f32 pixelSize; u32 firstGlyph; u32 glyphCount; }              12 bytes
      glyphCount   x { u32 codepoint; f32 advance; f32 plane[4]; f32 uv[4]; }        40 bytes
      kernCount    x { u32 left; u32 right; f32 adjust; }                            12 bytes
      atlasByteCount bytes: raw texels, or a PNG with atlasChannels channels

    A new field goes into the reserved words or bumps the version; ReadFont
    rejects versions it doesn't know rather than guessing.
*************************************************************************/

#include "Text/FontData.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <memory>

#include <stb_image.h>   // stbi_load_from_memory; implemented in Graphics/RenderSystem.cpp

// The PNG writer is only needed here, so its implementation is private to this file.
#ifdef _MSC_VER
#pragma warning(push, 0)
#pragma warning(disable : 4996)
#endif
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

static_assert(std::endian::native == std::endian::little, ".cfont is read and written as little-endian memory");

namespace Crucible {

    namespace {

        constexpr char          kMagic[4]       = { 'C', 'F', 'N', 'T' };
        constexpr std::uint32_t kVersion        = 1;
        constexpr std::size_t   kHeaderSize     = 92;
        constexpr std::size_t   kGroupSize      = 12;
        constexpr std::size_t   kGlyphSize      = 40;
        constexpr std::size_t   kKernSize       = 12;

        // Generous limits: anything past these is a corrupt file, not a real font.
        constexpr std::uint32_t kMaxGroups      = 64;
        constexpr std::uint32_t kMaxGlyphs      = 1u << 20;
        constexpr std::uint32_t kMaxKerns       = 1u << 22;
        constexpr std::uint32_t kMaxAtlasSide   = 16384;

        // ---- Writing ----------------------------------------------------------------------------

        class Writer {
        public:
            explicit Writer(std::vector<std::uint8_t>& out) : m_out(out) {}

            void Bytes(const void* data, std::size_t size) {
                const auto* p = static_cast<const std::uint8_t*>(data);
                m_out.insert(m_out.end(), p, p + size);
            }
            void U8(std::uint8_t v)   { Bytes(&v, 1); }
            void U32(std::uint32_t v) { Bytes(&v, 4); }
            void U64(std::uint64_t v) { Bytes(&v, 8); }
            void F32(float v)         { Bytes(&v, 4); }

        private:
            std::vector<std::uint8_t>& m_out;
        };

        // ---- Reading ----------------------------------------------------------------------------

        // Reads sequentially from a span; any read past the end sets `failed` and returns zeros,
        // so a truncated file is reported once at the end instead of after every field.
        class Reader {
        public:
            explicit Reader(std::span<const std::uint8_t> bytes) : m_bytes(bytes) {}

            bool Bytes(void* dst, std::size_t size) {
                if (m_failed || size > m_bytes.size() - m_pos) {
                    m_failed = true;
                    std::memset(dst, 0, size);
                    return false;
                }
                std::memcpy(dst, m_bytes.data() + m_pos, size);
                m_pos += size;
                return true;
            }
            std::uint8_t  U8()  { std::uint8_t v{};  Bytes(&v, 1); return v; }
            std::uint32_t U32() { std::uint32_t v{}; Bytes(&v, 4); return v; }
            std::uint64_t U64() { std::uint64_t v{}; Bytes(&v, 8); return v; }
            float         F32() { float v{};         Bytes(&v, 4); return v; }

            std::size_t Position()  const { return m_pos; }
            std::size_t Remaining() const { return m_bytes.size() - m_pos; }
            bool        Failed()    const { return m_failed; }
            const std::uint8_t* Here() const { return m_bytes.data() + m_pos; }
            void Skip(std::size_t size) { if (size > Remaining()) { m_failed = true; } else { m_pos += size; } }

        private:
            std::span<const std::uint8_t> m_bytes;
            std::size_t                   m_pos    = 0;
            bool                          m_failed = false;
        };

        bool Finite(float v) { return std::isfinite(v); }
        bool Finite(const glm::vec4& v) { return Finite(v.x) && Finite(v.y) && Finite(v.z) && Finite(v.w); }

        void AppendPng(void* context, void* data, int size) {
            auto* out = static_cast<std::vector<std::uint8_t>*>(context);
            const auto* p = static_cast<const std::uint8_t*>(data);
            out->insert(out->end(), p, p + size);
        }

    }   // anonymous namespace

    // ------------------------------------------------------------------------------------------------------------- //
    //  NAMES
    // ------------------------------------------------------------------------------------------------------------- //

    const char* ToString(FontOutputType type) {
        switch (type) {
            case FontOutputType::Bitmap: return "Bitmap";
            case FontOutputType::Sdf:    return "SDF";
            case FontOutputType::Mtsdf:  return "MTSDF";
        }
        return "Unknown";
    }

    UIDrawMode DrawModeFor(FontOutputType type) {
        switch (type) {
            case FontOutputType::Bitmap: return UIDrawMode::Image;   // Alpha8 coverage: texel * colour
            case FontOutputType::Sdf:    return UIDrawMode::Sdf;
            case FontOutputType::Mtsdf:  return UIDrawMode::Mtsdf;   // median for the letter, alpha for effects
        }
        return UIDrawMode::Image;
    }

    // ------------------------------------------------------------------------------------------------------------- //
    //  FONTDATA
    // ------------------------------------------------------------------------------------------------------------- //

    void FontData::BuildLookup() {

        std::sort(kerning.begin(), kerning.end(), [](const FontKernPair& a, const FontKernPair& b) {
            return a.left != b.left ? a.left < b.left : a.right < b.right;
        });

        m_lookup.clear();
        m_lookup.reserve(glyphs.size());
        for (std::size_t g = 0; g < groups.size(); ++g) {
            const FontSizeGroup& group = groups[g];
            const std::size_t end = std::min<std::size_t>(std::size_t{ group.firstGlyph } + group.glyphCount, glyphs.size());
            for (std::size_t i = group.firstGlyph; i < end; ++i) {
                const std::uint64_t key = (static_cast<std::uint64_t>(g) << 32) | glyphs[i].codepoint;
                m_lookup.emplace(key, static_cast<std::uint32_t>(i));   // first one wins on a duplicate
            }
        }

    }   // FontData::BuildLookup()

    const FontGlyph* FontData::FindGlyph(std::uint32_t codepoint, std::size_t group) const {

        const std::uint64_t key = (static_cast<std::uint64_t>(group) << 32) | codepoint;
        const auto it = m_lookup.find(key);
        return it != m_lookup.end() ? &glyphs[it->second] : nullptr;

    }   // FontData::FindGlyph()

    float FontData::GetKerning(std::uint32_t left, std::uint32_t right) const {

        const auto it = std::lower_bound(kerning.begin(), kerning.end(), FontKernPair{ left, right, 0.0f },
            [](const FontKernPair& a, const FontKernPair& b) {
                return a.left != b.left ? a.left < b.left : a.right < b.right;
            });
        return (it != kerning.end() && it->left == left && it->right == right) ? it->adjust : 0.0f;

    }   // FontData::GetKerning()

    std::size_t FontData::ClosestGroup(float pixelSize) const {

        std::size_t best = 0;
        float bestDistance = 1.0e30f;
        for (std::size_t i = 0; i < groups.size(); ++i) {
            const float distance = std::fabs(groups[i].pixelSize - pixelSize);
            if (distance < bestDistance) { bestDistance = distance; best = i; }
        }
        return best;

    }   // FontData::ClosestGroup()

    bool FontData::Validate(std::string& error) const {

        error.clear();
        const auto fail = [&](std::string message) { error = std::move(message); return false; };

        if (static_cast<std::uint8_t>(outputType) > static_cast<std::uint8_t>(FontOutputType::Mtsdf)) {
            return fail("unknown output type " + std::to_string(static_cast<int>(outputType)));
        }

        const std::uint32_t wantChannels = outputType == FontOutputType::Mtsdf ? 4u : 1u;
        if (atlas.channels != wantChannels) {
            return fail(std::string(ToString(outputType)) + " needs a " + std::to_string(wantChannels)
                        + "-channel atlas, not " + std::to_string(atlas.channels));
        }
        if (atlas.width == 0 || atlas.height == 0 || atlas.width > kMaxAtlasSide || atlas.height > kMaxAtlasSide) {
            return fail("bad atlas size " + std::to_string(atlas.width) + "x" + std::to_string(atlas.height));
        }
        if (atlas.pixels.size() != std::size_t{ atlas.width } * atlas.height * atlas.channels) {
            return fail("atlas has " + std::to_string(atlas.pixels.size()) + " bytes, expected "
                        + std::to_string(std::size_t{ atlas.width } * atlas.height * atlas.channels));
        }
        if (outputType != FontOutputType::Bitmap && !(atlas.pixelRange > 0.0f)) {
            return fail("a distance-field atlas needs a pixel range above 0");
        }

        if (groups.empty() || groups.size() > kMaxGroups) {
            return fail("bad size group count " + std::to_string(groups.size()));
        }
        if (outputType != FontOutputType::Bitmap && groups.size() != 1) {
            return fail("SDF/MTSDF fonts have exactly one size group");
        }
        if (glyphs.size() > kMaxGlyphs) { return fail("too many glyphs"); }
        if (kerning.size() > kMaxKerns) { return fail("too many kerning pairs"); }

        for (const FontSizeGroup& group : groups) {
            if (!(group.pixelSize > 0.0f) || !Finite(group.pixelSize)) { return fail("a size group has no pixel size"); }
            if (std::size_t{ group.firstGlyph } + group.glyphCount > glyphs.size()) {
                return fail("a size group runs past the glyph list");
            }
        }

        const FontMetrics& m = metrics;
        if (!Finite(m.lineHeight) || !Finite(m.ascender) || !Finite(m.descender)
            || !Finite(m.underlineY) || !Finite(m.underlineThickness) || !(m.lineHeight > 0.0f)) {
            return fail("bad font metrics");
        }

        for (const FontGlyph& glyph : glyphs) {
            if (!Finite(glyph.advance) || !Finite(glyph.plane) || !Finite(glyph.uv)) {
                return fail("glyph U+" + std::to_string(glyph.codepoint) + " has a non-finite value");
            }
            if (glyph.HasInk()) {
                const glm::vec4& uv = glyph.uv;
                if (uv.x < 0.0f || uv.y < 0.0f || uv.z > 1.0f || uv.w > 1.0f || uv.z < uv.x || uv.w < uv.y) {
                    return fail("glyph U+" + std::to_string(glyph.codepoint) + " has its atlas rect outside the atlas");
                }
            }
        }
        for (const FontKernPair& pair : kerning) {
            if (!Finite(pair.adjust)) { return fail("a kerning pair has a non-finite value"); }
        }
        return true;

    }   // FontData::Validate()

    // ------------------------------------------------------------------------------------------------------------- //
    //  .CFONT
    // ------------------------------------------------------------------------------------------------------------- //

    bool WriteFont(const FontData& font, std::vector<std::uint8_t>& out, std::string& error, FontAtlasEncoding encoding) {

        out.clear();
        if (!font.Validate(error)) { error = "Can't write font: " + error; return false; }

        std::vector<std::uint8_t> atlasBytes;
        if (encoding == FontAtlasEncoding::Png) {
            const int stride = static_cast<int>(font.atlas.width * font.atlas.channels);
            const int ok = stbi_write_png_to_func(&AppendPng, &atlasBytes,
                static_cast<int>(font.atlas.width), static_cast<int>(font.atlas.height),
                static_cast<int>(font.atlas.channels), font.atlas.pixels.data(), stride);
            if (!ok || atlasBytes.empty()) { error = "Can't write font: PNG encoding failed"; return false; }
        } else {
            atlasBytes = font.atlas.pixels;
        }

        out.reserve(kHeaderSize + font.groups.size() * kGroupSize + font.glyphs.size() * kGlyphSize
                    + font.kerning.size() * kKernSize + atlasBytes.size());
        Writer w(out);

        w.Bytes(kMagic, sizeof(kMagic));
        w.U32(kVersion);
        w.U8(static_cast<std::uint8_t>(font.outputType));
        w.U8(static_cast<std::uint8_t>(font.atlas.channels));
        w.U8(static_cast<std::uint8_t>(encoding));
        w.U8(0);
        w.U32(font.atlas.width);
        w.U32(font.atlas.height);
        w.F32(font.atlas.pixelRange);
        w.F32(font.metrics.lineHeight);
        w.F32(font.metrics.ascender);
        w.F32(font.metrics.descender);
        w.F32(font.metrics.underlineY);
        w.F32(font.metrics.underlineThickness);
        w.U64(font.sourceHash);
        w.U64(font.settingsHash);
        w.U32(static_cast<std::uint32_t>(font.groups.size()));
        w.U32(static_cast<std::uint32_t>(font.glyphs.size()));
        w.U32(static_cast<std::uint32_t>(font.kerning.size()));
        w.U32(static_cast<std::uint32_t>(atlasBytes.size()));
        for (int i = 0; i < 4; ++i) { w.U32(0); }

        for (const FontSizeGroup& group : font.groups) {
            w.F32(group.pixelSize);
            w.U32(group.firstGlyph);
            w.U32(group.glyphCount);
        }
        for (const FontGlyph& glyph : font.glyphs) {
            w.U32(glyph.codepoint);
            w.F32(glyph.advance);
            for (int i = 0; i < 4; ++i) { w.F32(glyph.plane[i]); }
            for (int i = 0; i < 4; ++i) { w.F32(glyph.uv[i]); }
        }
        for (const FontKernPair& pair : font.kerning) {
            w.U32(pair.left);
            w.U32(pair.right);
            w.F32(pair.adjust);
        }
        w.Bytes(atlasBytes.data(), atlasBytes.size());

        error.clear();
        return true;

    }   // WriteFont()

    std::uint32_t CurrentFontFileVersion() { return kVersion; }

    bool ReadFontInfo(std::span<const std::uint8_t> bytes, FontFileInfo& out, std::string& error) {

        out = FontFileInfo{};
        error.clear();
        Reader r(bytes);
        char magic[4] = {};
        r.Bytes(magic, sizeof(magic));
        if (r.Failed() || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) { error = "not a .cfont file"; return false; }

        out.version = r.U32();
        if (out.version != kVersion) {
            error = "unsupported .cfont version " + std::to_string(out.version) + " (this engine reads " + std::to_string(kVersion) + ")";
            return false;
        }
        out.outputType = static_cast<FontOutputType>(r.U8());
        r.U8(); r.U8(); r.U8();                     // channels, encoding, reserved
        out.atlasWidth  = r.U32();
        out.atlasHeight = r.U32();
        r.Skip(4 + 5 * 4);                          // pixel range, metrics
        out.sourceHash   = r.U64();
        out.settingsHash = r.U64();
        out.groupCount   = r.U32();
        out.glyphCount   = r.U32();
        out.kernCount    = r.U32();
        if (r.Failed()) { error = "file is truncated (header)"; return false; }
        return true;

    }   // ReadFontInfo()

    bool ReadFont(std::span<const std::uint8_t> bytes, FontData& out, std::string& error) {

        error.clear();
        const auto fail = [&](std::string message) { error = std::move(message); return false; };

        Reader r(bytes);
        char magic[4] = {};
        r.Bytes(magic, sizeof(magic));
        if (r.Failed() || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) { return fail("not a .cfont file"); }

        const std::uint32_t version = r.U32();
        if (version != kVersion) {
            return fail("unsupported .cfont version " + std::to_string(version) + " (this engine reads "
                        + std::to_string(kVersion) + ") - recompile the font");
        }

        FontData font;
        const std::uint8_t outputType = r.U8();
        const std::uint8_t channels   = r.U8();
        const std::uint8_t encoding   = r.U8();
        r.U8();   // reserved

        font.outputType          = static_cast<FontOutputType>(outputType);
        font.atlas.channels      = channels;
        font.atlas.width         = r.U32();
        font.atlas.height        = r.U32();
        font.atlas.pixelRange    = r.F32();
        font.metrics.lineHeight  = r.F32();
        font.metrics.ascender    = r.F32();
        font.metrics.descender   = r.F32();
        font.metrics.underlineY  = r.F32();
        font.metrics.underlineThickness = r.F32();
        font.sourceHash          = r.U64();
        font.settingsHash        = r.U64();
        const std::uint32_t groupCount = r.U32();
        const std::uint32_t glyphCount = r.U32();
        const std::uint32_t kernCount  = r.U32();
        const std::uint32_t atlasBytes = r.U32();
        r.Skip(16);   // reserved

        if (r.Failed()) { return fail("file is truncated (header)"); }
        if (groupCount == 0 || groupCount > kMaxGroups || glyphCount > kMaxGlyphs || kernCount > kMaxKerns) {
            return fail("corrupt header (counts out of range)");
        }
        if (encoding > static_cast<std::uint8_t>(FontAtlasEncoding::Png)) {
            return fail("unknown atlas encoding " + std::to_string(encoding));
        }
        if (channels != 1 && channels != 4) { return fail("corrupt header (atlas channels)"); }
        if (font.atlas.width == 0 || font.atlas.height == 0 || font.atlas.width > kMaxAtlasSide || font.atlas.height > kMaxAtlasSide) {
            return fail("corrupt header (atlas size)");
        }
        // Check the tables fit before allocating anything for them.
        const std::size_t tables = std::size_t{ groupCount } * kGroupSize + std::size_t{ glyphCount } * kGlyphSize
                                 + std::size_t{ kernCount } * kKernSize;
        if (tables + atlasBytes > r.Remaining()) { return fail("file is truncated"); }

        font.groups.resize(groupCount);
        for (FontSizeGroup& group : font.groups) {
            group.pixelSize  = r.F32();
            group.firstGlyph = r.U32();
            group.glyphCount = r.U32();
        }
        font.glyphs.resize(glyphCount);
        for (FontGlyph& glyph : font.glyphs) {
            glyph.codepoint = r.U32();
            glyph.advance   = r.F32();
            for (int i = 0; i < 4; ++i) { glyph.plane[i] = r.F32(); }
            for (int i = 0; i < 4; ++i) { glyph.uv[i]    = r.F32(); }
        }
        font.kerning.resize(kernCount);
        for (FontKernPair& pair : font.kerning) {
            pair.left   = r.U32();
            pair.right  = r.U32();
            pair.adjust = r.F32();
        }

        const std::uint8_t* atlasData = r.Here();
        r.Skip(atlasBytes);
        if (r.Failed()) { return fail("file is truncated (atlas)"); }

        const std::size_t texels = std::size_t{ font.atlas.width } * font.atlas.height * channels;
        if (encoding == static_cast<std::uint8_t>(FontAtlasEncoding::Raw)) {
            if (atlasBytes != texels) { return fail("atlas is the wrong size"); }
            font.atlas.pixels.assign(atlasData, atlasData + atlasBytes);
        } else {
            int w = 0, h = 0, fileChannels = 0;
            std::unique_ptr<stbi_uc, void (*)(void*)> decoded(
                stbi_load_from_memory(atlasData, static_cast<int>(atlasBytes), &w, &h, &fileChannels, channels),
                &stbi_image_free);
            if (!decoded) { return fail(std::string("atlas PNG decode failed: ") + stbi_failure_reason()); }
            if (static_cast<std::uint32_t>(w) != font.atlas.width || static_cast<std::uint32_t>(h) != font.atlas.height) {
                return fail("atlas PNG is " + std::to_string(w) + "x" + std::to_string(h) + ", header says "
                            + std::to_string(font.atlas.width) + "x" + std::to_string(font.atlas.height));
            }
            font.atlas.pixels.assign(decoded.get(), decoded.get() + texels);
        }

        if (r.Remaining() != 0) { return fail("unexpected bytes after the atlas"); }
        if (!font.Validate(error)) { error = "corrupt font: " + error; return false; }

        font.BuildLookup();
        out = std::move(font);
        return true;

    }   // ReadFont()

}   // NAMESPACE CRUCIBLE
