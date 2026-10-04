#pragma once
/*************************************************************************
* \file:         VulkanResource.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                Base VmaAllocation ownership shared by VulkanBuffer and
*                VulkanImage.
*
* \details
*                Declares VulkanResource (the common base owning one
*                VmaAllocation), VulkanBuffer, and VulkanImage - the
*                engine's thin wrappers around a Vulkan buffer/image plus
*                the VMA-suballocated device memory backing it. See
*                VulkanContext::GetAllocator()'s doc comment for the single
*                shared VmaAllocator these all suballocate from.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include "VulkanContext.h"

namespace Crucible {

    /*************************************************************************
     \brief
        Base for VulkanBuffer/VulkanImage: owns the one thing they have in
        common, a VmaAllocation (a suballocated range inside one of VMA's
        shared VkDeviceMemory blocks) rather than each resource doing its
        own vkAllocateMemory.

     \details
        See VulkanContext::m_allocator's doc comment for why that used to
        be a problem (VkPhysicalDeviceLimits::maxMemoryAllocationCount,
        commonly 4096, is a hard ceiling on live vkAllocateMemory calls -
        independent of how much VRAM is actually free - and every
        mesh/texture upload used to cost two of them, one for the resource
        and one for its throwaway staging buffer).

        Move-only, same as before (vk::raii::Buffer/Image in the derived
        classes already forced that), but now needs its own move logic: a
        VmaAllocation is a raw handle, so without an explicit move
        ctor/assignment a vector reallocation (m_meshes/m_textures
        growing) would memberwise-copy it into the new slot and then free
        it a second time when the moved-from slot's destructor also runs.
     *************************************************************************/
    class VulkanResource {

    public:
        VulkanResource() = default;

        /*********************************************************************
         \brief  Frees the owned VmaAllocation, if any.
         *********************************************************************/
        ~VulkanResource();

        VulkanResource(const VulkanResource&)            = delete;
        VulkanResource& operator=(const VulkanResource&) = delete;

        /*********************************************************************
         \brief  Transfers ownership of the allocation (and cached mapped
                 pointer) from `other`, leaving it in the empty state.
         \param[in] other The resource to move from.
         *********************************************************************/
        VulkanResource(VulkanResource&& other) noexcept;

        /*********************************************************************
         \brief  Frees this resource's current allocation (if any), then
                 transfers ownership from `other`, leaving it in the empty
                 state.
         \param[in] other The resource to move from.
         \return Reference to *this.
         *********************************************************************/
        VulkanResource& operator=(VulkanResource&& other) noexcept;

        /*********************************************************************
         \brief
            Returns the CPU-visible pointer for the whole allocation (VMA
            always maps/exposes the entire allocation, never a sub-range).

         \return
            The mapped pointer.

         \details
            Only valid for allocations created with a
            VMA_ALLOCATION_CREATE_HOST_ACCESS_*_BIT flag (see
            VulkanBuffer::Create's properties param) - those are also
            always created with VMA_ALLOCATION_CREATE_MAPPED_BIT (see
            MakeAllocationCreateInfo() in the .cpp), so Map()/Unmap() never
            actually call vmaMapMemory()/vmaUnmapMemory() themselves; they
            just hand back the pointer VMA already mapped at allocation
            time (m_mappedData) and Unmap() is a no-op. This matters
            because VMA's own VmaAllocation_T asserts `mapCount == 0` in
            ITS destructor (i.e. inside vmaFreeMemory) - a real
            vmaMapMemory() call that's never matched by vmaUnmapMemory()
            (exactly what a "map once at creation, never unmap"
            persistently-mapped uniform buffer needs to do) would trip
            that assert the moment the buffer is destroyed. Routing
            through VMA's own persistent-mapping mechanism instead
            sidesteps that counter entirely.
         *********************************************************************/
        void* Map();

        /*********************************************************************
         \brief  Counterpart to Map(). A no-op for a persistently-mapped
                 allocation (the common case - see Map()'s doc comment);
                 otherwise calls vmaUnmapMemory().
         *********************************************************************/
        void  Unmap();

    protected:
        VmaAllocator  m_allocator  = VK_NULL_HANDLE;   // non-owning: VulkanContext owns the allocator itself
        VmaAllocation m_allocation = VK_NULL_HANDLE;   // owning: freed in ~VulkanResource()
        void*         m_mappedData = nullptr;          // non-owning: cached VmaAllocationInfo::pMappedData, set by Create() when the allocation was created with VMA_ALLOCATION_CREATE_MAPPED_BIT

    };  // VULKAN RESOURCE CLASS

    /*************************************************************************
     \brief
        Vulkan buffer + its VMA-suballocated device memory.
     *************************************************************************/
    class VulkanBuffer : public VulkanResource {

    public:
        /*********************************************************************
         \brief
            Creates the buffer and suballocates its backing memory from
            VulkanContext's shared VmaAllocator.

         \param[in] context    The VulkanContext to create/allocate through.
         \param[in] size       Buffer size, in bytes.
         \param[in] usage      Vulkan buffer usage flags.
         \param[in] properties Desired memory properties (e.g. device-local,
                                host-visible).

         \details
            'properties' keeps its old vk::MemoryPropertyFlags shape (call
            sites are unchanged) but now only steers VMA's allocation
            strategy (VMA_MEMORY_USAGE_AUTO plus a host-access flag when
            eHostVisible is requested) rather than picking a memory type by
            hand.
         *********************************************************************/
        void Create(VulkanContext& context, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);

        /*********************************************************************
         \brief  One-shot map + copy + unmap into this buffer's memory.
         \param[in] context Unused (kept for interface symmetry).
         \param[in] data    Source bytes to copy in.
         \param[in] size    Number of bytes to copy.

         \details
            Host-visible buffers only. A device-local buffer can't be
            mapped; fill it through VulkanUploadQueue::QueueBuffer()
            instead, which goes through a staging buffer and a GPU-side
            copy.
         *********************************************************************/
        void Upload(VulkanContext& context, void const* data, vk::DeviceSize size);

        /*********************************************************************
         \brief  Accessor.
         \return Reference to the owned vk::raii::Buffer.
         *********************************************************************/
        const vk::raii::Buffer& GetBuffer() const;

    private:
        vk::raii::Buffer        m_buffer = nullptr;

    };  // VULKAN BUFFER CLASS

    /*************************************************************************
     \brief
        Vulkan image + its VMA-suballocated device memory.
     *************************************************************************/
    class VulkanImage : public VulkanResource {

    public:
        /*********************************************************************
         \brief  Creates the image and suballocates its backing memory from
                 VulkanContext's shared VmaAllocator.
         \param[in] context    The VulkanContext to create/allocate through.
         \param[in] w          Image width, in texels.
         \param[in] h          Image height, in texels.
         \param[in] format     Image format.
         \param[in] tiling     Image tiling mode.
         \param[in] usage      Vulkan image usage flags.
         \param[in] properties Desired memory properties (e.g. device-local).
         \param[in] arrayLayers Number of array layers (6 for a cubemap).
                                Defaults to 1 (a plain 2D image).
         \param[in] flags       Image create flags (vk::ImageCreateFlagBits::
                                eCubeCompatible for a cubemap). Defaults to
                                none.
         *********************************************************************/
        void Create(VulkanContext& context, uint32_t w, uint32_t h, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties,
                    uint32_t arrayLayers = 1, vk::ImageCreateFlags flags = {});

        /*********************************************************************
         \brief  Accessor.
         \return Const reference to the owned vk::raii::Image.
         *********************************************************************/
        const vk::raii::Image&  GetImage() const;

        /*********************************************************************
         \brief  Accessor.
         \return Mutable reference to the owned vk::raii::Image.
         *********************************************************************/
        vk::raii::Image&        GetImage();

    private:
        vk::raii::Image         m_image = nullptr;

    };  // VULKAN IMAGE CLASS

}   // NAMESPACE CRUCIBLE
