/*************************************************************************
  \file       RenderScene.cpp
  \brief
    See RenderScene.h.
*************************************************************************/
#include "Graphics/RenderScene.h"

#include "Graphics/Camera.h"
#include "Graphics/DebugDraw.h"
#include "Threading/WorkerPool.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Crucible {

    void RenderScene::Init(uint32_t framesInFlight) {

        const uint32_t frames = framesInFlight;
        for (uint32_t i = 0; i < frames; ++i) {
            m_instanceBuffers.emplace_back();
            m_instanceBuffers.back().Create(m_context, vk::DeviceSize{ kInitialInstanceCapacity } * sizeof(InstanceData),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            m_instanceBuffersMapped.push_back(m_instanceBuffers.back().Map());
            m_instanceCapacity.push_back(kInitialInstanceCapacity);
        }
        m_instanceScratch.resize(kInitialInstanceCapacity);
        m_renderQueue.reserve(1024);

    }   // RenderScene::Init()

    void RenderScene::Submit(MeshHandle mesh, MaterialHandle material, const glm::mat4& model, const glm::vec4& params) {

        m_renderQueue.push_back(RenderItem{ mesh, material, model, params });

    }   // RenderScene::Submit()

    RenderProxyHandle RenderScene::CreateProxy(std::uint64_t userData) {

        uint32_t id;
        if (!m_freeProxies.empty()) { id = m_freeProxies.back(); m_freeProxies.pop_back(); }
        else { id = static_cast<uint32_t>(m_proxies.size()); m_proxies.emplace_back(); m_proxyDraw.emplace_back(); }

        m_proxies[id]   = RenderProxy{};
        m_proxyDraw[id] = ProxyDraw{};
        m_proxies[id].alive = true;
        m_proxies[id].userData = userData;
        return RenderProxyHandle{ id };

    }   // RenderScene::CreateProxy()

    void RenderScene::SetProxy(RenderProxyHandle handle, MeshHandle mesh, MaterialHandle material, const glm::mat4& model, bool visible, const glm::vec4& params) {

        if (!handle.IsValid() || handle.id >= m_proxies.size() || !m_proxies[handle.id].alive) { return; }
        RenderProxy& p = m_proxies[handle.id];

        p.params = params;   // read straight from the proxy when the view is built; nothing else depends on it

        const bool sameMatrix = memcmp(&p.model, &model, sizeof(glm::mat4)) == 0;
        if (sameMatrix && p.mesh == mesh && p.material == material && p.visible == visible) { return; }   // the common case

        const bool boundsChange = !sameMatrix || !(p.mesh == mesh) || p.visible != visible;
        p.mesh     = mesh;
        p.material = material;
        m_proxyDraw[handle.id] = ProxyDraw{ mesh, material };
        p.model    = model;
        p.visible  = visible;
        if (boundsChange) { RefreshProxyBounds(handle.id); }   // a material-only change never touches the index

    }   // RenderScene::SetProxy()

    void RenderScene::DestroyProxy(RenderProxyHandle handle) {

        if (!handle.IsValid() || handle.id >= m_proxies.size() || !m_proxies[handle.id].alive) { return; }
        RenderProxy& p = m_proxies[handle.id];
        if (p.spatial != SpatialIndex::kInvalid) { m_spatial.Remove(p.spatial); }
        p = RenderProxy{};
        m_proxyDraw[handle.id] = ProxyDraw{};
        m_freeProxies.push_back(handle.id);

    }   // RenderScene::DestroyProxy()

    void RenderScene::RefreshProxyBounds(uint32_t id) {

        RenderProxy& p = m_proxies[id];
        p.awaitingBounds = false;

        auto removeFromIndex = [&] {
            if (p.spatial != SpatialIndex::kInvalid) { m_spatial.Remove(p.spatial); p.spatial = SpatialIndex::kInvalid; }
        };

        if (!p.visible || !p.mesh.IsValid()) { removeFromIndex(); return; }

        glm::vec3 mn, mx;
        if (!m_resources.GetMeshBounds(p.mesh, mn, mx)) {
            // Mesh not uploaded yet (CreateMeshAsync): not drawable anyway; retry next frame.
            removeFromIndex();
            p.awaitingBounds = true;
            m_proxiesAwaitingBounds.push_back(id);
            return;
        }

        const AABB world = AABB::Transform(AABB(mn, mx), p.model);
        if (p.spatial == SpatialIndex::kInvalid) { p.spatial = m_spatial.Insert(world, id); }
        else                                     { m_spatial.Update(p.spatial, world); }

    }   // RenderScene::RefreshProxyBounds()

    void RenderScene::RetryPendingBounds() {

        // Proxies whose mesh was still uploading last frame: try to get their bounds now.
        if (!m_proxiesAwaitingBounds.empty()) {
            std::vector<uint32_t> pending;
            pending.swap(m_proxiesAwaitingBounds);
            for (uint32_t id : pending) {
                if (id < m_proxies.size() && m_proxies[id].alive && m_proxies[id].awaitingBounds) { RefreshProxyBounds(id); }
            }
        }

    }   // RenderScene::RetryPendingBounds()

    void RenderScene::SetOutlinedObject(std::uint64_t userData) {

        if (userData == 0) { SetOutlinedObjects({}); }
        else               { SetOutlinedObjects({ userData }); }

    }   // RenderScene::SetOutlinedObject()

    void RenderScene::SetOutlinedObjects(const std::vector<std::uint64_t>& userData) {

        std::vector<std::uint64_t> wanted;
        wanted.reserve(userData.size());
        for (std::uint64_t id : userData) {
            if (id != 0 && std::find(wanted.begin(), wanted.end(), id) == wanted.end()) { wanted.push_back(id); }
        }
        if (wanted == m_outlineUserData) { return; }   // called every frame by the editor: usually unchanged

        m_outlineUserData = std::move(wanted);
        m_outlineProxies.assign(m_outlineUserData.size(), 0xFFFFFFFFu);   // re-resolved lazily (ResolveOutlineProxies)

    }   // RenderScene::SetOutlinedObjects()

    uint32_t RenderScene::AllocateInstances(uint32_t count) {

        const uint32_t first = m_instanceCount;
        m_instanceCount += count;
        if (m_instanceCount > m_instanceScratch.size()) {
            m_instanceScratch.resize(std::max<size_t>(m_instanceCount, m_instanceScratch.size() * 2));
        }
        return first;

    }   // RenderScene::AllocateInstances()

    bool RenderScene::IsDrawable(MeshHandle mesh) const {

        // No mesh, or one still uploading (CreateMeshAsync): nothing to draw yet.
        return mesh.IsValid() && mesh.id < m_resources.Meshes().size() && m_resources.Meshes()[mesh.id].indexCount != 0;

    }   // RenderScene::IsDrawable()

    uint64_t RenderScene::DepthKey(MeshHandle mesh, MaterialHandle material) const {

        // The depth-only passes (shadows, ambient occlusion, selection mask) only care about a
        // material that is cut-out or double-sided; for those the key carries it (as id + 1, so 0
        // can mean "none").
        const uint64_t high = m_resources.NeedsMaterialInDepthPasses(material) ? uint64_t{ material.id } + 1 : 0;
        return high << 32 | mesh.id;

    }   // RenderScene::DepthKey()

    MaterialHandle RenderScene::ResolveMaterial(MaterialHandle material) const {

        // The main pass always needs SOME material set bound (the pipeline layout declares set 1
        // unconditionally), so substitute the default rather than skip.
        return material.IsValid() && material.id < m_resources.Materials().size() ? material : m_resources.DefaultMaterial();

    }   // RenderScene::ResolveMaterial()

    template <typename Fn>
    void RenderScene::RunTasks(bool parallel, uint32_t count, Fn&& fn) {

        if (parallel) { m_workers.ParallelFor(count, fn); return; }
        for (uint32_t i = 0; i < count; ++i) { fn(i); }

    }   // RenderScene::RunTasks()

    void RenderScene::BuildViewBatches(const Camera& camera, std::span<const glm::mat4> cascadeViewProj, RenderCullStats& stats,
                                       std::vector<DrawBatch>& mainOut, std::span<std::vector<DrawBatch>> shadowOut) {

        const uint32_t cascades = static_cast<uint32_t>(std::min({ cascadeViewProj.size(), shadowOut.size(), size_t{ kMaxShadowCascades } }));
        const uint32_t passes   = 1 + cascades;
        std::array<Frustum, kMaxPasses> frusta;
        frusta[0] = Frustum::FromViewProjection(camera.GetProjection() * camera.GetView());
        for (uint32_t c = 0; c < cascades; ++c) { frusta[1 + c] = Frustum::FromViewProjection(cascadeViewProj[c]); }

        const std::span<const AABB>          activeBoxes = m_spatial.ActiveBoxes();
        const std::span<const std::uint64_t> activeIds   = m_spatial.ActiveUserData();   // our proxy ids
        const uint32_t activeCount = static_cast<uint32_t>(activeBoxes.size());
        const uint32_t chunks      = (activeCount + kCullChunk - 1) / kCullChunk;
        if (m_chunkHits.size() < size_t{ chunks } * kMaxPasses) { m_chunkHits.resize(size_t{ chunks } * kMaxPasses); }
        const bool parallel = m_spatial.GetCount() + m_renderQueue.size() >= kParallelMinItems;

        // The tree is walked in parts (one task per part and pass) when there is enough of it.
        m_spatial.PartitionSettled(parallel && m_spatial.GetSettledCount() >= kCullChunk ? kTreeParts : 1, m_treeParts);
        const uint32_t treeParts = static_cast<uint32_t>(m_treeParts.size());
        const uint32_t treeTasks = treeParts * passes;
        if (m_treeHits.size() < size_t{ treeParts } * kMaxPasses) { m_treeHits.resize(size_t{ treeParts } * kMaxPasses); }

        // A hit's sort key: by (material, mesh) in the main pass; by mesh alone in the depth-only
        // cascades (DepthKey), where the material doesn't matter and batches can merge across
        // materials - unless it is cut-out or double-sided. Returns false for what can't be drawn
        // (it still counts as seen).
        const auto keysOf = [&](uint32_t proxy, uint64_t& mainKey, uint64_t& depthKey) {
            const ProxyDraw& d = m_proxyDraw[proxy];
            if (!IsDrawable(d.mesh)) { return false; }
            const MaterialHandle material = ResolveMaterial(d.material);
            depthKey = DepthKey(d.mesh, material);
            mainKey  = static_cast<uint64_t>(material.id) << 32 | d.mesh.id;
            return true;
        };

        // 1. Cull. Tasks [0, treeTasks): one part of the tree (settled objects) for one pass. Tasks
        //    [treeTasks, treeTasks + chunks): a chunk of the moving objects, each box tested against
        //    every pass. Each task writes only its own lists; the spatial index and the proxies are
        //    only read.
        RunTasks(parallel, treeTasks + chunks, [&](uint32_t task) {
            if (task < treeTasks) {
                const uint32_t part = task / passes, pass = task % passes;
                HitList& hits = m_treeHits[size_t{ part } * kMaxPasses + pass];
                hits.items.clear();
                hits.seen = 0;
                m_spatial.QueryFrustumSettledPart(m_treeParts[part], frusta[pass], [&](SpatialIndex::Handle h) {
                    const uint32_t proxy = static_cast<uint32_t>(m_spatial.GetUserData(h));
                    uint64_t mainKey, depthKey;
                    ++hits.seen;
                    if (keysOf(proxy, mainKey, depthKey)) { hits.items.push_back(SortItem{ pass == 0 ? mainKey : depthKey, proxy }); }
                });
                return;
            }
            const uint32_t chunk = task - treeTasks;
            const uint32_t begin = chunk * kCullChunk, end = std::min(activeCount, begin + kCullChunk);
            HitList* hits = &m_chunkHits[size_t{ chunk } * kMaxPasses];
            for (uint32_t p = 0; p < passes; ++p) { hits[p].items.clear(); hits[p].seen = 0; }
            for (uint32_t i = begin; i < end; ++i) {
                const AABB& box = activeBoxes[i];
                bool looked = false, drawable = false;
                uint64_t mainKey = 0, depthKey = 0;
                const uint32_t proxy = static_cast<uint32_t>(activeIds[i]);
                for (uint32_t p = 0; p < passes; ++p) {
                    if (!frusta[p].Intersects(box)) { continue; }
                    if (!looked) { drawable = keysOf(proxy, mainKey, depthKey); looked = true; }
                    ++hits[p].seen;
                    if (drawable) { hits[p].items.push_back(SortItem{ p == 0 ? mainKey : depthKey, proxy }); }
                }
            }
        });

        // 2. Per pass: add the immediate items, then group everything by key.
        RunTasks(parallel, passes, [&](uint32_t pass) {
            // Immediate items: brute force (there are few, and they have no persistent identity).
            HitList& immediate = m_pass[pass].immediate;
            immediate.items.clear();
            immediate.seen = 0;
            glm::vec3 mn, mx;
            for (uint32_t q = 0; q < static_cast<uint32_t>(m_renderQueue.size()); ++q) {
                const RenderItem& item = m_renderQueue[q];
                if (m_resources.GetMeshBounds(item.mesh, mn, mx) && !frusta[pass].Intersects(AABB::Transform(AABB(mn, mx), item.model))) { continue; }
                ++immediate.seen;
                if (!IsDrawable(item.mesh)) { continue; }
                const MaterialHandle material = ResolveMaterial(item.material);
                const uint64_t key = pass == 0 ? static_cast<uint64_t>(material.id) << 32 | item.mesh.id : DepthKey(item.mesh, material);
                immediate.items.push_back(SortItem{ key, kImmediateRef | q });
            }
            GroupPass(pass, treeParts, chunks);
        });

        // 3. Each pass's instances go one after another, in pass order; one batch per key run.
        uint32_t viewInstances = 0;
        for (uint32_t pass = 0; pass < passes; ++pass) { viewInstances += static_cast<uint32_t>(m_pass[pass].grouped.size()); }
        const uint32_t firstOfView = AllocateInstances(viewInstances);
        uint32_t next = firstOfView;
        for (uint32_t pass = 0; pass < passes; ++pass) {
            PassScratch& ps = m_pass[pass];
            ps.firstInstance = next;
            std::vector<DrawBatch>& out = pass == 0 ? mainOut : shadowOut[pass - 1];
            for (uint32_t b : ps.keyOrder) {
                const Bucket& bucket = ps.buckets[b];
                const MeshHandle     mesh{ static_cast<uint32_t>(bucket.key & 0xFFFFFFFFu) };
                const uint32_t       high = static_cast<uint32_t>(bucket.key >> 32);
                const MaterialHandle material = pass == 0 ? MaterialHandle{ high }
                                              : high != 0 ? MaterialHandle{ high - 1 } : MaterialHandle{};   // see DepthKey
                out.push_back(DrawBatch{ mesh, material, ps.firstInstance + bucket.first, bucket.count });
            }
            next += static_cast<uint32_t>(ps.grouped.size());
        }

        // Copy the instances, in equal chunks across the threads (the main pass usually has most).
        const uint32_t total      = next - firstOfView;
        const uint32_t copyChunks = (total + kCopyChunk - 1) / kCopyChunk;
        const glm::vec4 noParams{ 1.0f, 0.0f, 0.0f, 0.0f };   // depth-only passes don't read them
        RunTasks(parallel, copyChunks, [&](uint32_t task) {
            uint32_t index = firstOfView + task * kCopyChunk;
            const uint32_t end = std::min(next, index + kCopyChunk);
            uint32_t pass = 0;
            while (pass + 1 < passes && index >= m_pass[pass + 1].firstInstance) { ++pass; }
            for (; index < end; ++index) {
                while (pass + 1 < passes && index >= m_pass[pass + 1].firstInstance) { ++pass; }
                const SortItem& item = m_pass[pass].grouped[index - m_pass[pass].firstInstance];
                if (item.ref & kImmediateRef) {
                    const RenderItem& r = m_renderQueue[item.ref & ~kImmediateRef];
                    m_instanceScratch[index] = InstanceData{ r.model, pass == 0 ? r.params : noParams };
                } else {
                    const RenderProxy& p = m_proxies[item.ref];
                    m_instanceScratch[index] = InstanceData{ p.model, pass == 0 ? p.params : noParams };
                }
            }
        });

        stats = RenderCullStats{};
        stats.proxies          = m_spatial.GetCount();
        stats.movingProxies    = m_spatial.GetActiveCount();
        stats.proxiesVisible   = 0;
        for (uint32_t part = 0; part < treeParts; ++part) { stats.proxiesVisible += m_treeHits[size_t{ part } * kMaxPasses].seen; }
        for (uint32_t chunk = 0; chunk < chunks; ++chunk) { stats.proxiesVisible += m_chunkHits[size_t{ chunk } * kMaxPasses].seen; }
        stats.immediate        = static_cast<uint32_t>(m_renderQueue.size());
        stats.immediateVisible = m_pass[0].immediate.seen;
        for (uint32_t c = 0; c < cascades; ++c) {
            stats.shadowCasters   += static_cast<uint32_t>(m_pass[1 + c].grouped.size());
            stats.shadowDrawCalls += static_cast<uint32_t>(shadowOut[c].size());
        }

    }   // RenderScene::BuildViewBatches()

    void RenderScene::GroupPass(uint32_t pass, uint32_t treeParts, uint32_t chunks) {

        // A counting sort: count each key's hits (keys found through a small hash table - a view has
        // few distinct mesh / material pairs), lay the buckets out in key order, then drop every hit
        // into its bucket. Linear in the hits; within a bucket they keep the order they were found
        // in (tree part by part, then chunk by chunk, then immediate items), so the result never
        // depends on how the threads were scheduled.
        constexpr uint64_t kEmptySlot = ~uint64_t{ 0 };   // never a real key: ids are below 0xFFFFFFFF
        PassScratch& ps = m_pass[pass];
        ps.buckets.clear();
        if (ps.slotKey.empty()) { ps.slotKey.assign(64, kEmptySlot); ps.slotBucket.assign(64, 0); }
        else { std::fill(ps.slotKey.begin(), ps.slotKey.end(), kEmptySlot); }

        const auto slotOf = [&](uint64_t key) {
            const size_t mask = ps.slotKey.size() - 1;
            size_t slot = static_cast<size_t>((key * 0x9E3779B97F4A7C15ull) >> 32) & mask;
            while (ps.slotKey[slot] != kEmptySlot && ps.slotKey[slot] != key) { slot = (slot + 1) & mask; }
            return slot;
        };
        const auto bucketOf = [&](uint64_t key) -> Bucket& {
            size_t slot = slotOf(key);
            if (ps.slotKey[slot] == kEmptySlot) {
                // Keep the table at most half full: grow and re-place every key when it would pass that.
                if ((ps.buckets.size() + 1) * 2 > ps.slotKey.size()) {
                    ps.slotKey.assign(ps.slotKey.size() * 2, kEmptySlot);
                    ps.slotBucket.assign(ps.slotKey.size(), 0);
                    for (uint32_t b = 0; b < ps.buckets.size(); ++b) {
                        const size_t s = slotOf(ps.buckets[b].key);
                        ps.slotKey[s] = ps.buckets[b].key;
                        ps.slotBucket[s] = b;
                    }
                    slot = slotOf(key);
                }
                ps.slotKey[slot]    = key;
                ps.slotBucket[slot] = static_cast<uint32_t>(ps.buckets.size());
                ps.buckets.push_back(Bucket{ key, 0, 0 });
            }
            return ps.buckets[ps.slotBucket[slot]];
        };
        const auto forEachList = [&](auto&& fn) {
            for (uint32_t part = 0; part < treeParts; ++part) { fn(m_treeHits[size_t{ part } * kMaxPasses + pass]); }
            for (uint32_t chunk = 0; chunk < chunks; ++chunk) { fn(m_chunkHits[size_t{ chunk } * kMaxPasses + pass]); }
            fn(ps.immediate);
        };

        // Count.
        uint32_t total = 0;
        forEachList([&](const HitList& list) {
            for (const SortItem& item : list.items) { ++bucketOf(item.key).count; }
            total += static_cast<uint32_t>(list.items.size());
        });

        // Lay the buckets out in key order (few of them).
        ps.keyOrder.resize(ps.buckets.size());
        for (uint32_t b = 0; b < ps.keyOrder.size(); ++b) { ps.keyOrder[b] = b; }
        std::sort(ps.keyOrder.begin(), ps.keyOrder.end(), [&](uint32_t a, uint32_t b) { return ps.buckets[a].key < ps.buckets[b].key; });
        uint32_t first = 0;
        for (uint32_t b : ps.keyOrder) { ps.buckets[b].first = first; first += ps.buckets[b].count; ps.buckets[b].count = 0; }

        // Place (count is rebuilt as each bucket's fill level).
        ps.grouped.resize(total);
        forEachList([&](const HitList& list) {
            for (const SortItem& item : list.items) {
                Bucket& bucket = bucketOf(item.key);
                ps.grouped[bucket.first + bucket.count++] = item;
            }
        });

    }   // RenderScene::GroupPass()

    void RenderScene::BuildOutlineBatches(std::vector<DrawBatch>& out) {

        ResolveOutlineProxies();
        m_casterItems.clear();
        for (uint32_t id : m_outlineProxies) {
            if (id != 0xFFFFFFFFu && m_proxies[id].spatial != SpatialIndex::kInvalid) {
                // The material only when the mask needs it (a cut-out leaf's outline follows the leaf).
                const MaterialHandle material = ResolveMaterial(m_proxyDraw[id].material);
                m_casterItems.push_back(RenderItem{ m_proxies[id].mesh,
                                                    m_resources.NeedsMaterialInDepthPasses(material) ? material : MaterialHandle{},
                                                    m_proxies[id].model });
            }
        }
        if (!m_casterItems.empty()) {
            // AppendBatches merges consecutive items with the same mesh (and material) into one instanced draw.
            std::sort(m_casterItems.begin(), m_casterItems.end(), [](const RenderItem& a, const RenderItem& b) {
                return a.mesh == b.mesh ? a.material.id < b.material.id : a.mesh < b.mesh;
            });
            AppendBatches(m_casterItems, out, false);
        }

    }   // RenderScene::BuildOutlineBatches()

    uint32_t RenderScene::AppendBatches(const std::vector<RenderItem>& items, std::vector<DrawBatch>& batches, bool byMaterial) {

        uint32_t added = 0;
        for (const RenderItem& item : items) {

            if (!IsDrawable(item.mesh)) { continue; }

            // Depth-only passes keep the item's material as given (Invalid unless it is cut-out or
            // double-sided), so their batches merge across ordinary materials.
            const MaterialHandle material = byMaterial ? ResolveMaterial(item.material) : item.material;

            const uint32_t index = AllocateInstances(1);
            m_instanceScratch[index] = InstanceData{ item.model, item.params };
            ++added;

            if (!batches.empty()) {
                DrawBatch& last = batches.back();
                if (last.mesh == item.mesh && last.material == material && last.firstInstance + last.instanceCount == index) {
                    ++last.instanceCount;
                    continue;
                }
            }
            batches.push_back(DrawBatch{ item.mesh, material, index, 1 });
        }
        return added;

    }   // RenderScene::AppendBatches()

    bool RenderScene::UploadInstances(uint32_t frameIndex) {

        bool grew = false;
        const size_t count = m_instanceCount;

        // Grow (power of two). Safe to recreate here: BeginFrame() already waited on this frame
        // index's fence, so the GPU is done with the old buffer, and nothing recorded this frame
        // references it yet. The caller re-points this frame index's sets at the new buffer.
        if (count > m_instanceCapacity[frameIndex]) {
            uint32_t capacity = m_instanceCapacity[frameIndex];
            while (capacity < count) { capacity *= 2; }
            m_instanceBuffers[frameIndex].Create(m_context, vk::DeviceSize{ capacity } * sizeof(InstanceData),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            m_instanceBuffersMapped[frameIndex] = m_instanceBuffers[frameIndex].Map();
            m_instanceCapacity[frameIndex]      = capacity;

            grew = true;
        }

        if (count > 0) { memcpy(m_instanceBuffersMapped[frameIndex], m_instanceScratch.data(), count * sizeof(InstanceData)); }
        return grew;

    }   // RenderScene::UploadInstances()

    void RenderScene::ResolveOutlineProxies() {

        constexpr uint32_t kNone = 0xFFFFFFFFu;
        bool stale = false;
        for (std::size_t i = 0; i < m_outlineUserData.size(); ++i) {
            const uint32_t id = m_outlineProxies[i];
            if (id < m_proxies.size() && m_proxies[id].alive && m_proxies[id].userData == m_outlineUserData[i]) { continue; }
            m_outlineProxies[i] = kNone;
            stale = true;
        }
        if (!stale) { return; }

        // Stale or never looked up: one linear scan for all of them together (selection changes
        // are rare; a selected entity with no proxy, e.g. a light, costs one cheap scan per frame).
        for (uint32_t p = 0; p < m_proxies.size(); ++p) {
            if (!m_proxies[p].alive) { continue; }
            for (std::size_t i = 0; i < m_outlineUserData.size(); ++i) {
                if (m_outlineProxies[i] == kNone && m_proxies[p].userData == m_outlineUserData[i]) { m_outlineProxies[i] = p; break; }
            }
        }

    }   // RenderScene::ResolveOutlineProxies()

    namespace {

        // Möller-Trumbore, double-sided (picking should hit a plane from below too). t is in units
        // of `dir`, so it carries over unchanged between world and object space.
        bool RayTriangle(const glm::vec3& o, const glm::vec3& dir, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float& t) {
            const glm::vec3 e1 = b - a, e2 = c - a;
            const glm::vec3 p = glm::cross(dir, e2);
            const float det = glm::dot(e1, p);
            if (std::abs(det) < 1e-12f) { return false; }
            const float inv = 1.0f / det;
            const glm::vec3 s = o - a;
            const float u = glm::dot(s, p) * inv;
            if (u < 0.0f || u > 1.0f) { return false; }
            const glm::vec3 q = glm::cross(s, e1);
            const float v = glm::dot(dir, q) * inv;
            if (v < 0.0f || u + v > 1.0f) { return false; }
            t = glm::dot(e2, q) * inv;
            return t >= 0.0f;
        }

    }   // anonymous namespace

    bool RenderScene::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RenderRaycastHit& outHit) const {

        const float len = glm::length(direction);
        if (len < 1e-8f || maxDistance <= 0.0f) { return false; }
        const Ray ray{ origin, direction / len };

        bool found = false;
        float best = maxDistance;

        m_spatial.RayCast(ray, maxDistance, [&](SpatialIndex::Handle h, float tBox) {
            if (tBox >= best) { return best; }
            const RenderProxy& p = m_proxies[static_cast<uint32_t>(m_spatial.GetUserData(h))];
            if (!p.mesh.IsValid() || p.mesh.id >= m_resources.Meshes().size()) { return best; }
            const GpuMesh& mesh = m_resources.Meshes()[p.mesh.id];

            float tHit = -1.0f;
            if (mesh.cpuIndices.empty()) {
                tHit = tBox;   // no geometry kept: the tight box is the best we can do
            } else {
                // Into object space: t is preserved because the mapping is affine.
                const glm::mat4 inv = glm::inverse(p.model);
                const glm::vec3 o = glm::vec3(inv * glm::vec4(ray.origin, 1.0f));
                const glm::vec3 d = glm::vec3(inv * glm::vec4(ray.direction, 0.0f));
                const auto& pos = mesh.cpuPositions;
                const auto& idx = mesh.cpuIndices;
                for (size_t i = 0; i + 2 < idx.size(); i += 3) {
                    float t;
                    if (RayTriangle(o, d, pos[idx[i]], pos[idx[i + 1]], pos[idx[i + 2]], t) && t < best && (tHit < 0.0f || t < tHit)) { tHit = t; }
                }
            }

            if (tHit >= 0.0f && tHit < best) {
                best = tHit;
                found = true;
                outHit.userData = p.userData;
                outHit.distance = tHit;
                outHit.point    = ray.origin + ray.direction * tHit;
                outHit.mesh     = p.mesh;
            }
            return best;
        });

        return found;

    }   // RenderScene::Raycast()

    void RenderScene::DrawBoundsDebug() const {

        DebugStyle settled;
        settled.category = DebugCategory::Bounds;
        settled.color    = { 0.35f, 0.55f, 1.0f, 1.0f };
        DebugStyle moving = settled;
        moving.color     = { 1.0f, 0.55f, 0.15f, 1.0f };

        for (const RenderProxy& p : m_proxies) {
            if (!p.alive || p.spatial == SpatialIndex::kInvalid) { continue; }
            const AABB& b = m_spatial.GetBounds(p.spatial);
            DebugDraw::AABB(b.min, b.max, m_spatial.IsActive(p.spatial) ? moving : settled);
        }

    }   // RenderScene::DrawBoundsDebug()

}   // NAMESPACE CRUCIBLE
