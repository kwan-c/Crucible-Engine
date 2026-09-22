#pragma once

#include "VulkanContext.h"

namespace Crucible {

    class VulkanResource {

    public:
        vk::raii::DeviceMemory& GetMemory();

    protected:
        uint32_t FindMemoryType(VulkanContext& context, uint32_t typeFilter, vk::MemoryPropertyFlags properties);

    protected:
        vk::raii::DeviceMemory  m_memory = nullptr;

    };  // VULKAN RESOURCE CLASS

    class VulkanBuffer : public VulkanResource {

    public:
        void Create(VulkanContext& context, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);
        void Upload(VulkanContext& context, void const* data, vk::DeviceSize size);
        void UploadStaging(VulkanContext& context, void const* data, vk::DeviceSize size, vk::raii::CommandPool& commandPool);

        const vk::raii::Buffer& GetBuffer() const;

    private:
        vk::raii::Buffer        m_buffer = nullptr;

    };  // VULKAN BUFFER CLASS

    class VulkanImage : public VulkanResource {

    public:
        void Create(VulkanContext& context, uint32_t w, uint32_t h, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties);

        const vk::raii::Image&  GetImage() const;
        vk::raii::Image&        GetImage();

    private:
        vk::raii::Image         m_image = nullptr;

    };  // VULKAN IMAGE CLASS

}   // NAMESPACE CRUCIBLE