#pragma once

#include "VulkanContext.h"

namespace Crucible {

    // Base for VulkanBuffer/VulkanImage: owns the one thing they have in common, a VmaAllocation
    // (a suballocated range inside one of VMA's shared VkDeviceMemory blocks) rather than each
    // resource doing its own vkAllocateMemory. See VulkanContext::m_allocator's doc comment for
    // why that used to be a problem (VkPhysicalDeviceLimits::maxMemoryAllocationCount, commonly
    // 4096, is a hard ceiling on live vkAllocateMemory calls - independent of how much VRAM is
    // actually free - and every mesh/texture upload used to cost two of them, one for the
    // resource and one for its throwaway staging buffer).
    //
    // Move-only, same as before (vk::raii::Buffer/Image in the derived classes already forced
    // that), but now needs its own move logic: a VmaAllocation is a raw handle, so without an
    // explicit move ctor/assignment a vector reallocation (m_meshes/m_textures growing) would
    // memberwise-copy it into the new slot and then free it a second time when the moved-from
    // slot's destructor also runs.
    class VulkanResource {

    public:
        VulkanResource() = default;
        ~VulkanResource();

        VulkanResource(const VulkanResource&)            = delete;
        VulkanResource& operator=(const VulkanResource&) = delete;
        VulkanResource(VulkanResource&& other) noexcept;
        VulkanResource& operator=(VulkanResource&& other) noexcept;

        // Returns the CPU-visible pointer for the whole allocation (VMA always maps/exposes the
        // entire allocation, never a sub-range). Only valid for allocations created with a
        // VMA_ALLOCATION_CREATE_HOST_ACCESS_*_BIT flag (see VulkanBuffer::Create's properties
        // param) - those are also always created with VMA_ALLOCATION_CREATE_MAPPED_BIT (see
        // MakeAllocationCreateInfo() in the .cpp), so Map()/Unmap() never actually call
        // vmaMapMemory()/vmaUnmapMemory() themselves; they just hand back the pointer VMA already
        // mapped at allocation time (m_mappedData) and Unmap() is a no-op. This matters because
        // VMA's own VmaAllocation_T asserts `mapCount == 0` in ITS destructor (i.e. inside
        // vmaFreeMemory) - a real vmaMapMemory() call that's never matched by vmaUnmapMemory()
        // (exactly what a "map once at creation, never unmap" persistently-mapped uniform buffer
        // needs to do) would trip that assert the moment the buffer is destroyed. Routing through
        // VMA's own persistent-mapping mechanism instead sidesteps that counter entirely.
        void* Map();
        void  Unmap();

    protected:
        VmaAllocator  m_allocator  = VK_NULL_HANDLE;   // non-owning: VulkanContext owns the allocator itself
        VmaAllocation m_allocation = VK_NULL_HANDLE;   // owning: freed in ~VulkanResource()
        void*         m_mappedData = nullptr;          // non-owning: cached VmaAllocationInfo::pMappedData, set by Create() when the allocation was created with VMA_ALLOCATION_CREATE_MAPPED_BIT

    };  // VULKAN RESOURCE CLASS

    class VulkanBuffer : public VulkanResource {

    public:
        // 'properties' keeps its old vk::MemoryPropertyFlags shape (call sites are unchanged) but
        // now only steers VMA's allocation strategy (VMA_MEMORY_USAGE_AUTO plus a host-access
        // flag when eHostVisible is requested) rather than picking a memory type by hand.
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
