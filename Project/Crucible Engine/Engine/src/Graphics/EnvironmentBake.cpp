/*************************************************************************
  \file       EnvironmentBake.cpp
  \brief
    See EnvironmentBake.h. Moved out of EnvironmentMaps.cpp unchanged, so a
    sky baked offline (.sky) matches one built at runtime.
*************************************************************************/
#include "Graphics/EnvironmentBake.h"

#include <algorithm>
#include <cmath>

namespace Crucible::EnvironmentBake {

    void DirectionToCubeTexel(const glm::vec3& d, uint32_t size, uint32_t& face, uint32_t& x, uint32_t& y) {

        const glm::vec3 a = glm::abs(d);
        float u, v, major;
        if (a.x >= a.y && a.x >= a.z) {
            major = a.x;
            if (d.x > 0.0f) { face = 0; u = -d.z; v = -d.y; } else { face = 1; u = d.z; v = -d.y; }
        } else if (a.y >= a.z) {
            major = a.y;
            if (d.y > 0.0f) { face = 2; u = d.x; v = d.z; } else { face = 3; u = d.x; v = -d.z; }
        } else {
            major = a.z;
            if (d.z > 0.0f) { face = 4; u = d.x; v = -d.y; } else { face = 5; u = -d.x; v = -d.y; }
        }
        major = std::max(major, 1e-8f);
        const float fs = static_cast<float>(size);
        x = static_cast<uint32_t>(glm::clamp((u / major * 0.5f + 0.5f) * fs, 0.0f, fs - 1.0f));
        y = static_cast<uint32_t>(glm::clamp((v / major * 0.5f + 0.5f) * fs, 0.0f, fs - 1.0f));

    }

    glm::vec3 CubeFaceTexelDirection(uint32_t face, uint32_t x, uint32_t y, uint32_t size) {

        const float u = 2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(size) - 1.0f;
        const float v = 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(size) - 1.0f;

        glm::vec3 d;
        switch (face) {
            case 0:  d = glm::vec3( 1.0f,   -v,   -u); break;   // +X
            case 1:  d = glm::vec3(-1.0f,   -v,    u); break;   // -X
            case 2:  d = glm::vec3(    u, 1.0f,    v); break;   // +Y
            case 3:  d = glm::vec3(    u,-1.0f,   -v); break;   // -Y
            case 4:  d = glm::vec3(    u,   -v, 1.0f); break;   // +Z
            default: d = glm::vec3(   -u,   -v,-1.0f); break;   // -Z
        }
        return glm::normalize(d);
    }

    std::vector<glm::vec3> EquirectToCube(const float* pixels, int w, int h, uint32_t faceSize) {

        // Bilinear lookup, wrapping horizontally (longitude is periodic) and clamping vertically.
        auto fetch = [&](int x, int y) -> glm::vec3 {
            x = ((x % w) + w) % w;
            y = std::clamp(y, 0, h - 1);
            const float* p = pixels + (static_cast<size_t>(y) * w + x) * 4;
            return glm::vec3(p[0], p[1], p[2]);
        };
        auto sample = [&](float u, float v) -> glm::vec3 {
            const float fx = u * static_cast<float>(w) - 0.5f;
            const float fy = v * static_cast<float>(h) - 0.5f;
            const int   x0 = static_cast<int>(std::floor(fx));
            const int   y0 = static_cast<int>(std::floor(fy));
            const float tx = fx - static_cast<float>(x0);
            const float ty = fy - static_cast<float>(y0);
            const glm::vec3 top    = glm::mix(fetch(x0, y0),     fetch(x0 + 1, y0),     tx);
            const glm::vec3 bottom = glm::mix(fetch(x0, y0 + 1), fetch(x0 + 1, y0 + 1), tx);
            return glm::mix(top, bottom, ty);
        };

        constexpr float kInvTwoPi = 0.15915494309f;
        constexpr float kInvPi    = 0.31830988618f;

        std::vector<glm::vec3> faces(static_cast<size_t>(faceSize) * faceSize * 6);
        size_t i = 0;
        for (uint32_t face = 0; face < 6; ++face) {
            for (uint32_t y = 0; y < faceSize; ++y) {
                for (uint32_t x = 0; x < faceSize; ++x) {
                    const glm::vec3 d = CubeFaceTexelDirection(face, x, y, faceSize);
                    // Longitude from +X towards +Z (turning right, in this right-handed Y-up
                    // world) runs left-to-right across the panorama; latitude runs top (+Y) to
                    // bottom (-Y).
                    const float u = std::atan2(d.z, d.x) * kInvTwoPi + 0.5f;
                    const float v = std::acos(glm::clamp(d.y, -1.0f, 1.0f)) * kInvPi;
                    faces[i++] = sample(u, v);
                }
            }
        }
        return faces;
    }

    Lighting BakeLighting(const std::vector<glm::vec3>& source, uint32_t faceSize) {

        // Done once per sky, at a small size (the result is blurry by nature):
        //   - an "environment map": level 0 is the sky shrunk to at most 128x128 per face, and
        //     each smaller level is that sky blurred as a rougher surface would reflect it;
        //   - nine spherical-harmonic coefficients describing the sky's overall diffuse light.
        Lighting out;
        const size_t facePixels = static_cast<size_t>(faceSize) * faceSize;

        // 1. A chain of plain shrunk copies: base size (power of two, at most 128) down to 16.
        //    Not smaller: the GPU stretches the smallest level over a whole hemisphere of
        //    reflection directions, and a 4x4 image stretched that far shows its texels as flat
        //    patches on round objects.
        uint32_t baseSize = 4;
        while (baseSize * 2 <= std::min(faceSize, 128u)) { baseSize *= 2; }
        std::vector<uint32_t>&              sizes = out.sizes;
        std::vector<std::vector<glm::vec3>> boxes;
        for (uint32_t s = baseSize; s >= std::min(baseSize, 16u) && sizes.size() < 6; s /= 2) { sizes.push_back(s); }

        boxes.emplace_back(static_cast<size_t>(baseSize) * baseSize * 6);
        for (uint32_t face = 0; face < 6; ++face) {
            for (uint32_t y = 0; y < baseSize; ++y) {
                const uint32_t y0 = y * faceSize / baseSize, y1 = std::max((y + 1) * faceSize / baseSize, y0 + 1);
                for (uint32_t x = 0; x < baseSize; ++x) {
                    const uint32_t x0 = x * faceSize / baseSize, x1 = std::max((x + 1) * faceSize / baseSize, x0 + 1);
                    glm::vec3 sum(0.0f);
                    for (uint32_t sy = y0; sy < y1; ++sy) {
                        for (uint32_t sx = x0; sx < x1; ++sx) { sum += source[face * facePixels + static_cast<size_t>(sy) * faceSize + sx]; }
                    }
                    boxes[0][(static_cast<size_t>(face) * baseSize + y) * baseSize + x] = sum / static_cast<float>((y1 - y0) * (x1 - x0));
                }
            }
        }
        for (size_t level = 1; level < sizes.size(); ++level) {
            const uint32_t s = sizes[level], ps = sizes[level - 1];
            boxes.emplace_back(static_cast<size_t>(s) * s * 6);
            for (uint32_t face = 0; face < 6; ++face) {
                for (uint32_t y = 0; y < s; ++y) {
                    for (uint32_t x = 0; x < s; ++x) {
                        const size_t row0 = (static_cast<size_t>(face) * ps + y * 2) * ps + x * 2;
                        const size_t row1 = row0 + ps;
                        boxes[level][(static_cast<size_t>(face) * s + y) * s + x] =
                            (boxes[level - 1][row0] + boxes[level - 1][row0 + 1] + boxes[level - 1][row1] + boxes[level - 1][row1 + 1]) * 0.25f;
                    }
                }
            }
        }
        const auto lookup = [&](size_t level, const glm::vec3& dir) {
            uint32_t face, x, y;
            DirectionToCubeTexel(dir, sizes[level], face, x, y);
            return boxes[level][(static_cast<size_t>(face) * sizes[level] + y) * sizes[level] + x];
        };

        // 2. Diffuse light: project the sky onto nine spherical harmonics (from a small level -
        //    the result only keeps the broadest features anyway), with the cosine falloff of a
        //    diffuse surface and the basis constants folded in, so the shader evaluates a plain
        //    polynomial of the normal (see SkyIrradiance in DefaultShader.slang).
        {
            size_t level = 0;
            while (level + 1 < sizes.size() && sizes[level] > 32) { ++level; }
            const uint32_t s = sizes[level];
            std::array<glm::vec3, 9> sum{};
            float weightSum = 0.0f;
            for (uint32_t face = 0; face < 6; ++face) {
                for (uint32_t y = 0; y < s; ++y) {
                    for (uint32_t x = 0; x < s; ++x) {
                        const glm::vec3 d = CubeFaceTexelDirection(face, x, y, s);   // normalized
                        // A texel near a face's corner covers less of the sphere than one at its centre.
                        const float u = 2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(s) - 1.0f;
                        const float v = 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(s) - 1.0f;
                        const float w = 1.0f / std::pow(1.0f + u * u + v * v, 1.5f);
                        const glm::vec3 c = boxes[level][(static_cast<size_t>(face) * s + y) * s + x] * w;
                        const float basis[9] = {
                            0.282095f, 0.488603f * d.y, 0.488603f * d.z, 0.488603f * d.x,
                            1.092548f * d.x * d.y, 1.092548f * d.y * d.z, 0.315392f * (3.0f * d.z * d.z - 1.0f),
                            1.092548f * d.x * d.z, 0.546274f * (d.x * d.x - d.y * d.y) };
                        for (int i = 0; i < 9; ++i) { sum[i] += c * basis[i]; }
                        weightSum += w;
                    }
                }
            }
            const float norm = 4.0f * 3.14159265f / std::max(weightSum, 1e-6f);
            // (cosine-lobe factor / pi) * basis constant, per coefficient.
            const float scale[9] = {
                1.0f * 0.282095f,
                (2.0f / 3.0f) * 0.488603f, (2.0f / 3.0f) * 0.488603f, (2.0f / 3.0f) * 0.488603f,
                0.25f * 1.092548f, 0.25f * 1.092548f, 0.25f * 0.315392f, 0.25f * 1.092548f, 0.25f * 0.546274f };
            for (int i = 0; i < 9; ++i) { out.sh[i] = sum[i] * norm * scale[i]; }
        }

        // 3. Reflections: level 0 is the shrunk sky as is (a mirror); each further level is what a
        //    rougher surface would reflect - the average of the sky over the spread of directions
        //    such a surface scatters into (GGX importance sampling, 64 directions per texel, read
        //    from the shrunk copy of the same size so bright spots do not turn into speckles).
        constexpr uint32_t kSamples = 64;
        out.levels.resize(sizes.size());
        for (size_t level = 0; level < sizes.size(); ++level) {
            const uint32_t s = sizes[level];
            std::vector<glm::vec3>& colours = out.levels[level];
            colours.resize(static_cast<size_t>(s) * s * 6);
            const float roughness = sizes.size() > 1 ? static_cast<float>(level) / static_cast<float>(sizes.size() - 1) : 0.0f;
            const float a         = roughness * roughness;
            for (uint32_t face = 0; face < 6; ++face) {
                for (uint32_t y = 0; y < s; ++y) {
                    for (uint32_t x = 0; x < s; ++x) {
                        glm::vec3 colour;
                        if (level == 0) {
                            colour = boxes[0][(static_cast<size_t>(face) * s + y) * s + x];
                        } else {
                            const glm::vec3 n  = CubeFaceTexelDirection(face, x, y, s);
                            const glm::vec3 up = std::abs(n.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
                            const glm::vec3 tx = glm::normalize(glm::cross(up, n));
                            const glm::vec3 ty = glm::cross(n, tx);
                            glm::vec3 sum(0.0f);
                            float     weight = 0.0f;
                            for (uint32_t i = 0; i < kSamples; ++i) {
                                // Hammersley point i: evenly spread pairs (u1, u2) in the unit square.
                                uint32_t bits = i;
                                bits = (bits << 16u) | (bits >> 16u);
                                bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
                                bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
                                bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
                                bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
                                const float u1 = (static_cast<float>(i) + 0.5f) / static_cast<float>(kSamples);
                                const float u2 = static_cast<float>(bits) * 2.3283064365386963e-10f;

                                const float phi      = 2.0f * 3.14159265f * u1;
                                const float cosTheta = std::sqrt((1.0f - u2) / (1.0f + (a * a - 1.0f) * u2));
                                const float sinTheta = std::sqrt(std::max(1.0f - cosTheta * cosTheta, 0.0f));
                                const glm::vec3 h = tx * (sinTheta * std::cos(phi)) + ty * (sinTheta * std::sin(phi)) + n * cosTheta;
                                const glm::vec3 l = 2.0f * glm::dot(n, h) * h - n;
                                const float nDotL = glm::dot(n, l);
                                if (nDotL <= 0.0f) { continue; }
                                sum    += lookup(level, l) * nDotL;
                                weight += nDotL;
                            }
                            colour = weight > 0.0f ? sum / weight : lookup(level, n);
                        }
                        // Half floats top out near 65504: keep an HDR sun from becoming infinity.
                        colours[(static_cast<size_t>(face) * s + y) * s + x] = glm::min(colour, glm::vec3(60000.0f));
                    }
                }
            }
        }
        return out;

    }

}
