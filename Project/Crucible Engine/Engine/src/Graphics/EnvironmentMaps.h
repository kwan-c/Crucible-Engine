/*************************************************************************
  \file       EnvironmentMaps.h
  \brief
    The cubemaps used as skies, and the lighting data built from each one:
    a blurred copy for reflections and nine spherical-harmonic numbers for
    diffuse light (image-based lighting).
  \details
    Moved out of the Renderer class. Game and editor code still uses the
    Renderer's CreateCubemap / CreateCubemapFromEquirect / GetDefaultSkybox,
    which forward here.
*************************************************************************/
#pragma once

#include "Vulkan/VulkanContext.h"
#include "Vulkan/VulkanResource.h"
#include "Vulkan/VulkanUploadQueue.h"
#include "CubemapHandle.h"

#include <glm/glm.hpp>

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace Crucible {

    // Direction TOWARDS the sun of the built-in sky, and of the light used when a scene has no
    // lights of its own. Shared so the painted sun and the light it casts agree.
    inline const glm::vec3 kFallbackSunTowards = glm::normalize(glm::vec3(0.2f, 0.8f, 0.5f));

    // GPU-side cubemap (skybox): a 6-layer cube-compatible image, its cube view, and the one
    // descriptor set (skybox pipeline's set = 1) it's bound into - allocated once at creation,
    // same as a material's, since a cubemap's contents never change after upload.
    struct GpuCubemap {
        VulkanImage              image;
        vk::raii::ImageView      view = nullptr;
        vk::raii::DescriptorSet  descriptorSet = nullptr;
        uint32_t                 faceSize = 0;
        std::string              debugName;
        // The same sky prepared as a light source (BuildEnvironmentLighting): a small cube
        // map whose every mip level is blurrier than the last, for reflections by roughness,
        // and nine spherical-harmonic coefficients for diffuse light.
        VulkanImage              envImage;
        vk::raii::ImageView      envView = nullptr;
        uint32_t                 envMipCount = 1;
        std::array<glm::vec4, 9> sh{};
        bool                     destroyed = false;   // DestroyCubemap: the slot stays (never reused) but is empty
    };

    /// The diffuse light a surface facing `normal` (unit length) gets from a sky, from its nine
    /// spherical-harmonic numbers (GpuCubemap::sh): the same polynomial as SkyIrradiance in
    /// DefaultShader.slang, so values read on the CPU match what the shader lights with.
    /// Linear RGB, never negative.
    glm::vec3 EvaluateSkyIrradiance(const std::array<glm::vec4, 9>& sh, const glm::vec3& normal);

    class EnvironmentMaps {
    public:
        /*********************************************************************
         \param[in] context  Used to create images. Must outlive this object.
         \param[in] uploads  The queue that copies pixels to the GPU at the
                            start of a frame. Must outlive this object.
         *********************************************************************/
        EnvironmentMaps(VulkanContext& context, VulkanUploadQueue& uploads) : m_context(context), m_uploads(uploads) {}

        EnvironmentMaps(const EnvironmentMaps&) = delete;
        EnvironmentMaps& operator=(const EnvironmentMaps&) = delete;

        /*********************************************************************
         \brief  Creates the samplers, the descriptor pool and the built-in
                 sky. Call once, before any cubemap is made.
         \param[in] skySetLayout  Layout of the sky pipeline's set 1 (one cubemap).
         *********************************************************************/
        void Init(vk::DescriptorSetLayout skySetLayout);

        // See the Renderer functions of the same names for the full notes.
        CubemapHandle CreateCubemap(const std::array<std::string, 6>& facePaths, bool isSRGB);
        CubemapHandle CreateCubemapFromEquirect(const std::string& path, uint32_t faceSize);

        CubemapHandle DefaultSkybox() const { return m_defaultSkybox; }   ///< The built-in procedural sky.

        /// Every cubemap, indexed by CubemapHandle::id.
        const std::vector<GpuCubemap>& Cubemaps() const { return m_cubemaps; }
        /// Whether a cubemap can be drawn with: valid, ours, and not destroyed.
        bool IsUsable(CubemapHandle handle) const {
            return handle.IsValid() && handle.id < m_cubemaps.size() && !m_cubemaps[handle.id].destroyed;
        }
        /// The cubemap behind `wanted`, or the built-in sky when that handle is not one of ours
        /// (or was destroyed).
        const GpuCubemap& Resolve(CubemapHandle wanted) const {
            if (IsUsable(wanted)) { return m_cubemaps[wanted.id]; }
            return m_cubemaps[m_defaultSkybox.id];
        }

        /*********************************************************************
         \brief  Frees a skybox (its cubemap, environment map and descriptor
                 set). The slot is emptied at once and never reused, so a
                 handle still held anywhere shows the built-in sky; the
                 Vulkan objects go in ReleaseDestroyed() once no frame in
                 flight can use them.
         \return false for the built-in sky, an unknown handle, or one
                 already destroyed.
         *********************************************************************/
        bool DestroyCubemap(CubemapHandle handle);

        /// Frees destroyed cubemaps no frame in flight can still use. Once a frame, after the fence wait.
        void ReleaseDestroyed();
        /// Sampler for GpuCubemap::envView: blends between its mip levels.
        const vk::raii::Sampler& EnvSampler() const { return m_envSampler; }

    private:
        void CreateSkyboxSampler();
        void CreateCubemapDescriptorPool();
        void CreateDefaultSkybox();
        // Shared core of every cubemap-creation path: uploads six tightly-packed faces (+X, -X,
        // +Y, -Y, +Z, -Z, each faceSize^2 texels of bytesPerTexel) to a new cube image, creates
        // its cube view and descriptor set, and returns the new handle.
        CubemapHandle UploadCubemapFaces(const void* faceData, uint32_t faceSize, vk::Format format,
                                         uint32_t bytesPerTexel, const std::string& debugName);
        /// Builds a cubemap's envImage and sh from its faces (called by UploadCubemapFaces).
        void BuildEnvironmentLighting(GpuCubemap& cube, const void* faceData, uint32_t faceSize, vk::Format format);

        VulkanContext&     m_context;
        VulkanUploadQueue& m_uploads;
        vk::DescriptorSetLayout m_skySetLayout;   // set by Init()

        // Cubemaps (skyboxes). Pool BEFORE the cubemaps whose descriptor sets it allocates -
        // same destruction-order rule as the material pools in RenderResources.
        static constexpr uint32_t                   kMaxCubemaps = 64;
        vk::raii::DescriptorPool                    m_cubemapDescriptorPool = nullptr;
        vk::raii::Sampler                           m_skyboxSampler = nullptr;
        vk::raii::Sampler                           m_envSampler = nullptr;     // environment maps: blends between mip levels
        std::vector<GpuCubemap>                     m_cubemaps;
        std::unordered_map<std::string, uint32_t>   m_cubemapCacheByKey;   // joined paths / path+size -> CubemapHandle::id
        CubemapHandle                               m_defaultSkybox;

        // Destroyed cubemaps waiting for the GPU to finish with them, with the ReleaseDestroyed()
        // count at which each was destroyed. Declared after the pool, so they are freed before it.
        struct RetiredCubemap { uint64_t frame = 0; GpuCubemap cubemap; };
        std::vector<RetiredCubemap>                 m_retiredCubemaps;
        uint64_t                                    m_releaseCount = 0;
    };

}   // NAMESPACE CRUCIBLE
