/*************************************************************************
  \file       EnvironmentMaps.cpp
  \brief
    See EnvironmentMaps.h.
*************************************************************************/
#include "Graphics/EnvironmentMaps.h"
#include "Graphics/EnvironmentBake.h"
#include "Graphics/Vulkan/VulkanFrameManager.h"   // MAX_FRAMES_IN_FLIGHT (ReleaseDestroyed)
#include "Resources/CompiledSky.h"                // CreateCubemapCompiled

#include <glm/gtc/packing.hpp>   // packHalf4x16 - RGBA16F cubemap texels

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Crucible {

    namespace {

        // Shared with the texture compiler, which bakes .sky files offline (EnvironmentBake.h).
        using EnvironmentBake::CubeFaceTexelDirection;
        using EnvironmentBake::DirectionToCubeTexel;

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

        CheckCubemapLimit("UploadCubemapFaces");

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

        return RegisterCubemap(std::move(cube));

    }   // EnvironmentMaps::UploadCubemapFaces()

    void EnvironmentMaps::CheckCubemapLimit(const char* caller) const {

        // The limit is the descriptor pool's: one set per cubemap still alive (or destroyed but
        // not yet released). Destroyed slots no longer hold one.
        const size_t live = std::count_if(m_cubemaps.begin(), m_cubemaps.end(), [](const GpuCubemap& c) { return !c.destroyed; })
                          + m_retiredCubemaps.size();
        if (live >= kMaxCubemaps) {
            throw std::runtime_error(std::string("[Renderer] ") + caller + ": kMaxCubemaps exceeded; raise the limit if you genuinely need more.");
        }

    }   // EnvironmentMaps::CheckCubemapLimit()

    CubemapHandle EnvironmentMaps::RegisterCubemap(GpuCubemap cube) {

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

    }   // EnvironmentMaps::RegisterCubemap()

    CubemapHandle EnvironmentMaps::CreateCubemapCompiled(const CompiledSky& sky, const std::string& debugName) {

        CheckCubemapLimit("CreateCubemapCompiled");
        constexpr vk::Format format = vk::Format::eBc6HUfloatBlock;
        const vk::FormatProperties support = m_context.GetPhysicalDevice().getFormatProperties(format);
        if (!(support.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImage)) {
            throw std::runtime_error("[Renderer] This GPU cannot use BC6H textures, so it cannot show compiled skyboxes");
        }
        if (sky.sky.format != CompiledTextureFormat::BC6H || sky.environment.format != CompiledTextureFormat::BC6H) {
            throw std::runtime_error("[Renderer] CreateCubemapCompiled: expected BC6H cubes");
        }

        // One cube image from a compiled cube: every mip level holds its six faces back to back,
        // exactly how a multi-layer copy reads them.
        const auto upload = [&](const CompiledTexture& source, VulkanImage& image, vk::raii::ImageView& view) {
            const uint32_t levels = static_cast<uint32_t>(source.mips.size());
            image.Create(m_context, source.width, source.height, format,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                /*arrayLayers*/ 6, vk::ImageCreateFlagBits::eCubeCompatible, levels);
            for (uint32_t level = 0; level < levels; ++level) {
                const CompiledTextureMip& mip = source.mips[level];
                m_uploads.QueueImage(*image.GetImage(), source.data.data() + mip.offset, mip.size, mip.width, mip.height, 6, level);
            }
            vk::ImageViewCreateInfo viewInfo{
                .image            = image.GetImage(),
                .viewType         = vk::ImageViewType::eCube,
                .format           = format,
                .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = levels, .layerCount = 6 }
            };
            view = vk::raii::ImageView(m_context.GetDevice(), viewInfo);
        };

        // Everything BuildEnvironmentLighting would work out was baked by the texture compiler.
        GpuCubemap cube;
        cube.faceSize    = sky.sky.width;
        cube.debugName   = debugName;
        upload(sky.sky, cube.image, cube.view);
        upload(sky.environment, cube.envImage, cube.envView);
        cube.envMipCount = static_cast<uint32_t>(sky.environment.mips.size());
        for (size_t i = 0; i < 9; ++i) { cube.sh[i] = glm::vec4(sky.sh[i][0], sky.sh[i][1], sky.sh[i][2], 0.0f); }

        return RegisterCubemap(std::move(cube));

    }   // EnvironmentMaps::CreateCubemapCompiled()

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

        // 2. Shrink, blur for reflections and project onto spherical harmonics - the same code
        //    the texture compiler bakes .sky files with (EnvironmentBake.h).
        const EnvironmentBake::Lighting lighting = EnvironmentBake::BakeLighting(source, faceSize);
        for (int i = 0; i < 9; ++i) { cube.sh[i] = glm::vec4(lighting.sh[i], 0.0f); }

        // 3. Upload the reflection levels.
        const uint32_t baseSize = lighting.sizes.front();
        cube.envMipCount = static_cast<uint32_t>(lighting.sizes.size());
        cube.envImage.Create(m_context, baseSize, baseSize, vk::Format::eR16G16B16A16Sfloat,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal,
            /*arrayLayers*/ 6, vk::ImageCreateFlagBits::eCubeCompatible, cube.envMipCount);

        std::vector<uint64_t> packed;
        for (size_t level = 0; level < lighting.sizes.size(); ++level) {
            const uint32_t s = lighting.sizes[level];
            packed.resize(lighting.levels[level].size());
            for (size_t i = 0; i < packed.size(); ++i) { packed[i] = PackHalfTexel(glm::vec4(lighting.levels[level][i], 1.0f)); }
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

    CubemapHandle EnvironmentMaps::CreateCubemapFromEquirect(const float* rgba, uint32_t width, uint32_t height,
                                                            uint32_t faceSize, const std::string& name) {

        faceSize = std::max(faceSize, 1u);
        const std::string cacheKey = "equirect:" + name + "@" + std::to_string(faceSize);
        if (auto it = m_cubemapCacheByKey.find(cacheKey); it != m_cubemapCacheByKey.end()) {
            return CubemapHandle{ it->second };
        }
        if (!rgba || width == 0 || height == 0) {
            throw std::runtime_error("[Renderer] CreateCubemapFromEquirect: no pixels (" + name + ")");
        }

        // Six cube faces of half-float texels (EnvironmentBake, shared with the texture compiler).
        const std::vector<glm::vec3> faces = EnvironmentBake::EquirectToCube(rgba, static_cast<int>(width), static_cast<int>(height), faceSize);
        std::vector<uint64_t> texels(faces.size());
        for (size_t i = 0; i < faces.size(); ++i) { texels[i] = PackHalfTexel(glm::vec4(faces[i], 1.0f)); }

        const CubemapHandle handle = UploadCubemapFaces(texels.data(), faceSize, vk::Format::eR16G16B16A16Sfloat, 8, name);

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
