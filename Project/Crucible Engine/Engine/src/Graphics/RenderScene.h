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

    Threads: BuildViewBatches spreads one view's culling, sorting and
    instance writing over the Renderer's WorkerPool (how is described at
    BuildViewBatches). Everything else runs on the calling thread, and
    nothing may change the proxies while a view is being built.
*************************************************************************/
#pragma once

#include "Vulkan/VulkanContext.h"
#include "Vulkan/VulkanResource.h"
#include "RenderResources.h"
#include "RenderTypes.h"
#include "DrawList.h"
#include "Spatial/SpatialIndex.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Crucible {

    class Camera;
    class WorkerPool;

    class RenderScene {
    public:
        /*********************************************************************
         \param[in] context    Used to create the instance buffers. Must outlive this object.
         \param[in] resources  Mesh bounds and geometry, and the default material.
         \param[in] workers    Threads the per-view culling and batching run on. Must outlive this object.
         *********************************************************************/
        RenderScene(VulkanContext& context, const RenderResources& resources, WorkerPool& workers)
            : m_context(context), m_resources(resources), m_workers(workers) {}

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
        /// Several objects outlined at once (multi-selection). Zeros are ignored; empty = none.
        void SetOutlinedObjects(const std::vector<std::uint64_t>& userData);

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
        void BeginViews();
        void SetProxyAnimation(RenderProxyHandle handle, std::span<const glm::mat4> palette,
                               const glm::vec3& boundsMin, const glm::vec3& boundsMax);
        /*********************************************************************
         \brief  Everything one view draws from the scene: what its camera
                 sees, sorted by (material, mesh), and what each shadow
                 cascade's box contains, sorted by mesh (depth only). Appends
                 their instances (main pass first, then each cascade, so the
                 instance order is the same however the work was split) and
                 one batch per run of equal mesh / material.
         \details Spread over the worker threads in three steps:
                   1. Cull: the spatial index's tree (settled objects) is
                      split into a few parts, one task per part and pass;
                      the moving objects' flat array is cut into chunks, each
                      box tested against every pass at once. Hits are kept
                      as compact (sort key, id) pairs.
                   2. Per pass: group the hits by key (a counting sort - a
                      view has few distinct mesh / material pairs), in a
                      fixed order, so the result never depends on timing.
                   3. Copy every pass's instances into the frame's instance
                      list, in equal chunks across the threads.
                 Small scenes skip the threads (waking them would cost more).
         \param[in] camera           The view's camera.
         \param[in] cascadeViewProj  Each shadow cascade's view-projection (empty = no shadows).
         \param[out] stats           Reset, then filled with the culling counts (all but drawCalls).
         \param[out] mainOut         Receives the main pass's batches.
         \param[out] shadowOut       One batch list per cascade, as many as cascadeViewProj.
         *********************************************************************/
        void BuildViewBatches(const Camera& camera, std::span<const glm::mat4> cascadeViewProj, RenderCullStats& stats,
                              std::vector<DrawBatch>& mainOut, std::span<std::vector<DrawBatch>> shadowOut);
        /// Appends the selected objects' batches (those that have a proxy and are visible).
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
        bool GetObjectBounds(std::uint64_t userData, glm::vec3& boundsMin, glm::vec3& boundsMax) const;
        bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RenderRaycastHit& outHit) const;
        const SpatialIndex& Spatial() const { return m_spatial; }

    private:
        // Re-derives a proxy's world box and (re)inserts/updates/removes it in the spatial index.
        void RefreshProxyBounds(uint32_t proxyId);
        // Appends `items` (already sorted) as batches: consecutive items with the same mesh (and
        // material, if byMaterial) become one instanced draw. Returns the number of instances.
        uint32_t AppendBatches(const std::vector<RenderItem>& items, std::vector<DrawBatch>& batches, bool byMaterial);
        // Brings m_outlineProxies up to date (re-finds any cached id that went stale).
        void ResolveOutlineProxies();
        // Whether a mesh can be drawn now (valid and uploaded).
        bool IsDrawable(MeshHandle mesh) const;
        // The material a main-pass draw binds: the item's own, or the default one if it has none.
        MaterialHandle ResolveMaterial(MaterialHandle material) const;
        // Sort key in the depth-only passes: the mesh, plus the material when it is cut-out or
        // double-sided (as id + 1 in the high half; 0 = merge with every other material).
        uint64_t DepthKey(MeshHandle mesh, MaterialHandle material) const;
        // Makes room for `count` more instances this frame and returns the index of the first. The
        // list keeps its size between frames, so it only fills new entries when it has to grow.
        uint32_t AllocateInstances(uint32_t count);
        // fn(i) for i in [0, count): on the worker threads when `parallel`, else here.
        template <typename Fn> void RunTasks(bool parallel, uint32_t count, Fn&& fn);
        // BuildViewBatches step 2 for one pass: groups its hits by key into m_pass[pass].
        void GroupPass(uint32_t pass, uint32_t treeParts, uint32_t chunks);
        // The selected proxy's id, or 0xFFFFFFFF (re-finds it if the cached id went stale).
        uint32_t ResolveOutlineProxy();

        VulkanContext&         m_context;
        const RenderResources& m_resources;
        WorkerPool&            m_workers;

        std::vector<RenderItem>     m_renderQueue;      // immediate Submit() items, this frame only

        // Persistent render proxies + the spatial index they live in (see CreateRenderProxy).
        struct RenderProxy {
            std::vector<glm::mat4> palette;
            glm::uvec4 skin{0u};
            glm::vec3 poseMin{0.0f}, poseMax{0.0f};
            bool poseBounds = false;
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
        // What culling needs of each proxy, kept apart from m_proxies and small (8 bytes) so the
        // culling tasks find it in cache instead of reading the big proxy records. Parallel to m_proxies.
        struct ProxyDraw {
            MeshHandle     mesh;
            MaterialHandle material;
        };
        std::vector<ProxyDraw>      m_proxyDraw;
        std::vector<uint32_t>       m_freeProxies;
        std::vector<uint32_t>       m_animatedProxies;
        std::vector<uint32_t>       m_proxiesAwaitingBounds;
        SpatialIndex                m_spatial{ 0.25f };
        std::vector<RenderItem>     m_casterItems;      // scratch: the selection outline's item

        // BuildViewBatches scratch, kept between frames so its vectors keep their capacity.
        // A pass is the main view (0) or one shadow cascade (1..).
        static constexpr uint32_t kMaxPasses        = 1 + kMaxShadowCascades;
        static constexpr uint32_t kCullChunk        = 2048;          // moving objects per culling task
        static constexpr uint32_t kTreeParts        = 8;             // parts the tree walk is split into (when threaded)
        static constexpr uint32_t kCopyChunk        = 4096;          // instances per copying task
        static constexpr uint32_t kParallelMinItems = 4096;          // below this many objects, stay on one thread
        static constexpr uint32_t kImmediateRef     = 0x80000000u;   // SortItem::ref flag: an index into m_renderQueue
        struct SortItem {
            uint64_t key;   // main pass: material id << 32 | mesh id; cascades: mesh id
            uint32_t ref;   // proxy id, or kImmediateRef | immediate item index
        };
        struct HitList {                        // one task's hits for one pass
            std::vector<SortItem> items;        // the drawable ones
            uint32_t              seen = 0;     // all of them (for the stats)
        };
        struct Bucket {                         // one key's run in a pass's grouped list
            uint64_t key;
            uint32_t first;
            uint32_t count;
        };
        struct PassScratch {
            HitList               immediate;    // immediate items this pass sees
            std::vector<SortItem> grouped;      // every hit, grouped by key, buckets in key order
            std::vector<Bucket>   buckets;      // in the order keys were first met
            std::vector<uint32_t> keyOrder;     // bucket indices in key order
            std::vector<uint64_t> slotKey;      // open-addressing table: key -> bucket (kEmptySlot = free)
            std::vector<uint32_t> slotBucket;
            uint32_t              firstInstance = 0;
        };
        std::vector<int32_t>                m_treeParts;   // tree nodes, one walk each (SpatialIndex::PartitionSettled)
        std::vector<HitList>                m_treeHits;    // [part * kMaxPasses + pass]: settled proxies
        std::vector<HitList>                m_chunkHits;   // [chunk * kMaxPasses + pass]: moving proxies
        std::array<PassScratch, kMaxPasses> m_pass;

        // Selection outline: cached proxy ids for every part of the selected objects.
        // Refresh only when selection or the set of live proxies changes.
        std::vector<std::uint64_t>  m_outlineUserData;
        std::vector<uint32_t>       m_outlineProxies;
        uint64_t m_proxyRevision = 1, m_outlineResolvedRevision = 0;

        // Instances: every object drawn this frame by every pass of every view, one model matrix
        // each (set 0, binding 2). Built on the CPU as each view is prepared, copied in one go by
        // UploadInstances() into this frame's host-visible buffer, which grows (power of two) as
        // needed. Shared by the window's and every viewport's set 0 for that frame index.
        std::vector<InstanceData>                   m_instanceScratch;      // only grows: the first m_instanceCount are this frame's
        uint32_t                                    m_instanceCount = 0;
        std::vector<VulkanBuffer>                   m_instanceBuffers;
        std::vector<void*>                          m_instanceBuffersMapped;
        std::vector<uint32_t>                       m_instanceCapacity;     // in instances
        static constexpr uint32_t                   kInitialInstanceCapacity = 16384;
    };

}   // NAMESPACE CRUCIBLE
