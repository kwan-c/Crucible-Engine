/*************************************************************************
  \file       UITestOverlay.cpp
  \brief
    See UITestOverlay.h.
*************************************************************************/
#include "Graphics/UITestOverlay.h"

#include "Graphics/RenderSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// stb_easy_font is all static functions, and this file doesn't use every one of them.
#if defined(_MSC_VER)
    #pragma warning(disable : 4505 4244)   // unreferenced static function; float conversions inside stb
#elif defined(__clang__)
    #pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
    #pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include <stb_easy_font.h>

namespace Crucible {

    namespace {

        // stb_easy_font's vertex: x, y, z, then an RGBA colour we replace with our own.
        struct EasyFontVertex { float x, y, z; unsigned char rgba[4]; };
        static_assert(sizeof(EasyFontVertex) == 16, "stb_easy_font writes 16-byte vertices");

        constexpr uint32_t kWhite  = PackUIColour(255, 255, 255);
        constexpr uint32_t kGrey   = PackUIColour(176, 176, 176);
        constexpr uint32_t kYellow = PackUIColour(255, 212, 0);

        // Circle distance field: 64 x 64 texels, radius 24, 0.5 on the edge, changing by 0.5 over
        // 8 texels either side of it.
        constexpr uint32_t kSdfSize   = 64;
        constexpr float    kSdfRadius = 24.0f;
        constexpr float    kSdfSpread = 8.0f;

    }   // namespace

    void UITestOverlay::MakeTextures(Renderer& renderer) {

        std::vector<unsigned char> alpha(kSdfSize * kSdfSize);
        std::vector<unsigned char> rgba(kSdfSize * kSdfSize * 4);
        const float centre = static_cast<float>(kSdfSize) * 0.5f;
        for (uint32_t y = 0; y < kSdfSize; ++y) {
            for (uint32_t x = 0; x < kSdfSize; ++x) {
                const float dx = static_cast<float>(x) + 0.5f - centre;
                const float dy = static_cast<float>(y) + 0.5f - centre;
                const float inside = kSdfRadius - std::sqrt(dx * dx + dy * dy);   // > 0 inside the circle
                const float value  = std::clamp(0.5f + inside / (2.0f * kSdfSpread), 0.0f, 1.0f);
                const auto  byte   = static_cast<unsigned char>(value * 255.0f + 0.5f);
                alpha[y * kSdfSize + x] = byte;
                for (int c = 0; c < 4; ++c) { rgba[(y * kSdfSize + x) * 4 + c] = byte; }
            }
        }
        m_sdfCircle     = renderer.CreateTextureFromPixels(alpha.data(), kSdfSize, kSdfSize, TexturePixelFormat::Alpha8);
        m_sdfCircleRgba = renderer.CreateTextureFromPixels(rgba.data(), kSdfSize, kSdfSize, TexturePixelFormat::RGBA8);
        m_texturesMade  = true;

    }   // UITestOverlay::MakeTextures()

    void UITestOverlay::AddQuadIndices(uint32_t base, TextureHandle texture, UIDrawMode mode) {

        const uint32_t first = static_cast<uint32_t>(m_indices.size());
        for (uint32_t i : { 0u, 1u, 2u, 0u, 2u, 3u }) { m_indices.push_back(base + i); }

        // Extend the previous command when nothing about the draw changes (usually the case: most
        // of the overlay is solid shapes and text), otherwise start a new one.
        if (!m_commands.empty()) {
            UIDrawCommand& last = m_commands.back();
            if (last.texture == texture && last.mode == mode && last.clipRect == m_clip && last.firstIndex + last.indexCount == first) {
                last.indexCount += 6;
                return;
            }
        }
        m_commands.push_back({ .texture = texture, .mode = mode, .clipRect = m_clip, .firstIndex = first, .indexCount = 6, .vertexOffset = 0 });

    }   // UITestOverlay::AddQuadIndices()

    void UITestOverlay::Quad(float x0, float y0, float x1, float y1, uint32_t colour, TextureHandle texture, UIDrawMode mode, const glm::vec4& uv) {

        const uint32_t base = static_cast<uint32_t>(m_vertices.size());
        m_vertices.push_back({ .position = { x0, y0 }, .uv = { uv.x, uv.y }, .colour = colour });
        m_vertices.push_back({ .position = { x1, y0 }, .uv = { uv.z, uv.y }, .colour = colour });
        m_vertices.push_back({ .position = { x1, y1 }, .uv = { uv.z, uv.w }, .colour = colour });
        m_vertices.push_back({ .position = { x0, y1 }, .uv = { uv.x, uv.w }, .colour = colour });
        AddQuadIndices(base, texture, mode);

    }   // UITestOverlay::Quad()

    void UITestOverlay::Frame(float x0, float y0, float x1, float y1, float t, uint32_t colour) {

        Quad(x0, y0, x1, y0 + t, colour);           // top
        Quad(x0, y1 - t, x1, y1, colour);           // bottom
        Quad(x0, y0 + t, x0 + t, y1 - t, colour);   // left
        Quad(x1 - t, y0 + t, x1, y1 - t, colour);   // right

    }   // UITestOverlay::Frame()

    float UITestOverlay::TextWidth(const std::string& text, float scale) {

        std::string copy = text;   // stb_easy_font takes char*, not const char*
        return static_cast<float>(stb_easy_font_width(copy.data())) * scale;

    }   // UITestOverlay::TextWidth()

    void UITestOverlay::Text(float x, float y, float scale, uint32_t colour, const std::string& text) {

        // About 270 bytes of quads per character in the worst case; grow to fit.
        const size_t needed = (text.size() + 1) * 300;
        if (m_fontScratch.size() < needed) { m_fontScratch.resize(needed); }

        std::string copy = text;
        const int quads = stb_easy_font_print(0.0f, 0.0f, copy.data(), nullptr, m_fontScratch.data(), static_cast<int>(m_fontScratch.size()));
        const auto* v = reinterpret_cast<const EasyFontVertex*>(m_fontScratch.data());
        for (int q = 0; q < quads; ++q) {
            const uint32_t base = static_cast<uint32_t>(m_vertices.size());
            for (int corner = 0; corner < 4; ++corner) {
                const EasyFontVertex& in = v[q * 4 + corner];
                m_vertices.push_back({ .position = { x + in.x * scale, y + in.y * scale }, .uv = { 0.0f, 0.0f }, .colour = colour });
            }
            AddQuadIndices(base, TextureHandle{}, UIDrawMode::Image);
        }

    }   // UITestOverlay::Text()

    void UITestOverlay::Submit(Renderer& renderer, float time) {

        const glm::uvec2 screen = renderer.GetGameScreenSize();
        if (screen.x == 0 || screen.y == 0) { return; }
        if (!m_texturesMade) { MakeTextures(renderer); }

        m_vertices.clear();
        m_indices.clear();
        m_commands.clear();
        m_clip = glm::vec4(-1.0e9f, -1.0e9f, 1.0e9f, 1.0e9f);

        const float w = static_cast<float>(screen.x);
        const float h = static_cast<float>(screen.y);

        // A 2-pixel yellow frame on the very edge of the screen: all four sides should be visible
        // and equally thick, which checks the pixel mapping.
        Frame(0.0f, 0.0f, w, h, 2.0f, kYellow);

        // ---- Panel ----
        const float px = 24.0f, py = 24.0f, pw = 560.0f, ph = 350.0f;
        Quad(px, py, px + pw, py + ph, PackUIColour(16, 20, 24, 200));   // 78% opaque: the scene should show through faintly
        Frame(px, py, px + pw, py + ph, 1.0f, PackUIColour(90, 100, 110));

        Text(px + 16.0f, py + 14.0f, 3.0f, kWhite, "UI TEST OVERLAY");
        char line[128];
        std::snprintf(line, sizeof(line), "game screen %u x %u", screen.x, screen.y);
        Text(px + 16.0f, py + 46.0f, 2.0f, kGrey, line);

        // ---- Colours: pick these with a colour picker on a screenshot; they should read back exactly ----
        struct Swatch { uint32_t colour; const char* label; };
        const Swatch swatches[] = {
            { PackUIColour(255, 136, 0),   "FF8800" },
            { PackUIColour(51, 153, 255),  "3399FF" },
            { PackUIColour(128, 128, 128), "808080" },
            { PackUIColour(255, 255, 255), "FFFFFF" },
        };
        float sx = px + 16.0f;
        const float sy = py + 80.0f;
        for (const Swatch& s : swatches) {
            Quad(sx, sy, sx + 56.0f, sy + 40.0f, s.colour);
            Text(sx, sy + 46.0f, 1.5f, kGrey, s.label);
            sx += 72.0f;
        }

        // ---- Clipping: stripes wider than their box, moving; only the part inside the box shows ----
        const float cx0 = px + 16.0f, cy0 = py + 160.0f, cx1 = cx0 + 260.0f, cy1 = cy0 + 40.0f;
        Frame(cx0 - 1.0f, cy0 - 1.0f, cx1 + 1.0f, cy1 + 1.0f, 1.0f, kGrey);
        m_clip = glm::vec4(cx0, cy0, cx1, cy1);
        const float scroll = std::fmod(time * 60.0f, 40.0f);
        for (float x = cx0 - 200.0f + scroll; x < cx1 + 200.0f; x += 40.0f) {
            Quad(x, cy0 - 20.0f, x + 20.0f, cy1 + 20.0f, PackUIColour(70, 200, 120));
        }
        m_clip = glm::vec4(-1.0e9f, -1.0e9f, 1.0e9f, 1.0e9f);
        Text(cx1 + 12.0f, cy0 + 12.0f, 2.0f, kGrey, "clipped");

        // ---- Distance fields: the same 64 x 64 circle drawn small and large ----
        // Sdf and Msdf edges stay about one pixel soft at any size; Image shows the raw field.
        const float dy = py + 220.0f;
        float dx = px + 16.0f;
        const auto circle = [&](float size, TextureHandle texture, UIDrawMode mode, const char* label) {
            Quad(dx, dy, dx + size, dy + size, PackUIColour(255, 136, 0), texture, mode);
            Text(dx, dy + size + 6.0f, 1.5f, kGrey, label);
            dx += size + 24.0f;
        };
        circle(24.0f, m_sdfCircle, UIDrawMode::Sdf, "sdf");
        circle(88.0f, m_sdfCircle, UIDrawMode::Sdf, "sdf, large");
        circle(88.0f, m_sdfCircleRgba, UIDrawMode::Msdf, "msdf");
        circle(88.0f, m_sdfCircle, UIDrawMode::Image, "raw field");

        // ---- World-to-screen: a marker on the world origin (ProjectToGameScreen) ----
        glm::vec2 origin;
        if (renderer.ProjectToGameScreen(glm::vec3(0.0f), origin)) {
            Quad(origin.x - 12.0f, origin.y - 1.0f, origin.x + 12.0f, origin.y + 1.0f, kYellow);
            Quad(origin.x - 1.0f, origin.y - 12.0f, origin.x + 1.0f, origin.y + 12.0f, kYellow);
            const std::string label = "world origin";
            Text(origin.x - TextWidth(label, 1.5f) * 0.5f, origin.y + 16.0f, 1.5f, kYellow, label);
        }

        renderer.SubmitUI(UIDrawList{ .vertices = m_vertices, .indices = m_indices, .commands = m_commands });

    }   // UITestOverlay::Submit()

}   // NAMESPACE CRUCIBLE
