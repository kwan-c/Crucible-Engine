#pragma once

#include "VulkanContext.h"

namespace Crucible {

    class VulkanBuffer {

    public:
        void Create(VulkanContext& context, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);
        void Upload(VulkanContext& context, void const* data, vk::DeviceSize size);
        void UploadStaging(VulkanContext& context, void const* data, vk::DeviceSize size, vk::raii::CommandPool& commandPool);

        vk::raii::Buffer const& GetBuffer() const;
        vk::raii::DeviceMemory& GetMemory();

    private:
        uint32_t FindMemoryType(VulkanContext& context, uint32_t typeFilter, vk::MemoryPropertyFlags properties);

    private:
        vk::raii::Buffer        m_buffer = nullptr;
        vk::raii::DeviceMemory  m_memory = nullptr;

    };  // VULKAN BUFFER CLASS

}   // NAMESPACE CRUCIBLE
