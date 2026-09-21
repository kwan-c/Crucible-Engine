#include "VulkanBuffer.h"

namespace Crucible {

    void VulkanBuffer::Create(VulkanContext& context, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties) {
        
        vk::BufferCreateInfo bufferInfo{ 
            .size = size, 
            .usage = usage,
            .sharingMode = vk::SharingMode::eExclusive
        };
        m_buffer = vk::raii::Buffer(context.GetDevice(), bufferInfo);

        vk::MemoryRequirements memRequirements = m_buffer.getMemoryRequirements();
        vk::MemoryAllocateInfo memoryAllocInfo = {
            .allocationSize = memRequirements.size,
            .memoryTypeIndex = FindMemoryType(context, memRequirements.memoryTypeBits, properties)
        };

        m_memory = vk::raii::DeviceMemory(context.GetDevice(), memoryAllocInfo);
        m_buffer.bindMemory(*m_memory, 0);

    }   // VulkanBuffer::Create()

    void VulkanBuffer::Upload(VulkanContext& context, const void* data, vk::DeviceSize size, vk::raii::CommandPool& commandPool) {

        assert(*m_buffer != vk::Buffer{} && "Upload() called before Create()");
        assert(*m_memory != vk::DeviceMemory{} && "Buffer has no memory allocated");

        // Create staging buffer
        vk::BufferCreateInfo bufferInfo { 
            .size = size, 
            .usage = vk::BufferUsageFlagBits::eTransferSrc, 
            .sharingMode = vk::SharingMode::eExclusive 
        };
        vk::raii::Buffer stagingBuffer = vk::raii::Buffer(context.GetDevice(), bufferInfo);

        vk::MemoryRequirements memRequirements = stagingBuffer.getMemoryRequirements();
        vk::MemoryAllocateInfo allocInfo{ 
            .allocationSize = memRequirements.size, 
            .memoryTypeIndex = FindMemoryType(context, memRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent) 
        };
        vk::raii::DeviceMemory stagingBufferMemory = vk::raii::DeviceMemory(context.GetDevice(), allocInfo);
        stagingBuffer.bindMemory(*stagingBufferMemory, 0);

        // Store to staging buffer
        void* dataStaging = stagingBufferMemory.mapMemory(0, size);
        memcpy(dataStaging, data, size);
        stagingBufferMemory.unmapMemory();
        
        // Copy over to *this buffer
        vk::CommandBufferAllocateInfo copyInfo{ 
            .commandPool = commandPool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1 
        };
        vk::raii::CommandBuffer commandCopyBuffer = std::move(context.GetDevice().allocateCommandBuffers(copyInfo).front());

        commandCopyBuffer.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
        commandCopyBuffer.copyBuffer(*stagingBuffer, *m_buffer, vk::BufferCopy(0, 0, size));
        commandCopyBuffer.end();

        context.GetGraphicsQueue().submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &*commandCopyBuffer}, nullptr);
        context.GetGraphicsQueue().waitIdle();

    }   // VulkanBuffer::Upload()

    vk::raii::Buffer const& VulkanBuffer::GetBuffer() const { return m_buffer; }

    uint32_t VulkanBuffer::FindMemoryType(VulkanContext& context, uint32_t typeFilter, vk::MemoryPropertyFlags properties) {

        vk::PhysicalDeviceMemoryProperties memProperties = context.GetPhysicalDevice().getMemoryProperties();

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }
        throw std::runtime_error("[Renderer] Failed to find suitable memory type.");

    }   // VulkanBuffer::FindMemoryType()


}   // NAMESPACE CRUCIBLE