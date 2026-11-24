/*************************************************************************
  \file       PipelineCache.h
  \brief
    The device's VkPipelineCache, kept on disk between runs. Building a
    pipeline means the driver compiling its shaders for this GPU; with a
    cache the driver keeps the result, so the same pipeline is close to
    free the next time - at the next start, or when a shader is reloaded
    back to something it has been before.
  \details
    One cache for the whole renderer, owned by VulkanContext. Every pass
    hands it to its pipeline constructors (context.GetPipelineCache()).

    The file is only valid for the GPU and driver version that wrote it:
    Load() checks the header (vendor, device, the driver's cache UUID) and
    starts with an empty cache when it does not match, which is what
    happens after a driver update. A missing or unreadable file is not an
    error either - the cache just starts empty.

    Vulkan synchronizes the cache internally, so pipelines may be created
    with it from several threads at once.
*************************************************************************/
#pragma once

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
    #include <vulkan/vulkan_raii.hpp>
#else
    import vulkan_hpp;
#endif

#include <cstddef>
#include <filesystem>

namespace Crucible {

    class PipelineCache {
    public:
        /*********************************************************************
         \brief  Creates the cache, starting from `file` if it holds a cache
                 this GPU and driver wrote. Never throws for a bad file.
         \param[in] gpu     The physical device (the file must match it).
         \param[in] device  The device to create the cache on.
         \param[in] file    Where the cache is kept. Empty = never saved.
         *********************************************************************/
        void Load(const vk::raii::PhysicalDevice& gpu, const vk::raii::Device& device, const std::filesystem::path& file);

        /*********************************************************************
         \brief  Writes the cache to its file if it has changed size since it
                 was loaded or last saved. Never throws: a cache that cannot
                 be written is only a slower next start.
         *********************************************************************/
        void Save();

        /// The cache, to pass to vk::raii::Pipeline's constructor. Null before Load().
        const vk::raii::PipelineCache& Get() const { return m_cache; }

    private:
        vk::raii::PipelineCache m_cache = nullptr;
        std::filesystem::path   m_file;
        size_t                  m_savedSize = 0;   // bytes loaded from, or last written to, m_file
    };

}   // NAMESPACE CRUCIBLE
