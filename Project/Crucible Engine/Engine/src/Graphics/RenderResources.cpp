/*************************************************************************
  \file       RenderResources.cpp
  \brief
    See RenderResources.h.
*************************************************************************/
#include "Graphics/RenderResources.h"

#include "Graphics/MaterialShaders.h"
#include "Diagnostics/Logger.h"

#include "Graphics/Vulkan/RenderLayouts.h"   // Vertex
#include "Graphics/Vulkan/VulkanFrameManager.h"   // MAX_FRAMES_IN_FLIGHT (ReleaseDestroyed)
#include "Resources/CompiledTexture.h"            // CreateTextureCompressed


#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>   // memcpy
#include <stdexcept>
#include <string>   // to_string

namespace Crucible {

    MeshHandle RenderResources::CreateMesh(const MeshData& data, const std::string& debugName) {

        MeshHandle handle{ static_cast<uint32_t>(m_meshes.size()) };
        m_meshes.emplace_back();       // reserve slot (indexCount == 0 => not yet renderable)
        UploadMesh(handle, data, debugName);      // ... buffers created and their copies queued, right here
        return handle;

    }   // RenderResources::CreateMesh()

    MeshHandle RenderResources::CreateMeshAsync(MeshData data, const std::string& debugName) {

        MeshHandle handle{ static_cast<uint32_t>(m_meshes.size()) };
        m_meshes.emplace_back();       // reserve slot; RecordCommandBuffer skips it until uploaded
        m_pendingMeshUploads.push_back({ handle, std::move(data), debugName });
        return handle;

    }   // RenderResources::CreateMeshAsync()

    void RenderResources::ProcessPendingMeshUploads() {

        for (uint32_t n = 0; n < kMaxMeshUploadsPerFrame && !m_pendingMeshUploads.empty(); ++n) {
            PendingMeshUpload pending = std::move(m_pendingMeshUploads.front());
            m_pendingMeshUploads.pop_front();
            UploadMesh(pending.handle, pending.data, pending.debugName);
        }

    }   // RenderResources::ProcessPendingMeshUploads()

    MeshHandle RenderResources::CreateDynamicMesh(const std::string& debugName) {

        MeshHandle handle{ static_cast<uint32_t>(m_meshes.size()) };
        GpuMesh& mesh = m_meshes.emplace_back();   // indexCount 0: not drawn until it has geometry
        mesh.debugName = debugName.empty() ? ("Dynamic mesh " + std::to_string(handle.id)) : debugName;
        mesh.dynamic   = std::make_unique<DynamicMesh>();
        m_dynamicMeshes.push_back(handle.id);
        return handle;

    }   // RenderResources::CreateDynamicMesh()

    void RenderResources::UpdateDynamicMesh(MeshHandle handle, std::span<const Vertex> vertices, std::span<const uint32_t> indices) {

        if (!handle.IsValid() || handle.id >= m_meshes.size() || !m_meshes[handle.id].dynamic) {
            throw std::invalid_argument("[RenderResources] UpdateDynamicMesh: not a dynamic mesh");
        }
        // An index past the end would make the GPU read outside the vertex buffer.
        for (uint32_t index : indices) {
            if (index >= vertices.size()) { throw std::invalid_argument("[RenderResources] UpdateDynamicMesh: index out of range"); }
        }

        GpuMesh& mesh = m_meshes[handle.id];
        DynamicMesh& dynamic = *mesh.dynamic;
        dynamic.vertices.assign(vertices.begin(), vertices.end());
        dynamic.indices.assign(indices.begin(), indices.end());
        ++dynamic.version;

        // What culling sees (GetMeshBounds / indexCount) is the newest geometry: it is what the
        // frame about to be prepared will upload and draw.
        mesh.indexCount = vertices.empty() ? 0u : static_cast<uint32_t>(indices.size());
        mesh.boundsMin = mesh.boundsMax = vertices.empty() ? glm::vec3(0.0f) : vertices.front().pos;
        for (const Vertex& v : vertices) {
            mesh.boundsMin = glm::min(mesh.boundsMin, v.pos);
            mesh.boundsMax = glm::max(mesh.boundsMax, v.pos);
        }

    }   // RenderResources::UpdateDynamicMesh()

    void RenderResources::UploadDynamicMeshes(uint32_t frameIndex) {

        m_dynamicFrame = frameIndex;

        // Grows a buffer to the next power of two that fits. Safe to replace here: this frame
        // index's fence has signalled, so the GPU is done with the old one.
        const auto grow = [this](VulkanBuffer& buffer, void*& mapped, size_t& capacity, size_t needed, size_t minimum,
                                 vk::DeviceSize elementSize, vk::BufferUsageFlags usage) {
            if (capacity >= needed) { return; }
            size_t newCapacity = minimum;
            while (newCapacity < needed) { newCapacity *= 2; }
            buffer.Create(m_context, vk::DeviceSize{ newCapacity } * elementSize, usage,
                          vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            mapped   = buffer.Map();
            capacity = newCapacity;
        };

        for (uint32_t id : m_dynamicMeshes) {
            DynamicMesh& dynamic = *m_meshes[id].dynamic;
            if (frameIndex >= dynamic.frames.size()) { dynamic.frames.resize(static_cast<size_t>(frameIndex) + 1); }
            DynamicMeshFrame& frame = dynamic.frames[frameIndex];
            if (frame.version == dynamic.version) { continue; }   // already holds the latest
            frame.version = dynamic.version;

            frame.indexCount = 0;
            if (dynamic.vertices.empty() || dynamic.indices.empty()) { continue; }

            grow(frame.vertexBuffer, frame.vertexMapped, frame.vertexCapacity, dynamic.vertices.size(), 1024,
                 sizeof(Vertex), vk::BufferUsageFlagBits::eVertexBuffer);
            grow(frame.indexBuffer, frame.indexMapped, frame.indexCapacity, dynamic.indices.size(), 2048,
                 sizeof(uint32_t), vk::BufferUsageFlagBits::eIndexBuffer);
            std::memcpy(frame.vertexMapped, dynamic.vertices.data(), dynamic.vertices.size() * sizeof(Vertex));
            std::memcpy(frame.indexMapped, dynamic.indices.data(), dynamic.indices.size() * sizeof(uint32_t));
            frame.indexCount = static_cast<uint32_t>(dynamic.indices.size());
        }

    }   // RenderResources::UploadDynamicMeshes()

    uint32_t RenderResources::BindMesh(vk::CommandBuffer cmd, MeshHandle handle) const {

        const GpuMesh& mesh = m_meshes[handle.id];
        if (!mesh.dynamic) {
            if (mesh.indexCount == 0) { return 0; }
            cmd.bindVertexBuffers(0, *mesh.vertexBuffer.GetBuffer(), { 0 });
            cmd.bindIndexBuffer(*mesh.indexBuffer.GetBuffer(), 0, vk::IndexType::eUint32);
            return mesh.indexCount;
        }

        const DynamicMesh& dynamic = *mesh.dynamic;
        if (m_dynamicFrame >= dynamic.frames.size()) { return 0; }
        const DynamicMeshFrame& frame = dynamic.frames[m_dynamicFrame];
        if (frame.indexCount == 0) { return 0; }
        cmd.bindVertexBuffers(0, *frame.vertexBuffer.GetBuffer(), { 0 });
        cmd.bindIndexBuffer(*frame.indexBuffer.GetBuffer(), 0, vk::IndexType::eUint32);
        return frame.indexCount;

    }   // RenderResources::BindMesh()

    void RenderResources::UploadMesh(MeshHandle handle, const MeshData& data, const std::string& debugName) {

        if (data.vertices.empty() || data.indices.empty()) {
            throw std::runtime_error("[RenderResources] UploadMesh: mesh has no vertices or indices");
        }

        GpuMesh mesh;
        mesh.debugName = debugName.empty() ? ("Mesh " + std::to_string(handle.id)) : debugName;

        vk::DeviceSize vbSize = sizeof(Vertex) * data.vertices.size();
        mesh.vertexBuffer.Create(m_context, vbSize,
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        m_uploads.QueueBuffer(*mesh.vertexBuffer.GetBuffer(), data.vertices.data(), vbSize);

        vk::DeviceSize ibSize = sizeof(uint32_t) * data.indices.size();
        mesh.indexBuffer.Create(m_context, ibSize,
            vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        m_uploads.QueueBuffer(*mesh.indexBuffer.GetBuffer(), data.indices.data(), ibSize);

        mesh.indexCount = static_cast<uint32_t>(data.indices.size());

        mesh.boundsMin = mesh.boundsMax = data.vertices.front().pos;
        mesh.cpuPositions.reserve(data.vertices.size());
        for (const Vertex& v : data.vertices) {
            mesh.boundsMin = glm::min(mesh.boundsMin, v.pos);
            mesh.boundsMax = glm::max(mesh.boundsMax, v.pos);
            mesh.cpuPositions.push_back(v.pos);
        }
        mesh.cpuIndices = data.indices;

        m_meshes[handle.id] = std::move(mesh);

    }   // RenderResources::UploadMesh()

    bool RenderResources::GetMeshBounds(MeshHandle handle, glm::vec3& outMin, glm::vec3& outMax) const {

        if (!handle.IsValid() || handle.id >= m_meshes.size() || m_meshes[handle.id].indexCount == 0) { return false; }
        outMin = m_meshes[handle.id].boundsMin;
        outMax = m_meshes[handle.id].boundsMax;
        return true;

    }   // RenderResources::GetMeshBounds()

    const std::string& RenderResources::GetMeshName(MeshHandle handle) const {

        static const std::string kUnknown = "(unknown mesh)";
        if (!handle.IsValid() || handle.id >= m_meshes.size()) { return kUnknown; }
        return m_meshes[handle.id].debugName;

    }   // RenderResources::GetMeshName()

    void RenderResources::DrawBatches(vk::CommandBuffer cmd, const std::vector<DrawBatch>& batches) const {

        MeshHandle bound;
        uint32_t   indexCount = 0;
        for (const DrawBatch& batch : batches) {
            if (!(batch.mesh == bound)) {
                indexCount = BindMesh(cmd, batch.mesh);
                bound = batch.mesh;
            }
            if (indexCount != 0) { cmd.drawIndexed(indexCount, batch.instanceCount, 0, 0, batch.firstInstance); }
        }

    }   // RenderResources::DrawBatches()

    // ------------------------------------------------------------------------------------------------------------- //
    //  TEXTURES
    // ------------------------------------------------------------------------------------------------------------- //

    namespace {

        // sRGB <-> linear tables for mipmap generation. Colour textures store sRGB-encoded
        // values; averaging those directly would make the smaller levels too dark.
        struct SrgbTables {
            std::array<float, 256>    toLinear{};
            std::array<uint8_t, 4096> toSrgb{};
            SrgbTables() {
                for (int i = 0; i < 256; ++i) {
                    const float c = static_cast<float>(i) / 255.0f;
                    toLinear[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
                }
                for (int i = 0; i < 4096; ++i) {
                    const float l = static_cast<float>(i) / 4095.0f;
                    const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
                    toSrgb[i] = static_cast<uint8_t>(std::clamp(c * 255.0f + 0.5f, 0.0f, 255.0f));
                }
            }
        };

        // The next mip level of an RGBA8 image: each texel is the average of the (up to) 2x2
        // texels it covers. Alpha, and every channel of a non-colour texture, is averaged as is.
        std::vector<unsigned char> HalveRGBA8(const unsigned char* src, uint32_t w, uint32_t h, uint32_t dw, uint32_t dh, bool isSRGB) {

            static const SrgbTables tables;

            std::vector<unsigned char> dst(static_cast<size_t>(dw) * dh * 4);
            for (uint32_t y = 0; y < dh; ++y) {
                const uint32_t y0 = std::min(y * 2, h - 1);
                const uint32_t y1 = std::min(y * 2 + 1, h - 1);
                for (uint32_t x = 0; x < dw; ++x) {
                    const uint32_t x0 = std::min(x * 2, w - 1);
                    const uint32_t x1 = std::min(x * 2 + 1, w - 1);
                    const unsigned char* p[4] = {
                        src + (static_cast<size_t>(y0) * w + x0) * 4, src + (static_cast<size_t>(y0) * w + x1) * 4,
                        src + (static_cast<size_t>(y1) * w + x0) * 4, src + (static_cast<size_t>(y1) * w + x1) * 4 };
                    unsigned char* out = dst.data() + (static_cast<size_t>(y) * dw + x) * 4;
                    for (int c = 0; c < 4; ++c) {
                        if (isSRGB && c < 3) {
                            const float sum = tables.toLinear[p[0][c]] + tables.toLinear[p[1][c]] + tables.toLinear[p[2][c]] + tables.toLinear[p[3][c]];
                            out[c] = tables.toSrgb[static_cast<size_t>(sum * 0.25f * 4095.0f + 0.5f)];
                        } else {
                            out[c] = static_cast<unsigned char>((p[0][c] + p[1][c] + p[2][c] + p[3][c] + 2) / 4);
                        }
                    }
                }
            }
            return dst;
        }

        // The next mip level of a one-channel image (Alpha8): the plain average of the (up to)
        // 2x2 texels each texel covers.
        std::vector<unsigned char> HalveA8(const unsigned char* src, uint32_t w, uint32_t h, uint32_t dw, uint32_t dh) {

            std::vector<unsigned char> dst(static_cast<size_t>(dw) * dh);
            for (uint32_t y = 0; y < dh; ++y) {
                const uint32_t y0 = std::min(y * 2, h - 1);
                const uint32_t y1 = std::min(y * 2 + 1, h - 1);
                for (uint32_t x = 0; x < dw; ++x) {
                    const uint32_t x0 = std::min(x * 2, w - 1);
                    const uint32_t x1 = std::min(x * 2 + 1, w - 1);
                    const unsigned sum = src[static_cast<size_t>(y0) * w + x0] + src[static_cast<size_t>(y0) * w + x1]
                                       + src[static_cast<size_t>(y1) * w + x0] + src[static_cast<size_t>(y1) * w + x1];
                    dst[static_cast<size_t>(y) * dw + x] = static_cast<unsigned char>((sum + 2) / 4);
                }
            }
            return dst;
        }

    }   // namespace

    TextureHandle RenderResources::UploadTexturePixels(const unsigned char* pixels, uint32_t width, uint32_t height,
                                                       TexturePixelFormat pixelFormat, bool mipmaps) {

        const bool           alpha8        = pixelFormat == TexturePixelFormat::Alpha8;
        const bool           isSRGB        = pixelFormat == TexturePixelFormat::RGBA8_sRGB;
        const uint32_t       bytesPerTexel = alpha8 ? 1u : 4u;
        const vk::DeviceSize imageSize     = vk::DeviceSize{ width } * height * bytesPerTexel;

        // Mipmaps: a chain of half-size copies down to 1x1. The sampler picks (and blends) the
        // levels that match how big the texture is on screen, which stops distant surfaces
        // shimmering and is cheaper to sample. Font atlases skip them (see CreateTextureFromPixels).
        uint32_t mipLevels = 1;
        if (mipmaps) {
            for (uint32_t size = std::max(width, height); size > 1; size >>= 1) { ++mipLevels; }
        }

        GpuTexture tex;
        tex.width = width;
        tex.height = height;
        const vk::Format format = alpha8 ? vk::Format::eR8Unorm : isSRGB ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
        tex.image.Create(m_context, width, height, format,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal, 1, {}, mipLevels);

        // Copies `pixels` into a staging buffer now; the GPU-side copy and the transition to
        // eShaderReadOnlyOptimal are recorded at the start of the next frame (m_uploads).
        m_uploads.QueueImage(*tex.image.GetImage(), pixels, imageSize, width, height);

        // The smaller levels are made here on the CPU, each from the one before, and queued the
        // same way (one upload per level).
        std::vector<unsigned char> previous;
        const unsigned char* source = pixels;
        uint32_t w = width, h = height;
        for (uint32_t level = 1; level < mipLevels; ++level) {
            const uint32_t nw = std::max(w / 2, 1u), nh = std::max(h / 2, 1u);
            std::vector<unsigned char> next = alpha8 ? HalveA8(source, w, h, nw, nh) : HalveRGBA8(source, w, h, nw, nh, isSRGB);
            m_uploads.QueueImage(*tex.image.GetImage(), next.data(), vk::DeviceSize{ nw } * nh * bytesPerTexel, nw, nh, 1, level);
            previous = std::move(next);
            source   = previous.data();
            w = nw; h = nh;
        }

        // An Alpha8 texture is read as (1, 1, 1, value), so it works as a tintable coverage mask
        // (UIDrawMode::Image) and puts SDF distances where UIDrawMode::Sdf looks for them (alpha).
        const vk::ComponentMapping components = alpha8
            ? vk::ComponentMapping{ .r = vk::ComponentSwizzle::eOne, .g = vk::ComponentSwizzle::eOne, .b = vk::ComponentSwizzle::eOne, .a = vk::ComponentSwizzle::eR }
            : vk::ComponentMapping{};
        vk::ImageViewCreateInfo viewInfo{
            .image = tex.image.GetImage(),
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .components = components,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = mipLevels, .layerCount = 1 }
        };
        tex.view = vk::raii::ImageView(m_context.GetDevice(), viewInfo);

        const TextureHandle handle{ static_cast<uint32_t>(m_textures.size()) };
        m_textures.push_back(std::move(tex));
        return handle;

    }   // RenderResources::UploadTexturePixels()

    TextureHandle RenderResources::CreateSolidColorTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool isSRGB) {

        const unsigned char pixels[4] = { r, g, b, a };
        return UploadTexturePixels(pixels, 1, 1, isSRGB ? TexturePixelFormat::RGBA8_sRGB : TexturePixelFormat::RGBA8, false);

    }   // RenderResources::CreateSolidColorTexture()

    TextureHandle RenderResources::CreateTextureFromPixels(const void* pixels, uint32_t width, uint32_t height,
                                                           TexturePixelFormat format, bool mipmaps) {

        if (!pixels || width == 0 || height == 0) {
            throw std::runtime_error("[Renderer] CreateTextureFromPixels: no pixels, or a width or height of 0.");
        }
        const uint32_t maxSize = m_context.GetPhysicalDevice().getProperties().limits.maxImageDimension2D;
        if (width > maxSize || height > maxSize) {
            throw std::runtime_error("[Renderer] CreateTextureFromPixels: " + std::to_string(width) + " x " + std::to_string(height)
                                     + " is larger than this GPU allows (" + std::to_string(maxSize) + " per side).");
        }
        return UploadTexturePixels(static_cast<const unsigned char*>(pixels), width, height, format, mipmaps);

    }   // RenderResources::CreateTextureFromPixels()

    TextureHandle RenderResources::CreateTextureCompressed(const CompiledTexture& texture, bool isSRGB, const std::string& cacheKey) {

        if (!cacheKey.empty()) {
            if (auto it = m_textureCacheByKey.find(cacheKey); it != m_textureCacheByKey.end()) {
                return TextureHandle{ it->second };
            }
        }

        if (texture.layers != 1) {
            throw std::runtime_error("[Renderer] CreateTextureCompressed: a cube (.sky) is not a 2D texture");
        }

        // The Vulkan format of each .bill format. BC4, BC5 and BC6H have no sRGB variant.
        vk::Format format = vk::Format::eUndefined;
        switch (texture.format) {
            case CompiledTextureFormat::RGBA8: format = isSRGB ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm; break;
            case CompiledTextureFormat::BC1:   format = isSRGB ? vk::Format::eBc1RgbaSrgbBlock : vk::Format::eBc1RgbaUnormBlock; break;
            case CompiledTextureFormat::BC3:   format = isSRGB ? vk::Format::eBc3SrgbBlock : vk::Format::eBc3UnormBlock; break;
            case CompiledTextureFormat::BC4:   format = vk::Format::eBc4UnormBlock; break;
            case CompiledTextureFormat::BC5:   format = vk::Format::eBc5UnormBlock; break;
            case CompiledTextureFormat::BC6H:  format = vk::Format::eBc6HUfloatBlock; break;
            case CompiledTextureFormat::BC7:   format = isSRGB ? vk::Format::eBc7SrgbBlock : vk::Format::eBc7UnormBlock; break;
        }
        const vk::FormatProperties support = m_context.GetPhysicalDevice().getFormatProperties(format);
        if (format == vk::Format::eUndefined || !(support.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImage)) {
            throw std::runtime_error(std::string("[Renderer] This GPU cannot use ") + ToString(texture.format) + " textures");
        }

        const uint32_t mipLevels = static_cast<uint32_t>(texture.mips.size());
        GpuTexture tex;
        tex.width = texture.width;
        tex.height = texture.height;
        tex.image.Create(m_context, texture.width, texture.height, format,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::MemoryPropertyFlagBits::eDeviceLocal, 1, {}, mipLevels);

        // Every level as stored: no mipmaps to make here (the texture compiler made them).
        for (uint32_t level = 0; level < mipLevels; ++level) {
            const CompiledTextureMip& mip = texture.mips[level];
            m_uploads.QueueImage(*tex.image.GetImage(), texture.data.data() + mip.offset, mip.size, mip.width, mip.height, 1, level);
        }

        // BC4 holds one channel (red): read it as grey, like the image it was compiled from.
        const vk::ComponentMapping components = texture.format == CompiledTextureFormat::BC4
            ? vk::ComponentMapping{ .r = vk::ComponentSwizzle::eR, .g = vk::ComponentSwizzle::eR, .b = vk::ComponentSwizzle::eR, .a = vk::ComponentSwizzle::eOne }
            : vk::ComponentMapping{};
        vk::ImageViewCreateInfo viewInfo{
            .image = tex.image.GetImage(),
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .components = components,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = mipLevels, .layerCount = 1 }
        };
        tex.view = vk::raii::ImageView(m_context.GetDevice(), viewInfo);

        const TextureHandle handle{ static_cast<uint32_t>(m_textures.size()) };
        m_textures.push_back(std::move(tex));
        if (!cacheKey.empty()) { m_textureCacheByKey.emplace(cacheKey, handle.id); }
        return handle;

    }   // RenderResources::CreateTextureCompressed()

    const GpuTexture& RenderResources::GetTextureOrThrow(TextureHandle handle) const {

        if (!handle.IsValid() || handle.id >= m_textures.size()) {
            throw std::runtime_error("[Renderer] Unknown or invalid TextureHandle.");
        }
        if (m_textures[handle.id].destroyed) {
            throw std::runtime_error("[Renderer] TextureHandle refers to a destroyed texture.");
        }
        return m_textures[handle.id];

    }   // RenderResources::GetTextureOrThrow()

    void RenderResources::CreateDefaultTextures() {

        m_defaultWhiteTexture  = CreateSolidColorTexture(255, 255, 255, 255, /*isSRGB*/ true);
        m_defaultNormalTexture = CreateSolidColorTexture(128, 128, 255, 255, /*isSRGB*/ false);   // flat tangent-space normal (0,0,1)
        m_defaultBlackTexture  = CreateSolidColorTexture(0, 0, 0, 255, /*isSRGB*/ true);

    }   // RenderResources::CreateDefaultTextures()

    void RenderResources::CreateMaterialSampler() {

        // One sampler shared by every material texture. Repeat wrap (unlike the viewport
        // sampler's clamp-to-edge - these are tiling surface textures, not a single displayed
        // framebuffer image). Trilinear across the mip levels UploadTexturePixels() builds, plus
        // anisotropic filtering - which keeps surfaces seen at a shallow angle (floors, long
        // walls) sharp - when the GPU has it (VulkanContext enables the feature if so).
        const bool  anisotropy    = m_context.GetPhysicalDevice().getFeatures().samplerAnisotropy == vk::True;
        const float maxAnisotropy = std::min(8.0f, m_context.GetPhysicalDevice().getProperties().limits.maxSamplerAnisotropy);
        vk::SamplerCreateInfo samplerInfo{
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .mipmapMode = vk::SamplerMipmapMode::eLinear,
            .addressModeU = vk::SamplerAddressMode::eRepeat,
            .addressModeV = vk::SamplerAddressMode::eRepeat,
            .addressModeW = vk::SamplerAddressMode::eRepeat,
            .anisotropyEnable = anisotropy ? vk::True : vk::False,
            .maxAnisotropy = anisotropy ? maxAnisotropy : 1.0f,
            .compareEnable = vk::False,
            .minLod = 0.0f,
            .maxLod = vk::LodClampNone,
            .borderColor = vk::BorderColor::eIntOpaqueBlack,
            .unnormalizedCoordinates = vk::False
        };
        m_materialSampler = vk::raii::Sampler(m_context.GetDevice(), samplerInfo);

    }   // RenderResources::CreateMaterialSampler()

    void RenderResources::CreateMaterialDescriptorPool() {

        // One block's worth (kMaterialsPerDescriptorPool materials, each a set of eight
        // combined-image-samplers) - see CreateMaterial() for what happens when it fills up.
        vk::DescriptorPoolSize poolSize{
            .type = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = kMaterialsPerDescriptorPool * RenderLayouts::kMaterialTextureSlots };
        vk::DescriptorPoolCreateInfo poolInfo{
            .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
            .maxSets = kMaterialsPerDescriptorPool, .poolSizeCount = 1, .pPoolSizes = &poolSize };
        m_materialDescriptorPools.emplace_back(m_context.GetDevice(), poolInfo);
        m_materialsInCurrentPool = 0;

    }   // RenderResources::CreateMaterialDescriptorPool()

    void RenderResources::WriteMaterialDescriptorSet(vk::DescriptorSet set, const MaterialData& data) {

        static_assert(MaterialShaderParams::kMaxTextures == RenderLayouts::kMaterialTextureSlots,
                      "a shader's texture list fills the material set's slots");
        constexpr uint32_t kSlots = RenderLayouts::kMaterialTextureSlots;

        // A slot's texture, or its placeholder when the slot is empty (a destroyed texture counts
        // as missing).
        const auto viewOf = [&](TextureHandle handle, TextureHandle fallback) -> vk::ImageView {
            return *GetTextureOrThrow(IsTextureUsable(handle) ? handle : fallback).view;
        };

        std::array<vk::ImageView, kSlots> views;
        const MaterialShaderParams& list = ParamsOf(data.shader);
        if (list.textures.empty()) {
            // The built-in slots. Binding order must match RenderLayouts::
            // CreateMaterialDescriptorSetLayout AND DefaultSurface.slang's [[vk::binding(n,1)]]
            // declarations exactly: 0=albedo, 1=normal, 2=metallicRoughness, 3=emissive,
            // 4=occlusion. The other three are unused.
            views.fill(viewOf(TextureHandle{}, m_defaultWhiteTexture));
            views[0] = viewOf(data.albedo,            m_defaultWhiteTexture);
            views[1] = viewOf(data.normal,            m_defaultNormalTexture);
            views[2] = viewOf(data.metallicRoughness, m_defaultWhiteTexture);
            views[3] = viewOf(data.emissive,          m_defaultWhiteTexture);
            views[4] = viewOf(data.occlusion,         m_defaultWhiteTexture);
        } else {
            // The shader's own textures, slot = position in its list (MATERIAL_TEXTURE(slot, name)
            // in the shader). An empty slot shows the default its list names.
            for (uint32_t i = 0; i < kSlots; ++i) {
                TextureHandle fallback = m_defaultWhiteTexture;
                if (i < list.textures.size()) {
                    switch (list.textures[i].fallback) {
                        case MaterialTextureDefault::Black:  fallback = m_defaultBlackTexture;  break;
                        case MaterialTextureDefault::Normal: fallback = m_defaultNormalTexture; break;
                        default: break;
                    }
                }
                const bool hasSlot = i < list.textures.size() && i < data.shaderTextures.size();
                views[i] = viewOf(hasSlot ? data.shaderTextures[i] : TextureHandle{}, fallback);
            }
        }

        std::array<vk::DescriptorImageInfo, kSlots> imageInfos;
        std::array<vk::WriteDescriptorSet, kSlots>  writes;
        for (uint32_t i = 0; i < kSlots; ++i) {
            imageInfos[i] = vk::DescriptorImageInfo{
                .sampler = *m_materialSampler, .imageView = views[i], .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
            writes[i] = vk::WriteDescriptorSet{
                .dstSet = set, .dstBinding = i, .dstArrayElement = 0,
                .descriptorCount = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .pImageInfo = &imageInfos[i]
            };
        }
        m_context.GetDevice().updateDescriptorSets(writes, {});

    }   // RenderResources::WriteMaterialDescriptorSet()

    // ------------------------------------------------------------------------------------------------------------- //
    //  MATERIALS
    // ------------------------------------------------------------------------------------------------------------- //

    PushConstants MakeMaterialPush(const MaterialData& data) {

        const auto toLinear = [](const glm::vec3& srgb) { return glm::pow(glm::clamp(srgb, glm::vec3(0.0f), glm::vec3(1.0f)), glm::vec3(2.2f)); };
        PushConstants push;
        push.factors     = data.factors;
        push.alphaCutoff = std::clamp(data.alphaCutoff, 0.0f, 1.0f);
        push.flags       = static_cast<uint32_t>(data.shading) & 3u;
        if (data.toon.ownEdge)        { push.flags |= kMaterialFlagOwnEdge; }
        if (data.toon.ownShadeColour) { push.flags |= kMaterialFlagOwnShade; }
        push.toonEdge  = glm::vec4(std::clamp(data.toon.threshold, 0.0f, 0.95f), std::clamp(data.toon.softness, 0.0f, 1.0f), 0.0f, 0.0f);
        push.toonShade = glm::vec4(toLinear(data.toon.shadeColour), 1.0f);
        push.toonLit   = glm::vec4(toLinear(data.toon.litColour), 1.0f);
        push.outline   = std::clamp(data.toon.outline, 0.0f, 1.0f);
        return push;

    }   // MakeMaterialPush()

    void RenderResources::DrawDepthBatches(vk::CommandBuffer cmd, const std::vector<DrawBatch>& batches, const DepthPassPipelines& p) const {

        bool              cutoutBound = false;
        vk::CullModeFlags cullBound   = p.cull;
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, p.solid);
        if (p.dynamicCull) { cmd.setCullMode(cullBound); }
        bool mirroredBound = false;
        if (p.dynamicFrontFace) { cmd.setFrontFace(vk::FrontFace::eCounterClockwise); }

        MeshHandle     boundMesh;
        MaterialHandle boundMaterial;
        uint32_t       indexCount = 0;
        for (const DrawBatch& batch : batches) {

            if (p.dynamicFrontFace && batch.mirrored != mirroredBound) {
                cmd.setFrontFace(batch.mirrored ? vk::FrontFace::eClockwise : vk::FrontFace::eCounterClockwise);
                mirroredBound = batch.mirrored;
            }

            if (!(batch.material == boundMaterial)) {
                const MaterialData* data = IsMaterialUsable(batch.material)   // destroyed: plain solid depth
                                         ? &m_materials[batch.material.id].data : nullptr;
                const bool cutout = data && data->alphaCutoff > 0.0f;
                if (cutout != cutoutBound) {
                    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, cutout ? p.cutout : p.solid);   // same layout: sets stay bound
                    cutoutBound = cutout;
                }
                if (cutout) {
                    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, p.layout, 1, *m_materials[batch.material.id].descriptorSet, nullptr);
                    const CutoutPush push{ std::clamp(data->alphaCutoff, 0.0f, 1.0f), data->factors.albedoTint.a };
                    cmd.pushConstants(p.layout, p.cutoutPushStages, p.cutoutPushOffset, sizeof(CutoutPush), &push);
                }
                const vk::CullModeFlags cull = data && data->doubleSided ? vk::CullModeFlags(vk::CullModeFlagBits::eNone) : p.cull;
                if (p.dynamicCull && cull != cullBound) { cmd.setCullMode(cull); cullBound = cull; }
                boundMaterial = batch.material;
            }

            if (!(batch.mesh == boundMesh)) {
                indexCount = BindMesh(cmd, batch.mesh);
                boundMesh  = batch.mesh;
            }
            if (indexCount != 0) { cmd.drawIndexed(indexCount, batch.instanceCount, 0, 0, batch.firstInstance); }
        }

    }   // RenderResources::DrawDepthBatches()

    MaterialHandle RenderResources::CreateMaterial(const MaterialData& data) {

        // A VkDescriptorPool's maxSets is fixed at creation and can't grow, so once the current
        // block is full (or there isn't one yet, on the very first call) allocate a fresh block
        // instead of ever trying to over-allocate from a full pool - that would throw a raw
        // vk::OutOfPoolMemoryError with no indication of what actually went wrong. This makes
        // material count effectively unbounded (limited only by actual GPU memory/descriptor
        // budget, same as every other resource here), in blocks of kMaterialsPerDescriptorPool.
        if (m_materialDescriptorPools.empty() || m_materialsInCurrentPool >= kMaterialsPerDescriptorPool) {
            CreateMaterialDescriptorPool();
        }

        vk::DescriptorSetLayout layout = m_materialSetLayout;
        vk::DescriptorSetAllocateInfo allocInfo{
            .descriptorPool = m_materialDescriptorPools.back(),
            .descriptorSetCount = 1,
            .pSetLayouts = &layout
        };
        std::vector<vk::raii::DescriptorSet> sets = m_context.GetDevice().allocateDescriptorSets(allocInfo);
        ++m_materialsInCurrentPool;

        GpuMaterial mat;
        mat.data = data;
        mat.push = MakeMaterialPush(data);
        ApplyShaderParams(mat);   // its range in the parameter buffer, and push.paramOffset
        mat.descriptorSet = std::move(sets.front());
        WriteMaterialDescriptorSet(*mat.descriptorSet, data);

        const MaterialHandle handle{ static_cast<uint32_t>(m_materials.size()) };
        m_materials.push_back(std::move(mat));
        return handle;

    }   // RenderResources::CreateMaterial()

    void RenderResources::UpdateMaterial(MaterialHandle handle, const MaterialData& data) {

        if (!handle.IsValid() || handle.id >= m_materials.size()) {
            throw std::runtime_error("[Renderer] Unknown or invalid MaterialHandle.");
        }
        GpuMaterial& mat = m_materials[handle.id];
        if (mat.destroyed) { return; }   // freed (no scene used it): nothing to update

        // What is bound in its texture set: the five built-in textures, or its shader's own - and
        // which of the two, so a change of shader counts as well.
        const bool texturesChanged =
            !(mat.data.albedo == data.albedo) || !(mat.data.normal == data.normal) ||
            !(mat.data.metallicRoughness == data.metallicRoughness) ||
            !(mat.data.emissive == data.emissive) || !(mat.data.occlusion == data.occlusion) ||
            !(mat.data.shader == data.shader) || mat.data.shaderTextures != data.shaderTextures;

        mat.data = data;
        mat.push = MakeMaterialPush(data);
        // The shader's own parameters: a write into the parameter buffer (uploaded per frame), so
        // dragging one of their sliders needs no GPU synchronization either.
        ApplyShaderParams(mat);

        if (texturesChanged) {
            // A command buffer from a still-in-flight frame may currently be executing a draw
            // that references this exact descriptor set's OLD image bindings; rewriting it while
            // that's true is undefined behaviour. This wait-idle makes the swap safe at the cost
            // of a full GPU stall - acceptable for an editor-triggered "designer changed a
            // texture" edit (rare, human-paced), not something to do from a hot gameplay path.
            // (Factor-only edits, the common case for a live-tweaked slider, skip this entirely -
            // see the early-continue-free structure above: texturesChanged is false and we fall
            // straight through without ever calling WriteMaterialDescriptorSet.)
            m_context.GetDevice().waitIdle();
            WriteMaterialDescriptorSet(*mat.descriptorSet, data);
        }

    }   // RenderResources::UpdateMaterial()

    const MaterialShaderParams& RenderResources::ParamsOf(MaterialShaderHandle shader) const {

        static const MaterialShaderParams none;
        return m_shaders ? m_shaders->Params(shader) : none;

    }   // RenderResources::ParamsOf()

    void RenderResources::ApplyShaderParams(GpuMaterial& mat) {

        const MaterialShaderParams& list = ParamsOf(mat.data.shader);
        const uint32_t count = static_cast<uint32_t>(list.params.size());

        // A range of the right size: only when the number of parameters changes (a new
        // material, another shader, a longer list).
        if (count != mat.paramCount) {
            m_materialParams.Free(mat.paramOffset, mat.paramCount);
            mat.paramOffset = m_materialParams.Allocate(count);
            mat.paramCount  = mat.paramOffset != MaterialParamBuffer::kNone ? count : 0;
            if (count != 0 && mat.paramCount == 0 && !m_paramsFullReported) {
                m_paramsFullReported = true;
                CRU_ERROR("[Renderer] The material parameter buffer is full ({} values): materials made from now on get "
                          "wrong shader parameters. Raise MaterialParamBuffer::kCapacity.", MaterialParamBuffer::kCapacity);
            }
        }

        // The material's values, the list's defaults for those it does not have.
        if (mat.paramCount != 0) {
            m_paramScratch.resize(count);
            for (uint32_t i = 0; i < count; ++i) {
                m_paramScratch[i] = i < mat.data.params.size() ? mat.data.params[i] : list.params[i].defaultValue;
            }
            m_materialParams.Write(mat.paramOffset, m_paramScratch);
        }
        mat.push.paramOffset = mat.paramCount != 0 ? mat.paramOffset : 0;

    }   // RenderResources::ApplyShaderParams()

    void RenderResources::OnShaderParamsChanged(MaterialShaderHandle shader, const MaterialShaderParams& before) {

        const MaterialShaderParams& now = ParamsOf(shader);
        const bool texturesChanged = !(before.textures == now.textures);

        std::vector<GpuMaterial*> affected;
        for (GpuMaterial& mat : m_materials) {
            if (mat.destroyed || !(mat.data.shader == shader)) { continue; }

            // Its values are kept by slot: carried over by name to where the new list has them.
            RemapMaterialShaderValues(before, now, mat.data.params, mat.data.shaderTextures);
            ApplyShaderParams(mat);
            affected.push_back(&mat);
        }

        // The texture sets are read by frames still in flight: wait for them once, as
        // UpdateMaterial does for a texture change.
        if (texturesChanged && !affected.empty()) {
            m_context.GetDevice().waitIdle();
            for (GpuMaterial* mat : affected) { WriteMaterialDescriptorSet(*mat->descriptorSet, mat->data); }
        }

    }   // RenderResources::OnShaderParamsChanged()

    const MaterialData& RenderResources::GetMaterialData(MaterialHandle handle) const {

        if (!handle.IsValid() || handle.id >= m_materials.size()) {
            throw std::runtime_error("[Renderer] Unknown or invalid MaterialHandle.");
        }
        return m_materials[handle.id].data;

    }   // RenderResources::GetMaterialData()

    void RenderResources::InitMaterials(vk::DescriptorSetLayout materialSetLayout, const MaterialShaders& shaders) {

        m_materialSetLayout = materialSetLayout;
        m_shaders = &shaders;
        CreateMaterialSampler();
        CreateMaterialDescriptorPool();
        CreateDefaultTextures();
        m_defaultMaterial = CreateMaterial(MaterialData{});   // Invalid textures -> both placeholders, neutral factors

    }   // RenderResources::InitMaterials()

    // ------------------------------------------------------------------------------------------------------------- //
    //  DESTROYING
    // ------------------------------------------------------------------------------------------------------------- //

    bool RenderResources::DestroyMesh(MeshHandle handle) {

        if (!handle.IsValid() || handle.id >= m_meshes.size()) { return false; }
        GpuMesh& mesh = m_meshes[handle.id];
        if (mesh.dynamic) { return false; }   // owned by whoever made it (e.g. vines), not the ResourceManager

        // Still waiting for its upload (CreateMeshAsync): drop the request - there is nothing to free yet.
        std::erase_if(m_pendingMeshUploads, [&](const PendingMeshUpload& p) { return p.handle.id == handle.id; });

        m_retiredMeshes.push_back({ m_releaseCount, std::move(mesh) });
        mesh = GpuMesh{};                     // indexCount 0: IsDrawable / BindMesh / bounds skip it from now on
        mesh.debugName = "(destroyed)";
        return true;

    }   // RenderResources::DestroyMesh()

    bool RenderResources::DestroyTexture(TextureHandle handle) {

        if (!IsTextureUsable(handle) || handle == m_defaultWhiteTexture || handle == m_defaultNormalTexture) { return false; }

        // Forget its cache key(s), so asking for the same file again makes a new texture.
        std::erase_if(m_textureCacheByKey, [&](const auto& entry) { return entry.second == handle.id; });

        GpuTexture& texture = m_textures[handle.id];
        m_retiredTextures.push_back({ m_releaseCount, std::move(texture) });
        texture = GpuTexture{};
        texture.destroyed = true;             // IsTextureUsable false: passes draw the default instead
        return true;

    }   // RenderResources::DestroyTexture()

    bool RenderResources::DestroyMaterial(MaterialHandle handle) {

        if (!IsMaterialUsable(handle) || handle == m_defaultMaterial) { return false; }

        GpuMaterial& material = m_materials[handle.id];
        m_materialParams.Free(material.paramOffset, material.paramCount);   // frames in flight have their own copy of the values
        m_retiredMaterials.push_back({ m_releaseCount, std::move(material) });
        material = GpuMaterial{};
        material.destroyed = true;            // IsMaterialUsable false: draws use the default material instead
        return true;

    }   // RenderResources::DestroyMaterial()

    void RenderResources::ReleaseDestroyed() {

        ++m_releaseCount;

        // Destroyed at count c (during frame n): frames up to n-1 may still be on the GPU. Each later
        // call has waited for one more frame's fence, so after MAX_FRAMES_IN_FLIGHT + 1 more calls
        // every one of them has finished - one frame of margin on top of what is strictly needed.
        const auto finished = [&](const auto& retired) {
            return m_releaseCount - retired.frame > VulkanFrameManager::MAX_FRAMES_IN_FLIGHT;
        };
        std::erase_if(m_retiredMeshes, finished);
        std::erase_if(m_retiredTextures, finished);
        std::erase_if(m_retiredMaterials, finished);

    }   // RenderResources::ReleaseDestroyed()

}   // NAMESPACE CRUCIBLE
