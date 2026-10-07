/*************************************************************************
  \file       RenderResources.h
  \brief
    The GPU resources the renderer draws with, and the handles that name
    them: meshes (vertex and index buffers, names, bounds, the CPU copy used
    for picking), textures, and materials (five textures plus their factors).
  \details
    Moved out of the Renderer class so render passes (Graphics/Passes/) can
    draw meshes without going through the Renderer. Game and editor code
    still uses the Renderer's functions (CreateMesh, GetMeshName, ...),
    which forward here.
*************************************************************************/
#pragma once

#include "Vulkan/VulkanContext.h"
#include "Vulkan/VulkanResource.h"
#include "Vulkan/VulkanUploadQueue.h"
#include "RenderTypes.h"
#include "DrawList.h"
#include "MeshHandle.h"
#include "MaterialHandle.h"
#include "TextureHandle.h"

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace Crucible {

    // GPU-side mesh. One vertex + one index buffer per mesh for now (simple; suballocate from
    // big shared buffers later if mesh counts get large).
    struct GpuMesh {
        VulkanBuffer vertexBuffer;
        VulkanBuffer indexBuffer;
        uint32_t     indexCount = 0;

        // Human-readable label, purely for tools (Editor UI, RenderDoc/Nsight captures via a
        // future debug-object-name call, log messages) - never used to look a mesh up, and
        // never required to be unique. See Renderer::GetMeshName()'s doc comment for who's meant to
        // read this and why it lives here rather than in the Editor.
        std::string  debugName;

        // Object-space bounding box of the vertex positions (computed at upload), for
        // selection boxes and future culling - see Renderer::GetMeshBounds().
        glm::vec3    boundsMin{ 0.0f };
        glm::vec3    boundsMax{ 0.0f };

        // CPU copy of the geometry, for exact ray picking (Raycast). Positions + indices only:
        // 12 bytes per vertex + 4 per index, per MESH (instances share it).
        std::vector<glm::vec3> cpuPositions;
        std::vector<uint32_t>  cpuIndices;
    };

    // GPU-side texture: an image + its view, indexed by TextureHandle. No per-texture
    // sampler - every material texture is sampled through the one sampler RenderResources shares between them
    // (a repeat-wrap trilinear-ish sampler is fine for every texture kind we load today).
    struct GpuTexture {
        VulkanImage         image;
        vk::raii::ImageView view = nullptr;
        uint32_t            width = 0;
        uint32_t            height = 0;
    };

    // GPU-side material: which texture each of the five PBR slots currently points at (kept
    // around so UpdateMaterial can tell whether a texture binding actually changed, and so
    // the Inspector can read back what a material is set to), the scalar factors (pushed per
    // draw - see PushConstants), and the one descriptor set (layout set = 1) those five
    // textures are bound into.
    struct GpuMaterial {
        MaterialData             data;
        vk::raii::DescriptorSet   descriptorSet = nullptr;
    };

    class RenderResources {
    public:
        /*********************************************************************
         \param[in] context  Used to create buffers. Must outlive this object.
         \param[in] uploads  The queue that copies mesh data to the GPU at the
                            start of a frame. Must outlive this object.
         *********************************************************************/
        RenderResources(VulkanContext& context, VulkanUploadQueue& uploads) : m_context(context), m_uploads(uploads) {}

        RenderResources(const RenderResources&) = delete;
        RenderResources& operator=(const RenderResources&) = delete;

        // ---- Meshes (see the Renderer functions of the same names for the full notes) ----

        /// Creates a mesh's buffers now and queues their contents for upload.
        MeshHandle CreateMesh(const MeshData& data, const std::string& debugName = {});
        /// Returns a handle now; the buffers are created a few meshes per frame.
        MeshHandle CreateMeshAsync(MeshData data, const std::string& debugName = {});
        /// Creates the buffers of up to kMaxMeshUploadsPerFrame waiting meshes. Call once a frame.
        void ProcessPendingMeshUploads();

        bool GetMeshBounds(MeshHandle handle, glm::vec3& outMin, glm::vec3& outMax) const;
        const std::string& GetMeshName(MeshHandle handle) const;

        /// Every mesh, indexed by MeshHandle::id. One with indexCount == 0 is still waiting
        /// for its upload and cannot be drawn yet.
        const std::vector<GpuMesh>& Meshes() const { return m_meshes; }

        // ---- Textures and materials (see the Renderer functions of the same names) ----

        /*********************************************************************
         \brief  Creates the shared material sampler, the first block of
                 material descriptor sets, the two placeholder textures and
                 the default material. Call once, before any material is made.
         \param[in] materialSetLayout  Layout of the mesh pipeline's set 1
                                      (the five material textures).
         *********************************************************************/
        void InitMaterials(vk::DescriptorSetLayout materialSetLayout);

        TextureHandle CreateTexture(const std::string& path, bool isSRGB);
        TextureHandle CreateTextureFromMemory(const unsigned char* fileBytes, size_t byteCount, bool isSRGB, const std::string& cacheKey);

        MaterialHandle      CreateMaterial(const MaterialData& data);
        void                UpdateMaterial(MaterialHandle handle, const MaterialData& data);
        const MaterialData& GetMaterialData(MaterialHandle handle) const;

        /// Every texture, indexed by TextureHandle::id, and every material, by MaterialHandle::id.
        const std::vector<GpuTexture>&  Textures() const  { return m_textures; }
        const std::vector<GpuMaterial>& Materials() const { return m_materials; }
        /// The texture behind a handle. Throws std::runtime_error if the handle is not one of ours.
        const GpuTexture& GetTextureOrThrow(TextureHandle handle) const;

        TextureHandle  DefaultWhiteTexture() const  { return m_defaultWhiteTexture; }    ///< 1x1 white: stands in for a missing colour texture.
        TextureHandle  DefaultNormalTexture() const { return m_defaultNormalTexture; }   ///< 1x1 flat normal.
        MaterialHandle DefaultMaterial() const      { return m_defaultMaterial; }        ///< Placeholders and neutral factors.

        /*********************************************************************
         \brief  Draws a list of batches with whatever pipeline and descriptor
                 sets are already bound: geometry only, no materials.
         \param[in] cmd      Command buffer, inside a rendering pass.
         \param[in] batches  Meshes to draw; each must be uploaded.
         *********************************************************************/
        void DrawBatches(vk::CommandBuffer cmd, const std::vector<DrawBatch>& batches) const;

    private:
        // ---- Texture/Material helpers ----
        // Shared core of every texture-creation path: creates a new device-local image + view
        // for already-decoded RGBA8 pixels and queues the copy into it (the upload queue). Format is
        // eR8G8B8A8Srgb or eUnorm depending on isSRGB - see CreateTexture's doc comment on when
        // each is correct.
        TextureHandle UploadTexturePixels(const unsigned char* pixels, uint32_t width, uint32_t height, bool isSRGB);
        // 1x1 solid-color texture, used by CreateDefaultTextures() and directly reusable for any
        // other flat-color placeholder need.
        TextureHandle CreateSolidColorTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool isSRGB);
        void CreateDefaultTextures();
        void CreateMaterialSampler();
        // Allocates ONE new descriptor pool block (kMaterialsPerDescriptorPool materials' worth)
        // and appends it to m_materialDescriptorPools. Called once up front for the first block,
        // and again by CreateMaterial() whenever the current block is full - see that function.
        void CreateMaterialDescriptorPool();
        // Writes all five combined-image-sampler bindings of `set` from `data`, substituting
        // m_defaultWhiteTexture/m_defaultNormalTexture for any Invalid handle in `data`.
        void WriteMaterialDescriptorSet(vk::DescriptorSet set, const MaterialData& data);

        void UploadMesh(MeshHandle handle, const MeshData& data, const std::string& debugName);

        VulkanContext&     m_context;
        VulkanUploadQueue& m_uploads;

        std::vector<GpuMesh> m_meshes;

        // Textures + Materials (see the Renderer's "---- Textures ----"/"---- Materials ----" public API).
        std::vector<GpuTexture>                     m_textures;
        std::unordered_map<std::string, uint32_t>   m_textureCacheByKey;   // path or explicit cacheKey -> TextureHandle::id
        vk::raii::Sampler                           m_materialSampler = nullptr;

        // Pool(s) BEFORE the sets they allocate (the Renderer's own pools follow the same rule): members are destroyed in REVERSE declaration order, so declaring the pools
        // first means they are destroyed LAST - after every GpuMaterial's vk::raii::DescriptorSet
        // has already run its own destructor (which frees itself against whichever pool allocated
        // it). The opposite order compiles fine but destroys a pool while sets allocated from it
        // still exist, which is exactly the "Couldn't find VkDescriptorPool"/"Invalid
        // VkDescriptorSet" validation errors this fixed once already - see the fix's own commit
        // for the exact symptom if this ever regresses.
        //
        // A VkDescriptorPool can't be resized once created, so unbounded material counts mean
        // unbounded POOLS, not one pool sized "big enough": m_materials.size() used to be capped
        // hard at a single pool's fixed maxSets (kMaterialsPerDescriptorPool), and CreateMaterial
        // had no check for that at all - the (kMaterialsPerDescriptorPool + 1)-th material would
        // throw a raw vk::OutOfPoolMemoryError with no indication of what actually went wrong.
        // CreateMaterial now allocates a fresh pool block on demand instead (see its own comment),
        // so every pool after the first is a plain vector growth, same as any other resource here.
        std::vector<vk::raii::DescriptorPool>       m_materialDescriptorPools;
        uint32_t                                    m_materialsInCurrentPool = 0;   // toward the current (last) pool's cap
        std::vector<GpuMaterial>                    m_materials;
        static constexpr uint32_t                   kMaterialsPerDescriptorPool = 256;

        TextureHandle                               m_defaultWhiteTexture;    // (255,255,255,255)
        TextureHandle                               m_defaultNormalTexture;   // flat tangent-space normal (128,128,255,255)
        MaterialHandle                              m_defaultMaterial;
        vk::DescriptorSetLayout                     m_materialSetLayout;      // set 1 of the mesh pipeline (InitMaterials)

        // Pending async mesh uploads (see CreateMeshAsync).
        struct PendingMeshUpload { MeshHandle handle; MeshData data; std::string debugName; };
        std::deque<PendingMeshUpload> m_pendingMeshUploads;
        static constexpr uint32_t     kMaxMeshUploadsPerFrame = 2;
    };

}   // NAMESPACE CRUCIBLE
