/*************************************************************************
  \file       EnvironmentMaps.cpp
  \brief
    See EnvironmentMaps.h.
*************************************************************************/
#include "Graphics/EnvironmentMaps.h"
#include "Graphics/Vulkan/VulkanFrameManager.h"   // MAX_FRAMES_IN_FLIGHT (ReleaseDestroyed)

#include <glm/gtc/packing.hpp>   // packHalf4x16 - RGBA16F cubemap texels

#include <stb_image.h>   // the implementation is compiled in RenderSystem.cpp

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>       // unique_ptr (stb_image pixel ownership in CreateCubemap)
#include <stdexcept>

namespace Crucible {

    namespace {

        // The reverse of CubeFaceTexelDirection below: which face, and which texel of it, a
        // direction points at (same face selection table).
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

        // World-space direction through the centre of texel (x, y) of cube face `face`, using the
        // Vulkan/OpenGL cube-map face selection table (face order +X, -X, +Y, -Y, +Z, -Z; row 0 =
        // t near 0). Inverse of what the sampler does, so procedural/equirect faces generated with
        // this line up with SamplerCube lookups by construction.
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

        // RGBA float -> one RGBA16F texel. Clamped to half's finite range (an HDRI's sun can
        // exceed 65504) and NaN-scrubbed, so one bad source pixel can't poison the sky.
        uint64_t PackHalfTexel(glm::vec4 c) {
            for (int i = 0; i < 4; ++i) {
                if (!(c[i] == c[i])) { c[i] = 0.0f; }   // NaN
                c[i] = glm::clamp(c[i], 0.0f, 65504.0f);
            }
            return glm::packHalf4x16(c);
        }

        // The built-in sky's colour for a world direction: zenith-to-horizon gradient above,
        // darker ground below, and a soft glow + small disc around the fallback sun.
        glm::vec3 ProceduralSkyColour(const glm::vec3& d) {

            const glm::vec3 zenith  { 0.10f, 0.24f, 0.55f };
            const glm::vec3 horizon { 0.62f, 0.74f, 0.90f };
            const glm::vec3 ground  { 0.16f, 0.15f, 0.14f };

            glm::vec3 colour;
            if (d.y >= 0.0f) {
                colour = glm::mix(horizon, zenith, std::pow(d.y, 0.5f));
            } else {
                colour = glm::mix(horizon * 0.8f, ground, std::pow(std::min(-d.y * 4.0f, 1.0f), 0.6f));
            }

            const float s = std::max(glm::dot(d, kFallbackSunTowards), 0.0f);
            colour += glm::vec3(1.0f, 0.85f, 0.6f) * (std::pow(s, 64.0f) * 0.5f + std::pow(s, 1500.0f) * 6.0f);
            return colour;
        }

    }   // anonymous namespace

    void EnvironmentMaps::Init(vk::DescriptorSetLayout skySetLayout) {

        m_skySetLayout = skySetLayout;
        CreateSkyboxSampler();
        CreateCubemapDescriptorPool();
        CreateDefaultSkybox();

    }   // EnvironmentMaps::Init()

    void EnvironmentMaps::CreateSkyboxSampler() {

        // Clamp-to-edge so bilinear filtering at a face's border never wraps around to that
        // face's opposite edge (seamless cube filtering across faces is the default in Vulkan).
        vk::SamplerCreateInfo samplerInfo{
            .magFilter               = vk::Filter::eLinear,
            .minFilter               = vk::Filter::eLinear,
            .mipmapMode              = vk::SamplerMipmapMode::eLinear,
            .addressModeU            = vk::SamplerAddressMode::eClampToEdge,
            .addressModeV            = vk::SamplerAddressMode::eClampToEdge,
            .addressModeW            = vk::SamplerAddressMode::eClampToEdge,
            .anisotropyEnable        = vk::False,
            .compareEnable           = vk::False,
            .minLod                  = 0.0f,
            .maxLod                  = 0.0f,
            .borderColor             = vk::BorderColor::eIntOpaqueBlack,
            .unnormalizedCoordinates = vk::False
        };
        m_skyboxSampler = vk::raii::Sampler(m_context.GetDevice(), samplerInfo);

        // Environment maps are read at a mip level chosen by roughness, blending between levels.
        samplerInfo.maxLod = vk::LodClampNone;
        m_envSampler = vk::raii::Sampler(m_context.GetDevice(), samplerInfo);

    }   // EnvironmentMaps::CreateSkyboxSampler()

    void EnvironmentMaps::CreateCubemapDescriptorPool() {

        // eFreeDescriptorSet is required, not optional: each GpuCubemap's vk::raii::DescriptorSet
        // frees itself back to this pool in its destructor.
        vk::DescriptorPoolSize poolSize{
            .type            = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = kMaxCubemaps };
        vk::DescriptorPoolCreateInfo poolInfo{
            .flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets       = kMaxCubemaps,
            .poolSizeCount = 1,
            .pPoolSizes    = &poolSize };
        m_cubemapDescriptorPool = vk::raii::DescriptorPool(m_context.GetDevice(), poolInfo);

    }   // EnvironmentMaps::CreateCubemapDescriptorPool()

    CubemapHandle EnvironmentMaps::UploadCubemapFaces(const void* faceData, uint32_t faceSize, vk::Format format,
                                               uint32_t bytesPerTexel, const std::string& debugName) {

        // The limit is the descriptor pool's: one set per cubemap still alive (or destroyed but
        // not yet released). Destroyed slots no longer hold one.
        const size_t live = std::count_if(m_cubemaps.begin(), m_cubemaps.end(), [](const GpuCubemap& c) { return !c.destroyed; })
                          + m_retiredCubemaps.size();
        if (live >= kMaxCubemaps) {
            throw std::runtime_error("[Renderer] UploadCubemapFaces: kMaxCubemaps exceeded; raise the limit if you genuinely need more.");
        }

        const vk::DeviceSize faceBytes  = vk::DeviceSize{ faceSize } * faceSize * bytesPerTexel;
        const vk::DeviceSize totalBytes = faceBytes * 6;

        GpuCubemap cube;
        cube.faceSize  = faceSize;
        cube.debugName = debugName;
        cube.image.Create(m_context, faceSize, faceSize, format,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            /*arrayLayers*/ 6, vk::ImageCreateFlagBits::eCubeCompatible);

        // Six faces, tightly packed one after another; see UploadTexturePixels for when the
        // GPU-side copy actually runs.
        m_uploads.QueueImage(*cube.image.GetImage(), faceData, totalBytes, faceSize, faceSize, 6);

        // The same sky, prepared as a light source for image-based lighting.
        BuildEnvironmentLighting(cube, faceData, faceSize, format);

        vk::ImageViewCreateInfo viewInfo{
            .image            = cube.image.GetImage(),
            .viewType         = vk::ImageViewType::eCube,
            .format           = format,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 6 }
        };
        cube.view = vk::raii::ImageView(m_context.GetDevice(), viewInfo);

        vk::DescriptorSetLayout layout = m_skySetLayout;
        vk::DescriptorSetAllocateInfo allocInfo{
            .descriptorPool     = m_cubemapDescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &layout
        };
        std::vector<vk::raii::DescriptorSet> sets = m_context.GetDevice().allocateDescriptorSets(allocInfo);
        cube.descriptorSet = std::move(sets.front());

        vk::DescriptorImageInfo imageInfo{
            .sampler     = *m_skyboxSampler,
            .imageView   = *cube.view,
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        vk::WriteDescriptorSet write{
            .dstSet          = *cube.descriptorSet,
            .dstBinding      = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eCombinedImageSampler,
            .pImageInfo      = &imageInfo };
        m_context.GetDevice().updateDescriptorSets(write, {});

        const CubemapHandle handle{ static_cast<uint32_t>(m_cubemaps.size()) };
        m_cubemaps.push_back(std::move(cube));
        return handle;

    }   // EnvironmentMaps::UploadCubemapFaces()

    void EnvironmentMaps::BuildEnvironmentLighting(GpuCubemap& cube, const void* faceData, uint32_t faceSize, vk::Format format) {

        // Done once per cubemap, on the CPU, at a small size (the result is blurry by nature):
        //   - an "environment map": level 0 is the sky shrunk to at most 128x128 per face, and
        //     each smaller level is that sky blurred as a rougher surface would reflect it;
        //   - nine spherical-harmonic coefficients describing the sky's overall diffuse light.

        // 1. Decode the faces to linear floating-point colour.
        const size_t facePixels = static_cast<size_t>(faceSize) * faceSize;
        std::vector<glm::vec3> source(facePixels * 6);
        if (format == vk::Format::eR16G16B16A16Sfloat) {
            const uint16_t* p = static_cast<const uint16_t*>(faceData);
            for (size_t i = 0; i < source.size(); ++i) {
                source[i] = glm::vec3(glm::unpackHalf1x16(p[i * 4]), glm::unpackHalf1x16(p[i * 4 + 1]), glm::unpackHalf1x16(p[i * 4 + 2]));
            }
        } else {
            const bool srgb = format == vk::Format::eR8G8B8A8Srgb;
            const unsigned char* p = static_cast<const unsigned char*>(faceData);
            for (size_t i = 0; i < source.size(); ++i) {
                glm::vec3 c(p[i * 4] / 255.0f, p[i * 4 + 1] / 255.0f, p[i * 4 + 2] / 255.0f);
                if (srgb) { c = glm::pow(c, glm::vec3(2.2f)); }
                source[i] = c;
            }
        }

        // 2. A chain of plain shrunk copies: base size (power of two, at most 128) down to 16.
        //    Not smaller: the GPU stretches the smallest level over a whole hemisphere of
        //    reflection directions, and a 4x4 image stretched that far shows its texels as flat
        //    patches on round objects.
        uint32_t baseSize = 4;
        while (baseSize * 2 <= std::min(faceSize, 128u)) { baseSize *= 2; }
        std::vector<uint32_t>               sizes;
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

        // 3. Diffuse light: project the sky onto nine spherical harmonics (from a small level -
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
            for (int i = 0; i < 9; ++i) { cube.sh[i] = glm::vec4(sum[i] * norm * scale[i], 0.0f); }
        }

        // 4. Reflections: level 0 is the shrunk sky as is (a mirror); each further level is what a
        //    rougher surface would reflect - the average of the sky over the spread of directions
        //    such a surface scatters into (GGX importance sampling, 64 directions per texel, read
        //    from the shrunk copy of the same size so bright spots do not turn into speckles).
        cube.envMipCount = static_cast<uint32_t>(sizes.size());
        cube.envImage.Create(m_context, baseSize, baseSize, vk::Format::eR16G16B16A16Sfloat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            /*arrayLayers*/ 6, vk::ImageCreateFlagBits::eCubeCompatible, cube.envMipCount);

        constexpr uint32_t kSamples = 64;
        std::vector<uint64_t> packed;
        for (size_t level = 0; level < sizes.size(); ++level) {
            const uint32_t s = sizes[level];
            packed.resize(static_cast<size_t>(s) * s * 6);
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
                        colour = glm::min(colour, glm::vec3(60000.0f));
                        packed[(static_cast<size_t>(face) * s + y) * s + x] = PackHalfTexel(glm::vec4(colour, 1.0f));
                    }
                }
            }
            m_uploads.QueueImage(*cube.envImage.GetImage(), packed.data(), vk::DeviceSize{ packed.size() } * 8, s, s, 6,
                                 static_cast<uint32_t>(level));
        }

        vk::ImageViewCreateInfo viewInfo{
            .image            = cube.envImage.GetImage(),
            .viewType         = vk::ImageViewType::eCube,
            .format           = vk::Format::eR16G16B16A16Sfloat,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = cube.envMipCount, .layerCount = 6 }
        };
        cube.envView = vk::raii::ImageView(m_context.GetDevice(), viewInfo);

    }   // EnvironmentMaps::BuildEnvironmentLighting()

    void EnvironmentMaps::CreateDefaultSkybox() {

        // Small is fine: the procedural sky is a smooth gradient, and linear filtering hides the
        // texel size everywhere except the (deliberately soft) sun glow.
        constexpr uint32_t kSize = 128;
        std::vector<uint64_t> texels(static_cast<size_t>(kSize) * kSize * 6);

        size_t i = 0;
        for (uint32_t face = 0; face < 6; ++face) {
            for (uint32_t y = 0; y < kSize; ++y) {
                for (uint32_t x = 0; x < kSize; ++x) {
                    const glm::vec3 dir = CubeFaceTexelDirection(face, x, y, kSize);
                    texels[i++] = PackHalfTexel(glm::vec4(ProceduralSkyColour(dir), 1.0f));
                }
            }
        }

        m_defaultSkybox = UploadCubemapFaces(texels.data(), kSize, vk::Format::eR16G16B16A16Sfloat, 8, "Default Sky");

    }   // EnvironmentMaps::CreateDefaultSkybox()

    CubemapHandle EnvironmentMaps::CreateCubemap(const std::array<std::string, 6>& facePaths, bool isSRGB) {

        std::string cacheKey = isSRGB ? "faces[srgb]:" : "faces[unorm]:";
        for (const std::string& path : facePaths) { cacheKey += path + "|"; }
        if (auto it = m_cubemapCacheByKey.find(cacheKey); it != m_cubemapCacheByKey.end()) {
            return CubemapHandle{ it->second };
        }

        using StbPixels = std::unique_ptr<stbi_uc, void(*)(void*)>;
        std::vector<StbPixels> faces;
        faces.reserve(6);
        int faceSize = 0;

        for (size_t f = 0; f < 6; ++f) {
            int w{}, h{}, channels{};
            stbi_uc* pixels = stbi_load(facePaths[f].c_str(), &w, &h, &channels, STBI_rgb_alpha);
            if (!pixels) {
                throw std::runtime_error("[Renderer] CreateCubemap: failed to load face image (" + facePaths[f] + ")");
            }
            faces.emplace_back(pixels, stbi_image_free);

            if (w != h) {
                throw std::runtime_error("[Renderer] CreateCubemap: face is not square (" + facePaths[f] + ")");
            }
            if (f == 0) { faceSize = w; }
            else if (w != faceSize) {
                throw std::runtime_error("[Renderer] CreateCubemap: face size differs from face 0 (" + facePaths[f] + ")");
            }
        }

        const size_t faceBytes = static_cast<size_t>(faceSize) * faceSize * 4;
        std::vector<unsigned char> packed(faceBytes * 6);
        for (size_t f = 0; f < 6; ++f) {
            memcpy(packed.data() + f * faceBytes, faces[f].get(), faceBytes);
        }

        const vk::Format format = isSRGB ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
        const CubemapHandle handle = UploadCubemapFaces(packed.data(), static_cast<uint32_t>(faceSize), format, 4,
            std::filesystem::path(facePaths[0]).parent_path().filename().string());

        m_cubemapCacheByKey.emplace(cacheKey, handle.id);
        return handle;

    }   // EnvironmentMaps::CreateCubemap()

    CubemapHandle EnvironmentMaps::CreateCubemapFromEquirect(const std::string& path, uint32_t faceSize) {

        faceSize = std::max(faceSize, 1u);
        const std::string cacheKey = "equirect:" + path + "@" + std::to_string(faceSize);
        if (auto it = m_cubemapCacheByKey.find(cacheKey); it != m_cubemapCacheByKey.end()) {
            return CubemapHandle{ it->second };
        }

        // stbi_loadf returns linear floats for every format: .hdr as-is (true HDR), LDR images
        // converted through stb's default 2.2 gamma - so one path serves both.
        int w{}, h{}, channels{};
        float* pixels = stbi_loadf(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
        if (!pixels) {
            throw std::runtime_error("[Renderer] CreateCubemapFromEquirect: failed to load (" + path + ")");
        }
        std::unique_ptr<float, void(*)(void*)> owner(pixels, stbi_image_free);

        // Bilinear lookup, wrapping horizontally (longitude is periodic) and clamping vertically.
        auto fetch = [&](int x, int y) -> glm::vec4 {
            x = ((x % w) + w) % w;
            y = std::clamp(y, 0, h - 1);
            const float* p = pixels + (static_cast<size_t>(y) * w + x) * 4;
            return glm::vec4(p[0], p[1], p[2], 1.0f);
        };
        auto sample = [&](float u, float v) -> glm::vec4 {
            const float fx = u * static_cast<float>(w) - 0.5f;
            const float fy = v * static_cast<float>(h) - 0.5f;
            const int   x0 = static_cast<int>(std::floor(fx));
            const int   y0 = static_cast<int>(std::floor(fy));
            const float tx = fx - static_cast<float>(x0);
            const float ty = fy - static_cast<float>(y0);
            const glm::vec4 top    = glm::mix(fetch(x0, y0),     fetch(x0 + 1, y0),     tx);
            const glm::vec4 bottom = glm::mix(fetch(x0, y0 + 1), fetch(x0 + 1, y0 + 1), tx);
            return glm::mix(top, bottom, ty);
        };

        constexpr float kInvTwoPi = 0.15915494309f;
        constexpr float kInvPi    = 0.31830988618f;

        std::vector<uint64_t> texels(static_cast<size_t>(faceSize) * faceSize * 6);
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
                    texels[i++] = PackHalfTexel(sample(u, v));
                }
            }
        }

        const CubemapHandle handle = UploadCubemapFaces(texels.data(), faceSize, vk::Format::eR16G16B16A16Sfloat, 8,
            std::filesystem::path(path).stem().string());

        m_cubemapCacheByKey.emplace(cacheKey, handle.id);
        return handle;

    }   // EnvironmentMaps::CreateCubemapFromEquirect()

    bool EnvironmentMaps::DestroyCubemap(CubemapHandle handle) {

        if (!IsUsable(handle) || handle == m_defaultSkybox) { return false; }

        // Forget its cache key, so loading the same file again makes a new cubemap.
        std::erase_if(m_cubemapCacheByKey, [&](const auto& entry) { return entry.second == handle.id; });

        GpuCubemap& cube = m_cubemaps[handle.id];
        m_retiredCubemaps.push_back({ m_releaseCount, std::move(cube) });
        cube = GpuCubemap{};
        cube.destroyed = true;   // IsUsable false: Resolve and the sky pass use the built-in sky instead
        return true;

    }   // EnvironmentMaps::DestroyCubemap()

    void EnvironmentMaps::ReleaseDestroyed() {

        // Same rule as RenderResources::ReleaseDestroyed: every frame that could still sample
        // it has finished after MAX_FRAMES_IN_FLIGHT more fence waits (plus one frame of margin).
        ++m_releaseCount;
        std::erase_if(m_retiredCubemaps, [&](const RetiredCubemap& retired) {
            return m_releaseCount - retired.frame > VulkanFrameManager::MAX_FRAMES_IN_FLIGHT;
        });

    }   // EnvironmentMaps::ReleaseDestroyed()

    glm::vec3 EvaluateSkyIrradiance(const std::array<glm::vec4, 9>& sh, const glm::vec3& n) {

        // Must stay the same as SkyIrradiance in DefaultShader.slang (and the coefficient order
        // BuildEnvironmentLighting writes).
        const glm::vec3 e = glm::vec3(sh[0])
                          + glm::vec3(sh[1]) * n.y + glm::vec3(sh[2]) * n.z + glm::vec3(sh[3]) * n.x
                          + glm::vec3(sh[4]) * (n.x * n.y) + glm::vec3(sh[5]) * (n.y * n.z)
                          + glm::vec3(sh[6]) * (3.0f * n.z * n.z - 1.0f)
                          + glm::vec3(sh[7]) * (n.x * n.z) + glm::vec3(sh[8]) * (n.x * n.x - n.y * n.y);
        return glm::max(e, glm::vec3(0.0f));

    }   // EvaluateSkyIrradiance()

}   // NAMESPACE CRUCIBLE
