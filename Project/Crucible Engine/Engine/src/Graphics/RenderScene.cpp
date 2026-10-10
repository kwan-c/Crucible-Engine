/*************************************************************************
  \file       RenderScene.cpp
  \brief
    See RenderScene.h.
*************************************************************************/
#include "Graphics/RenderScene.h"

#include "Graphics/Camera.h"
#include "Graphics/DebugDraw.h"

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
        m_instanceScratch.reserve(kInitialInstanceCapacity);
        m_renderQueue.reserve(1024);

    }   // RenderScene::Init()

    void RenderScene::Submit(MeshHandle mesh, MaterialHandle material, const glm::mat4& model, const glm::vec4& params) {

        m_renderQueue.push_back(RenderItem{ mesh, material, model, params });

    }   // RenderScene::Submit()

    RenderProxyHandle RenderScene::CreateProxy(std::uint64_t userData) {

        uint32_t id;
        if (!m_freeProxies.empty()) { id = m_freeProxies.back(); m_freeProxies.pop_back(); }
        else { id = static_cast<uint32_t>(m_proxies.size()); m_proxies.emplace_back(); }

        m_proxies[id] = RenderProxy{};
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
        p.model    = model;
        p.visible  = visible;
        if (boundsChange) { RefreshProxyBounds(handle.id); }   // a material-only change never touches the index

    }   // RenderScene::SetProxy()

    void RenderScene::DestroyProxy(RenderProxyHandle handle) {

        if (!handle.IsValid() || handle.id >= m_proxies.size() || !m_proxies[handle.id].alive) { return; }
        RenderProxy& p = m_proxies[handle.id];
        if (p.spatial != SpatialIndex::kInvalid) { m_spatial.Remove(p.spatial); }
        p = RenderProxy{};
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

    void RenderScene::BuildMainBatches(const Camera& camera, RenderCullStats& stats, std::vector<DrawBatch>& out) {

        BuildVisibleItems(camera, stats);
        AppendBatches(m_visibleItems, out, true);

    }   // RenderScene::BuildMainBatches()

    void RenderScene::BuildShadowBatches(const glm::mat4& viewProj, std::vector<DrawBatch>& out, RenderCullStats& stats) {

        glm::vec3 mn, mx;
        m_casterItems.clear();
        const Frustum frustum = Frustum::FromViewProjection(viewProj);
        m_spatial.QueryFrustum(frustum, [&](SpatialIndex::Handle h) {
            const RenderProxy& p = m_proxies[static_cast<uint32_t>(m_spatial.GetUserData(h))];
            m_casterItems.push_back(RenderItem{ p.mesh, MaterialHandle{}, p.model });
        });
        for (const RenderItem& item : m_renderQueue) {
            if (m_resources.GetMeshBounds(item.mesh, mn, mx) && !frustum.Intersects(AABB::Transform(AABB(mn, mx), item.model))) { continue; }
            m_casterItems.push_back(RenderItem{ item.mesh, MaterialHandle{}, item.model });
        }
        // Depth only: the material doesn't matter, so batch by mesh alone.
        std::sort(m_casterItems.begin(), m_casterItems.end(), [](const RenderItem& a, const RenderItem& b) { return a.mesh < b.mesh; });
        stats.shadowCasters   += AppendBatches(m_casterItems, out, false);
        stats.shadowDrawCalls += static_cast<uint32_t>(out.size());

    }   // RenderScene::BuildShadowBatches()

    void RenderScene::BuildOutlineBatches(std::vector<DrawBatch>& out) {

        ResolveOutlineProxies();
        m_casterItems.clear();
        for (uint32_t id : m_outlineProxies) {
            if (id != 0xFFFFFFFFu && m_proxies[id].spatial != SpatialIndex::kInvalid) {
                m_casterItems.push_back(RenderItem{ m_proxies[id].mesh, MaterialHandle{}, m_proxies[id].model });
            }
        }
        if (!m_casterItems.empty()) {
            // AppendBatches merges consecutive items with the same mesh into one instanced draw.
            std::sort(m_casterItems.begin(), m_casterItems.end(), [](const RenderItem& a, const RenderItem& b) { return a.mesh < b.mesh; });
            AppendBatches(m_casterItems, out, false);
        }

    }   // RenderScene::BuildOutlineBatches()

    void RenderScene::BuildVisibleItems(const Camera& camera, RenderCullStats& stats) {

        m_visibleItems.clear();
        stats = RenderCullStats{};

        const Frustum frustum = Frustum::FromViewProjection(camera.GetProjection() * camera.GetView());

        // Proxies: tree branches / the moving-object array, per SpatialIndex.
        m_spatial.QueryFrustum(frustum, [&](SpatialIndex::Handle h) {
            const RenderProxy& p = m_proxies[static_cast<uint32_t>(m_spatial.GetUserData(h))];
            m_visibleItems.push_back(RenderItem{ p.mesh, p.material, p.model, p.params });
        });
        stats.proxies        = m_spatial.GetCount();
        stats.proxiesVisible = static_cast<uint32_t>(m_visibleItems.size());
        stats.movingProxies  = m_spatial.GetActiveCount();

        // Immediate items: brute force (there are few, and they have no persistent identity).
        glm::vec3 mn, mx;
        for (const RenderItem& item : m_renderQueue) {
            if (m_resources.GetMeshBounds(item.mesh, mn, mx) && !frustum.Intersects(AABB::Transform(AABB(mn, mx), item.model))) { continue; }
            m_visibleItems.push_back(item);
        }
        stats.immediate        = static_cast<uint32_t>(m_renderQueue.size());
        stats.immediateVisible = static_cast<uint32_t>(m_visibleItems.size()) - stats.proxiesVisible;

        // Group by (material, mesh) so the material set and mesh buffers rebind as rarely as possible.
        std::sort(m_visibleItems.begin(), m_visibleItems.end(), [](const RenderItem& a, const RenderItem& b) {
            if (!(a.material == b.material)) { return a.material < b.material; }
            return a.mesh < b.mesh;
        });

    }   // RenderScene::BuildVisibleItems()

    uint32_t RenderScene::AppendBatches(const std::vector<RenderItem>& items, std::vector<DrawBatch>& batches, bool byMaterial) {

        uint32_t added = 0;
        for (const RenderItem& item : items) {

            // Skip what can't be drawn: no mesh, or one still uploading (CreateMeshAsync).
            if (!item.mesh.IsValid() || item.mesh.id >= m_resources.Meshes().size() || m_resources.Meshes()[item.mesh.id].indexCount == 0) { continue; }

            // The main pass always needs SOME material set bound (the pipeline layout declares set 1
            // unconditionally), so substitute the default rather than skip. Depth-only passes don't
            // care, and leaving it Invalid lets their batches merge across materials.
            MaterialHandle material;
            if (byMaterial) {
                material = item.material.IsValid() && item.material.id < m_resources.Materials().size() ? item.material : m_resources.DefaultMaterial();
            }

            const uint32_t index = static_cast<uint32_t>(m_instanceScratch.size());
            m_instanceScratch.push_back(InstanceData{ item.model, item.params });
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
        const size_t count = m_instanceScratch.size();

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
