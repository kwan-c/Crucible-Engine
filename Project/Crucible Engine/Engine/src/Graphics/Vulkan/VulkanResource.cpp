/*************************************************************************
* \file:         VulkanResource.cpp
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                See VulkanResource.h for what VulkanResource/VulkanBuffer/
*                VulkanImage are and why they exist.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include "VulkanResource.h"

namespace Crucible {

    // ------------------------------------------------------------------------------------------------------------- //
    // VULKAN RESOURCE (base: VmaAllocation ownership shared by VulkanBuffer/VulkanImage)
    // ------------------------------------------------------------------------------------------------------------- //

    VulkanResource::~VulkanResource() {

        if (m_allocation != VK_NULL_HANDLE) {
            vmaFreeMemory(m_allocator, m_allocation);
        }

    }   // VulkanResource::~VulkanResource()

    VulkanResource::VulkanResource(VulkanResource&& other) noexcept
        : m_allocator(other.m_allocator)
        , m_allocation(other.m_allocation)
        , m_mappedData(other.m_mappedData) {

        other.m_allocator  = VK_NULL_HANDLE;
        other.m_allocation = VK_NULL_HANDLE;
        other.m_mappedData = nullptr;

    }   // VulkanResource::VulkanResource(&&)

    VulkanResource& VulkanResource::operator=(VulkanResource&& other) noexcept {

        if (this != &other) {

            if (m_allocation != VK_NULL_HANDLE) {
                vmaFreeMemory(m_allocator, m_allocation);
            }

            m_allocator  = other.m_allocator;
            m_allocation = other.m_allocation;
            m_mappedData = other.m_mappedData;

            other.m_allocator  = VK_NULL_HANDLE;
            other.m_allocation = VK_NULL_HANDLE;
            other.m_mappedData = nullptr;
        }
        return *this;

    }   // VulkanResource::operator=(&&)

    void* VulkanResource::Map() {

        // The common case: the allocation was created with VMA_ALLOCATION_CREATE_MAPPED_BIT (see
        // MakeAllocationCreateInfo()), so VMA already mapped it at creation time - just hand back
        // that pointer without ever touching VMA's own map reference count.
        if (m_mappedData != nullptr) {
            return m_mappedData;
        }

        // Fallback for an allocation that wasn't created with MAPPED_BIT (there currently isn't
        // one - MakeAllocationCreateInfo() sets it on every host-visible request - but this keeps
        // Map() correct if that ever changes). Note this path DOES increment VMA's map count, so
        // it must be paired with a real Unmap() call and never left permanently mapped.
        void* data = nullptr;
        if (vmaMapMemory(m_allocator, m_allocation, &data) != VK_SUCCESS) {
            throw std::runtime_error("[Renderer] vmaMapMemory failed.");
        }
        return data;

    }   // VulkanResource::Map()

    void VulkanResource::Unmap() {

        // Persistent mapping (the common case - see Map() above): nothing to do. VMA keeps it
        // mapped until the allocation itself is freed, at which point it unmaps automatically.
        if (m_mappedData != nullptr) {
            return;
        }

        vmaUnmapMemory(m_allocator, m_allocation);

    }   // VulkanResource::Unmap()

    namespace {

        // Shared by VulkanBuffer::Create and VulkanImage::Create: translates the
        // engine's existing vk::MemoryPropertyFlags "what kind of memory do I want" request into
        // a VmaAllocationCreateInfo.
        //
        // NOTE: this deliberately does NOT set `usage` to VMA_MEMORY_USAGE_AUTO*. Those values
        // only work when VMA itself knows the resource's buffer/image usage flags, which it infers
        // automatically inside vmaCreateBuffer()/vmaCreateImage() - but VulkanBuffer/VulkanImage
        // create their own vk::raii::Buffer/Image first and only ask VMA for memory afterwards, via
        // vmaAllocateMemoryForBuffer()/vmaAllocateMemoryForImage(). Those two functions always pass
        // VmaBufferImageUsage::UNKNOWN internally (see vk_mem_alloc.h), so combining them with
        // VMA_MEMORY_USAGE_AUTO* trips VMA's own assert in FindMemoryPreferences() ("VMA_MEMORY_
        // USAGE_AUTO* values can only be used with functions like vmaCreateBuffer, vmaCreateImage")
        // - which is exactly the __debugbreak() seen from VulkanImage::Create(). Instead this uses
        // VMA's documented workaround for "I created the resource myself": leave `usage` at its
        // zero-initialized VMA_MEMORY_USAGE_UNKNOWN and specify the memory type directly via
        // `requiredFlags`, the same information FindMemoryType() used to consume by hand.
        VmaAllocationCreateInfo MakeAllocationCreateInfo(vk::MemoryPropertyFlags properties) {

            VmaAllocationCreateInfo allocCreateInfo{};
            allocCreateInfo.requiredFlags = static_cast<VkMemoryPropertyFlags>(properties);

            if (properties & vk::MemoryPropertyFlagBits::eHostVisible) {
                // Sequential-write covers every current CPU-write use (uniform buffer updates,
                // staging uploads); switch to the RANDOM_BIT variant if something ever reads back
                // through a mapped pointer instead of just writing to it. Needed independently of
                // `usage` above: vmaMapMemory()/vmaCopyMemoryToAllocation() assert on this flag
                // (IsMappingAllowed()) regardless of how the memory type itself was chosen.
                allocCreateInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

                // Also ask VMA to map it once, immediately, for the allocation's whole lifetime
                // ("persistent mapping"). Without this, a buffer that's meant to stay mapped
                // forever (the uniform buffers in RenderSystem.cpp call Map() once at creation and
                // never Unmap()) would leave VMA's internal map reference count non-zero forever,
                // which trips VMA's own VMA_ASSERT_LEAK(mapCount == 0) inside VmaAllocation_T's
                // destructor - i.e. a debug-break the moment vmaFreeMemory() destroys it, which is
                // exactly what "closing the application" (when these buffers finally get
                // destroyed) hit. See VulkanResource::Map()/Unmap() for how this pointer gets used.
                allocCreateInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
            }

            return allocCreateInfo;

        }   // MakeAllocationCreateInfo()

    }   // ANONYMOUS NAMESPACE

    // ------------------------------------------------------------------------------------------------------------- //
    // VULKAN BUFFER
    // ------------------------------------------------------------------------------------------------------------- //

    void VulkanBuffer::Create(VulkanContext& context, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties) {

        vk::BufferCreateInfo bufferInfo{
            .size = size,
            .usage = usage,
            .sharingMode = vk::SharingMode::eExclusive
        };
        m_buffer = vk::raii::Buffer(context.GetDevice(), bufferInfo);

        m_allocator = context.GetAllocator();

        // Create() can legitimately run more than once on the same VulkanBuffer (none currently
        // do, but nothing stops it) - free any allocation from a previous Create() first. The old
        // vk::raii::DeviceMemory did this for free via move-assignment; a raw VmaAllocation handle
        // doesn't, so it has to be explicit here.
        if (m_allocation != VK_NULL_HANDLE) {
            vmaFreeMemory(m_allocator, m_allocation);
            m_allocation = VK_NULL_HANDLE;
        }

        VmaAllocationCreateInfo allocCreateInfo = MakeAllocationCreateInfo(properties);

        // vmaAllocateMemoryForBuffer() queries the buffer's own memory requirements internally, so
        // there's no separate getMemoryRequirements() call needed here (FindMemoryType() is gone
        // entirely - MakeAllocationCreateInfo()'s requiredFlags above replaces it). The
        // VmaAllocationInfo out-param gives back pMappedData when the allocation was created with
        // VMA_ALLOCATION_CREATE_MAPPED_BIT (host-visible requests only - see
        // MakeAllocationCreateInfo()); non-null-checked and cached so Map()/Unmap() can use it
        // instead of touching VMA's own map reference count (see their doc comments in the header).
        VmaAllocationInfo allocInfo{};
        if (vmaAllocateMemoryForBuffer(m_allocator, static_cast<VkBuffer>(*m_buffer), &allocCreateInfo, &m_allocation, &allocInfo) != VK_SUCCESS) {
            throw std::runtime_error("[Renderer] vmaAllocateMemoryForBuffer failed.");
        }
        if (vmaBindBufferMemory(m_allocator, m_allocation, static_cast<VkBuffer>(*m_buffer)) != VK_SUCCESS) {
            throw std::runtime_error("[Renderer] vmaBindBufferMemory failed.");
        }
        m_mappedData = allocInfo.pMappedData;

    }   // VulkanBuffer::Create()

    void VulkanBuffer::Upload(VulkanContext& context, void const* data, vk::DeviceSize size) {

        (void)context;

        // One-shot map + memcpy + unmap (and, for a non-coherent memory type, the flush that
        // needs), done atomically by VMA instead of by hand through Map()/Unmap() - this is what
        // the persistently-mapped uniform buffers' per-frame UpdateViewportUniforms()/memcpy path
        // still does manually via the raw pointer; this Upload() path is for host-visible buffers
        // that aren't kept mapped between uploads.
        if (vmaCopyMemoryToAllocation(m_allocator, data, m_allocation, 0, size) != VK_SUCCESS) {
            throw std::runtime_error("[Renderer] vmaCopyMemoryToAllocation failed.");
        }

    }   // VulkanBuffer::Upload()

    const vk::raii::Buffer& VulkanBuffer::GetBuffer() const { return m_buffer; }

    // ------------------------------------------------------------------------------------------------------------- //
    // VULKAN IMAGE
    // ------------------------------------------------------------------------------------------------------------- //

    void VulkanImage::Create(VulkanContext& context, uint32_t w, uint32_t h, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties,
        uint32_t arrayLayers, vk::ImageCreateFlags flags) {

        vk::ImageCreateInfo imageInfo{
            .flags       = flags,
            .imageType   = vk::ImageType::e2D,
            .format      = format,
            .extent      = {w, h, 1},
            .mipLevels   = 1,
            .arrayLayers = arrayLayers,
            .samples     = vk::SampleCountFlagBits::e1,
            .tiling      = tiling,
            .usage       = usage,
            .sharingMode = vk::SharingMode::eExclusive
        };
        m_image = vk::raii::Image(context.GetDevice(), imageInfo);

        m_allocator = context.GetAllocator();

        // See the matching comment in VulkanBuffer::Create(): Renderer::ResizeViewport() calls
        // CreateViewportImages() -> VulkanImage::Create() again on the SAME vp.colorImage/depthImage
        // objects (after a waitIdle(), so it's safe to free immediately) - without this, every
        // viewport resize would leak the previous allocation.
        if (m_allocation != VK_NULL_HANDLE) {
            vmaFreeMemory(m_allocator, m_allocation);
            m_allocation = VK_NULL_HANDLE;
        }

        VmaAllocationCreateInfo allocCreateInfo = MakeAllocationCreateInfo(properties);

        // See the matching comment in VulkanBuffer::Create(): captures pMappedData in case this
        // is ever created host-visible (nothing in this codebase does today - images are always
        // eDeviceLocal - so allocInfo.pMappedData will simply be null here in practice).
        VmaAllocationInfo allocInfo{};
        if (vmaAllocateMemoryForImage(m_allocator, static_cast<VkImage>(*m_image), &allocCreateInfo, &m_allocation, &allocInfo) != VK_SUCCESS) {
            throw std::runtime_error("[Renderer] vmaAllocateMemoryForImage failed.");
        }
        if (vmaBindImageMemory(m_allocator, m_allocation, static_cast<VkImage>(*m_image)) != VK_SUCCESS) {
            throw std::runtime_error("[Renderer] vmaBindImageMemory failed.");
        }
        m_mappedData = allocInfo.pMappedData;

    }   // VulkanImage::Create()

    const vk::raii::Image&  VulkanImage::GetImage() const { return m_image; }
    vk::raii::Image&  VulkanImage::GetImage() { return m_image; }

}   // NAMESPACE CRUCIBLE
