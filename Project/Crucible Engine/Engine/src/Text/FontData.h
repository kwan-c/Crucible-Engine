#pragma once
/*************************************************************************
  \file       FontData.h
  \brief
    A compiled font (.cfont): metrics, glyphs, kerning and the atlas
    texture's pixels, plus reading and writing the .cfont file.
  \details
    The editor's font compiler bakes a .ttf/.otf into a .cfont (Bitmap, SDF
    or MTSDF); the engine only ever loads the .cfont, so the game needs no
    FreeType or msdfgen. ResourceManager::GetFont() loads one and uploads its
    atlas; this file knows nothing about the GPU.

    Units and conventions (all chosen to match the UI, see UITypes.h):
      - Glyph metrics are in EMS: 1.0 = the font size. Multiply by the text's
        pixel size (UIText::fontSize) to get canvas pixels.
      - +y is DOWN. The pen sits on the baseline; a glyph's plane bounds are
        offsets from the pen, so the top of a capital letter is negative.
      - Atlas UVs: (0, 0) = the atlas's top-left corner, like UIVertex::uv.
      - Ascender / descender are distances from the baseline, both positive.

    How the output types differ:
      - Mtsdf:  4-channel atlas. RGB = multi-channel distance (median = the
                distance), A = true distance (for outline / glow / shadow).
                Upload as TexturePixelFormat::RGBA8, draw with UIDrawMode::Mtsdf.
      - Sdf:    1-channel distance atlas. Upload as Alpha8, draw with
                UIDrawMode::Sdf.
      - Bitmap: 1-channel coverage atlas, rasterized at fixed pixel sizes.
                Upload as Alpha8, draw with UIDrawMode::Image. Each baked
                size is a FontSizeGroup; pick one with ClosestGroup().
      SDF and MTSDF have exactly one group (the size the field was baked at),
      and scale to any size.

    .cfont layout (little-endian, version 1). Read with ReadFont(), written
    with WriteFont(); see FontData.cpp for the field order.
      header | size groups | glyphs | kerning pairs | atlas bytes
    The atlas is stored as a PNG (lossless) by default, or raw.
*************************************************************************/

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "EngineExport.h"
#include "Graphics/TextureHandle.h"
#include "Graphics/UITypes.h"

namespace Crucible {

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL members in an exported class; Engine.dll and its clients share one toolchain
#endif

    /// What kind of atlas the font was compiled into. Stored in the .cfont as a byte.
    enum class FontOutputType : std::uint8_t {
        Bitmap = 0,   ///< Coverage, fixed sizes (crisp only at the baked sizes).
        Sdf    = 1,   ///< Single-channel signed distance (rounds sharp corners slightly).
        Mtsdf  = 2,   ///< Multi-channel + true signed distance (sharp corners, effects).
    };

    /// "Bitmap", "SDF" or "MTSDF".
    ENGINE_API const char* ToString(FontOutputType type);

    /// How the renderer must draw a font of this type (Bitmap -> Image, Sdf -> Sdf, Mtsdf -> Mtsdf).
    ENGINE_API UIDrawMode DrawModeFor(FontOutputType type);

    /// Font-wide metrics, in ems (see the file header).
    struct FontMetrics {
        float lineHeight         = 1.2f;   ///< Baseline to baseline.
        float ascender           = 0.8f;   ///< Baseline up to the top of tall letters (positive).
        float descender          = 0.2f;   ///< Baseline down to the bottom of g, p, y (positive).
        float underlineY         = 0.1f;   ///< Baseline to the underline's centre (+y down, so usually positive).
        float underlineThickness = 0.05f;
    };

    /// One glyph. Whitespace has an advance but an empty atlas rect (HasInk() == false).
    struct FontGlyph {
        std::uint32_t codepoint = 0;
        float         advance   = 0.0f;           ///< How far the pen moves after this glyph, in ems.
        glm::vec4     plane{ 0.0f };              ///< Quad from the pen: min x, min y, max x, max y, in ems, +y down.
        glm::vec4     uv{ 0.0f };                 ///< Atlas rect: min u, min v, max u, max v (0,0 = top-left).

        bool HasInk() const { return plane.z > plane.x && plane.w > plane.y; }
    };

    /// A run of glyphs baked at one pixel size. SDF/MTSDF fonts have one; Bitmap fonts one per baked size.
    struct FontSizeGroup {
        float         pixelSize  = 0.0f;   ///< Pixels per em this group was rasterized at.
        std::uint32_t firstGlyph = 0;      ///< Into FontData::m_glyphs.
        std::uint32_t glyphCount = 0;
    };

    /// Extra advance between two glyphs, in ems (usually negative: "AV" moves closer).
    struct FontKernPair {
        std::uint32_t left   = 0;
        std::uint32_t right  = 0;
        float         adjust = 0.0f;
    };

    /// The atlas texture's pixels: tightly packed rows, top row first.
    struct FontAtlas {
        std::uint32_t             width      = 0;
        std::uint32_t             height     = 0;
        std::uint32_t             channels   = 0;      ///< 1 (Bitmap, SDF) or 4 (MTSDF).
        float                     pixelRange = 0.0f;   ///< Distance-field range in atlas texels (0 for Bitmap).
        std::vector<std::uint8_t> pixels;              ///< width * height * channels bytes.
    };

    /*************************************************************************
     \brief
        A compiled font in memory.
     \details
        Fill the public fields (the compiler does), call BuildLookup(), then
        query with FindGlyph / GetKerning. ReadFont() does all of that.
     *************************************************************************/
    class ENGINE_API FontData {
    public:
        FontOutputType             m_outputType = FontOutputType::Mtsdf;
        FontMetrics                m_metrics;
        std::vector<FontSizeGroup> m_groups;     ///< At least one.
        std::vector<FontGlyph>     m_glyphs;     ///< Grouped by FontSizeGroup.
        std::vector<FontKernPair>  m_kerning;    ///< Sorted by (left, right); BuildLookup() sorts it.
        FontAtlas                  m_atlas;

        /// What it was compiled from, so the editor can tell when it is out of date.
        /// Not used at runtime. 0 = unknown.
        std::uint64_t m_sourceHash   = 0;   ///< The source font's contentHash (its .meta "hash").
        std::uint64_t m_settingsHash = 0;   ///< Hash of the import settings it was compiled with.

        /// Sorts the kerning and builds the codepoint lookup. Call after filling the fields.
        void BuildLookup();

        /// The glyph for a codepoint in a size group, or null if the font doesn't have it.
        const FontGlyph* FindGlyph(std::uint32_t codepoint, std::size_t group = 0) const;

        /// Kerning between two codepoints in ems (0 if none).
        float GetKerning(std::uint32_t left, std::uint32_t right) const;

        /// Index of the size group whose pixel size is closest to `pixelSize` (0 for SDF/MTSDF).
        std::size_t ClosestGroup(float pixelSize) const;

        /// Checks every count, range and size is consistent. On failure, `error` says what is wrong.
        bool Validate(std::string& error) const;

    private:
        // (group << 32 | codepoint) -> index into glyphs
        std::unordered_map<std::uint64_t, std::uint32_t> m_lookup;
    };

    /// How WriteFont stores the atlas.
    enum class FontAtlasEncoding : std::uint8_t {
        Raw = 0,   ///< The bytes as they are (fastest to load, biggest file).
        Png = 1,   ///< Lossless PNG (much smaller; the default).
    };

    /*************************************************************************
     \brief  Decodes a .cfont file's bytes. On success `out` is ready to use
             (BuildLookup() already called).
     \param[in]  bytes  The whole file.
     \param[out] out    The font. Left unspecified on failure.
     \param[out] error  Why it failed (empty on success).
     \return true on success.
     *************************************************************************/
    ENGINE_API bool ReadFont(std::span<const std::uint8_t> bytes, FontData& out, std::string& error);

    /*************************************************************************
     \brief  Encodes a font as .cfont bytes (for the editor's compiler).
     \param[in]  font      Must pass Validate().
     \param[out] out       The file's bytes.
     \param[out] error     Why it failed (empty on success).
     \param[in]  encoding  How to store the atlas.
     \return true on success.
     *************************************************************************/
    ENGINE_API bool WriteFont(const FontData& font, std::vector<std::uint8_t>& out, std::string& error,
                              FontAtlasEncoding encoding = FontAtlasEncoding::Png);

    /// A .cfont's header: what ReadFontInfo() returns without decoding the atlas.
    struct FontFileInfo {
        std::uint32_t  version      = 0;
        FontOutputType outputType   = FontOutputType::Bitmap;
        std::uint32_t  atlasWidth   = 0;
        std::uint32_t  atlasHeight  = 0;
        std::uint32_t  groupCount   = 0;
        std::uint32_t  glyphCount   = 0;
        std::uint32_t  kernCount    = 0;
        std::uint64_t  sourceHash   = 0;
        std::uint64_t  settingsHash = 0;
    };

    /// The .cfont version this engine reads and writes.
    ENGINE_API std::uint32_t CurrentFontFileVersion();

    /*************************************************************************
     \brief  Reads only a .cfont's header - cheap, for the editor's "is this
             compiled font up to date?" check.
     \param[in]  bytes  The file (only the first 92 bytes are needed).
     \param[out] out    The header. `version` is filled even when the
                        version is unsupported (then false is returned).
     \param[out] error  Why it failed.
     \return true if this is a .cfont of a version this engine reads.
     *************************************************************************/
    ENGINE_API bool ReadFontInfo(std::span<const std::uint8_t> bytes, FontFileInfo& out, std::string& error);

    /*************************************************************************
     \brief  A loaded font: its data, and its atlas on the GPU.
             ResourceManager::GetFont() owns these; the pointer stays valid
             until the font is reloaded (ReloadFont) or the manager is
             destroyed.
     *************************************************************************/
    struct FontAsset {
        FontData      data;
        TextureHandle texture{};                      ///< The atlas, uploaded.
        UIDrawMode    drawMode = UIDrawMode::Mtsdf;   ///< DrawModeFor(data.m_outputType).
    };

#ifdef _MSC_VER
#pragma warning(pop)
#endif

}   // NAMESPACE CRUCIBLE
