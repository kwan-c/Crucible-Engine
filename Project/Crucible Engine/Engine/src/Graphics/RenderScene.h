/*************************************************************************
  \file       RenderScene.h
  \brief
    What there is to draw: the objects the game has registered (render
    proxies) and the spatial index they sit in, plus the per-frame work of
    finding what each view can see and turning it into instanced draws.
  \details
    A proxy is the renderer's own record of one drawable object: its mesh,
    material and transform. Each frame, per view, the scene asks the spatial
    index which proxies are inside the camera's view, sorts them so objects
    sharing a mesh and material sit together, and writes one batch per run.
    The same is done per shadow cascade. Also answers ray picks. Moved out
    of the Renderer class, whose Submit / CreateRenderProxy / SetRenderProxy
    / DestroyRenderProxy / Raycast forward here.
*************************************************************************/
#pragma once

#include "Vulkan/VulkanContext.h"
#include "Vulkan/VulkanResource.h"
#include "RenderResources.h"
#include "RenderTypes.h"
#include "DrawList.h"
#include "Spatial/SpatialIndex.h"

#include <cstdint>
#include <vector>

namespace Crucible {

    class Camera;

    class RenderScene {
    public:
        /*********************************************************************
         \param[in] context    Used to create the instance buffers. Must outlive this object.
         \param[in] resources  Mesh bounds and geometry, and the default material.
         *********************************************************************/
        RenderScene(VulkanContext& context, const RenderResources& resources) : m_context(context), m_resources(resources) {}

        RenderScene(const RenderScene&) = delete;
        RenderScene& operator=(const RenderScene&) = delete;

        /// Creates one instance buffer per frame in flight. Call once.
        void Init(uint32_t framesInFlight);

        // ---- What to draw (see the Renderer functions these forward from) ----
        void Submit(MeshHandle mesh, MaterialHandle material, const glm::mat4& model, const glm::vec4& params);
        RenderProxyHandle CreateProxy(std::uint64_t userData);
        void SetProxy(RenderProxyHandle handle, MeshHandle mesh, MaterialHandle material, const glm::mat4& model, bool visible, const glm::vec4& params);
        void DestroyProxy(RenderProxyHandle handle);
        /// Which object gets the selection outline (0 = none). Matched against proxies' userData.
        void SetOutlinedObject(std::uint64_t userData);

        // ---- Once a frame ----
        /// Gives proxies whose mesh was still uploading another try at getting their bounds.
        void RetryPendingBounds();
        /// Draws every proxy's box with DebugDraw: settled = blue, moving = orange.
        void DrawBoundsDebug() const;
        /// Forgets the immediate Submit() items. Call once the frame has been drawn.
        void ClearSubmitted() { m_renderQueue.clear(); }
        /// Lets the spatial index settle objects that stopped moving. Call at the end of the frame.
        void EndFrame() { m_spatial.EndFrame(); }

        // ---- Per view ----
        /// Empties the frame's instance list. Call before the frame's first view.
        void BeginViews() { m_instanceScratch.clear(); }
        /*********************************************************************
         \brief  Finds what `camera` can see (proxies through the spatial
                 index, immediate items by brute force), sorts it by
                 (material, mesh) and appends one batch per run.
         \param[out] stats  Reset, then filled with the culling counts.
         \param[out] out    Receives the view's main-pass batches.
         *********************************************************************/
        void BuildMainBatches(const Camera& camera, RenderCullStats& stats, std::vector<DrawBatch>& out);
        /*********************************************************************
         \brief  The same for one shadow cascade: everything inside the
                 cascade's box, batched by mesh alone.
         \param[in] viewProj   The cascade's view-projection.
         \param[out] out       Receives the cascade's batches.
         \param[in,out] stats  shadowCasters and shadowDrawCalls are added to.
         *********************************************************************/
        void BuildShadowBatches(const glm::mat4& viewProj, std::vector<DrawBatch>& out, RenderCullStats& stats);
        /// Appends the selected object's batch, if there is one and it is visible.
        void BuildOutlineBatches(std::vector<DrawBatch>& out);

        /*********************************************************************
         \brief  Copies every view's instances into this frame's buffer.
         \return true if the buffer had to be replaced by a bigger one, in
                 which case every descriptor set bound to it (set 0, binding 2)
                 must be pointed at InstanceBuffer(frameIndex) again.
         *********************************************************************/
        bool UploadInstances(uint32_t frameIndex);
        vk::Buffer InstanceBuffer(size_t frame) const { return *m_instanceBuffers[frame].GetBuffer(); }
        size_t     InstanceBufferCount() const        { return m_instanceBuffers.size(); }

        // ---- Queries ----
        bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RenderRaycastHit& outHit) const;
        const SpatialIndex& Spatial() const { return m_spatial; }

    private:
        // Re-derives a proxy's world box and (re)inserts/updates/removes it in the spatial index.
        void RefreshProxyBounds(uint32_t proxyId);
        // Fills m_visibleItems with what `camera` can see, sorted by (material, mesh), and
        // records the counts in `stats`.
        void BuildVisibleItems(const Camera& camera, RenderCullStats& stats);
        // Appends `items` (already sorted) as batches: consecutive items with the same mesh (and
        // material, if byMaterial) become one instanced draw. Returns the number of instances.
        uint32_t AppendBatches(const std::vector<RenderItem>& items, std::vector<DrawBatch>& batches, bool byMaterial);
        // The selected proxy's id, or 0xFFFFFFFF (re-finds it if the cached id went stale).
        uint32_t ResolveOutlineProxy();

        VulkanContext&         m_context;
        const RenderResources& m_resources;

        std::vector<RenderItem>     m_renderQueue;      // immediate Submit() items, this frame only

        // Persistent render proxies + the spatial index they live in (see CreateRenderProxy).
        struct RenderProxy {
            MeshHandle           mesh;
            MaterialHandle       material;
            glm::mat4            model{ 1.0f };
            glm::vec4            params{ 1.0f, 0.0f, 0.0f, 0.0f };   // InstanceData::params
            std::uint64_t        userData = 0;
            SpatialIndex::Handle spatial = SpatialIndex::kInvalid;   // kInvalid = not in the index (hidden / no bounds yet)
            bool                 visible = false;
            bool                 alive = false;
            bool                 awaitingBounds = false;              // mesh still uploading (CreateMeshAsync)
        };
        std::vector<RenderProxy>    m_proxies;
        std::vector<uint32_t>       m_freeProxies;
        std::vector<uint32_t>       m_proxiesAwaitingBounds;
        SpatialIndex                m_spatial{ 0.25f };
        std::vector<RenderItem>     m_visibleItems;     // scratch: one view's culled + sorted draw list
        std::vector<RenderItem>     m_casterItems;      // scratch: one shadow cascade's casters

        // Selection outline (SetOutlinedObject).
        std::uint64_t               m_outlineUserData = 0;
        uint32_t                    m_outlineProxy = 0xFFFFFFFFu;     // cached lookup of m_outlineUserData

        // Instances: every object drawn this frame by every pass of every view, one model matrix
        // each (set 0, binding 2). Built on the CPU as each view is prepared, copied in one go by
        // UploadInstances() into this frame's host-visible buffer, which grows (power of two) as
        // needed. Shared by the window's and every viewport's set 0 for that frame index.
        std::vector<InstanceData>                   m_instanceScratch;
        std::vector<VulkanBuffer>                   m_instanceBuffers;
        std::vector<void*>                          m_instanceBuffersMapped;
        std::vector<uint32_t>                       m_instanceCapacity;     // in instances
        static constexpr uint32_t                   kInitialInstanceCapacity = 16384;
    };

}   // NAMESPACE CRUCIBLE
