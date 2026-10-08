/*************************************************************************
  \file       SceneLights.cpp
  \brief
    See SceneLights.h.
*************************************************************************/
#include "Graphics/SceneLights.h"

#include "Graphics/Camera.h"
#include "Graphics/EnvironmentMaps.h"   // kFallbackSunTowards
#include "Diagnostics/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Crucible {

    namespace {

        // CPU-side LightData -> the shader's GpuLight (see both structs in RenderTypes.h).
        GpuLight ToGpuLight(const LightData& light) {

            glm::vec3 direction = light.direction;
            const float length = glm::length(direction);
            direction = (length > 1e-6f) ? direction / length : glm::vec3(0.0f, -1.0f, 0.0f);

            // smoothstep(cosOuter, cosInner, x) in the shader needs cosOuter < cosInner strictly,
            // i.e. outer > inner - keep at least a sliver of falloff even if someone sets them
            // equal (or backwards) in the Inspector.
            const float outerDeg = glm::clamp(light.outerConeDeg, 0.1f, 89.9f);
            const float innerDeg = glm::clamp(light.innerConeDeg, 0.0f, outerDeg - 0.1f);

            GpuLight gpu;
            gpu.positionRange   = glm::vec4(light.position, std::max(light.range, 1e-3f));
            gpu.directionType   = glm::vec4(direction, static_cast<float>(light.type));
            gpu.colourIntensity = glm::vec4(light.colour, std::max(light.intensity, 0.0f));
            gpu.spotParams      = glm::vec4(std::cos(glm::radians(innerDeg)), std::cos(glm::radians(outerDeg)), 0.0f, 0.0f);
            return gpu;
        }

        // The light used when nothing was submitted: the old shader's one hardcoded light
        // (towards (0.2, 0.8, 0.5), radiance 3.0), so scenes without Light entities look as they
        // did before the lighting system existed.
        // (kFallbackSunTowards lives in EnvironmentMaps.h: the built-in sky paints its sun there.)

        LightData MakeFallbackSun() {
            LightData sun;
            sun.type      = LightType::Directional;
            sun.direction = -kFallbackSunTowards;
            sun.intensity = 3.0f;
            return sun;
        }

    }   // anonymous namespace

    void SceneLights::Init(VulkanContext& context, uint32_t framesInFlight) {

        CreateLightBuffers(context, framesInFlight);
        CreateClusterBuffers(context, framesInFlight);
        m_lightQueue.reserve(kMaxLights);

    }   // SceneLights::Init()

    void SceneLights::Submit(const LightData& light) {

        if (m_lightQueue.size() >= kMaxLights) {
            if (!m_warnedTooManyLights) {
                CRU_WARN("[Renderer] More than {} lights submitted in one frame; extras are ignored.", kMaxLights);
                m_warnedTooManyLights = true;
            }
            return;
        }
        // The first enabled directional light that asks for shadows gets the cascaded shadow map.
        if (m_shadowLightIndex < 0 && light.castShadows && light.type == LightType::Directional) {
            m_shadowLightIndex = static_cast<int32_t>(m_lightQueue.size());
        }
        m_lightQueue.push_back(ToGpuLight(light));

    }   // SceneLights::Submit()

    void SceneLights::CreateLightBuffers(VulkanContext& context, uint32_t framesInFlight) {

        for (uint32_t i = 0; i < framesInFlight; ++i) {
            m_lightBuffers.emplace_back();
            m_lightBuffers.back().Create(context, sizeof(GpuLight) * kMaxLights,
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            m_lightBuffersMapped.emplace_back(m_lightBuffers.back().Map());
        }

    }   // SceneLights::CreateLightBuffers()

    void SceneLights::BuildFrameLights(uint32_t frameIndex) {

        // m_frameLights = what is uploaded: directional lights first (the shader loops over those
        // directly and finds the rest through the light clusters), each group in submission order.
        m_frameLights.clear();

        if (m_lightQueue.empty()) {
            m_frameLights.push_back(ToGpuLight(MakeFallbackSun()));
            m_frameDirLightCount = 1;
            m_shadowLightIndex   = 0;   // the fallback sun always casts
        } else {
            const auto isDirectional = [](const GpuLight& l) { return static_cast<int>(l.directionType.w + 0.5f) == static_cast<int>(LightType::Directional); };
            int32_t shadowIndex = -1;
            for (size_t i = 0; i < m_lightQueue.size(); ++i) {
                if (!isDirectional(m_lightQueue[i])) { continue; }
                if (static_cast<int32_t>(i) == m_shadowLightIndex) { shadowIndex = static_cast<int32_t>(m_frameLights.size()); }
                m_frameLights.push_back(m_lightQueue[i]);
            }
            m_frameDirLightCount = static_cast<uint32_t>(m_frameLights.size());
            for (const GpuLight& l : m_lightQueue) { if (!isDirectional(l)) { m_frameLights.push_back(l); } }
            m_shadowLightIndex = shadowIndex;   // now an index into the reordered buffer
        }

        m_frameLightCount = static_cast<uint32_t>(m_frameLights.size());   // already capped by SubmitLight
        if (m_shadowLightIndex >= 0) { m_shadowLightDir = glm::vec3(m_frameLights[m_shadowLightIndex].directionType); }

        // Same frame-in-flight safety as the uniform buffers: BeginFrame() has already waited on
        // this frame index's fence, so the GPU is done reading m_lightBuffers[frameIndex].
        memcpy(m_lightBuffersMapped[frameIndex], m_frameLights.data(), sizeof(GpuLight) * m_frameLightCount);

    }   // SceneLights::BuildFrameLights()

    void SceneLights::CreateClusterBuffers(VulkanContext& context, uint32_t framesInFlight) {

        const uint32_t frames = framesInFlight;
        for (uint32_t i = 0; i < frames; ++i) {
            m_clusterGridBuffers.emplace_back();
            m_clusterGridBuffers.back().Create(context, vk::DeviceSize{ kMaxClusterViews } * kClustersPerView * sizeof(ClusterCell),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            m_clusterGridMapped.push_back(m_clusterGridBuffers.back().Map());

            m_clusterIndexBuffers.emplace_back();
            m_clusterIndexBuffers.back().Create(context, vk::DeviceSize{ kMaxClusterIndices } * sizeof(uint32_t),
                vk::BufferUsageFlagBits::eStorageBuffer,
                vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            m_clusterIndexMapped.push_back(m_clusterIndexBuffers.back().Map());
        }
        m_clusterCounts.resize(kClustersPerView);

    }   // SceneLights::CreateClusterBuffers()

    uint32_t SceneLights::BuildClusters(const Camera& camera, vk::Extent2D extent, FrameUniforms& ubo) {

        // The grid: kClusterX x kClusterY tiles across the screen, kClusterZ slices in depth. Slices
        // are logarithmic (each one a constant factor deeper than the last), which keeps cells
        // roughly cube-shaped from the near plane to the far plane.
        const float nearZ    = std::max(camera.nearZ, 1e-3f);
        const float farZ     = std::max(camera.farZ, nearZ * 1.01f);
        const float logScale = static_cast<float>(kClusterZ) / std::log(farZ / nearZ);
        const float tanY     = std::tan(glm::radians(camera.fovY) * 0.5f);
        const float tanX     = tanY * camera.aspect;

        const uint32_t view = std::min(m_preparedViews, kMaxClusterViews - 1);
        const uint32_t base = view * kClustersPerView;
        ubo.clusterDims = glm::uvec4(kClusterX, kClusterY, kClusterZ, base);
        ubo.clusterZ    = glm::vec4(nearZ, logScale, static_cast<float>(std::max(extent.width, 1u)), static_cast<float>(std::max(extent.height, 1u)));

        m_clusterGridScratch.resize(static_cast<size_t>(base) + kClustersPerView);   // zero-initialised cells
        std::fill(m_clusterCounts.begin(), m_clusterCounts.end(), 0u);
        m_clusterRanges.clear();
        m_clusterRangeLights.clear();

        // Pass 1: for each point/spot light, the box of cells its sphere of influence can touch.
        // A spot light is treated as its whole sphere (conservative: a few extra cells, never a
        // missing one).
        for (uint32_t i = m_frameDirLightCount; i < m_frameLightCount; ++i) {

            const GpuLight& light = m_frameLights[i];
            const glm::vec3 c = glm::vec3(ubo.view * glm::vec4(glm::vec3(light.positionRange), 1.0f));
            const float r = light.positionRange.w;
            const float z = -c.z;   // view space looks down -Z; z is depth in front of the camera

            if (z + r <= nearZ || z - r >= farZ) { continue; }
            const float zMin = std::max(z - r, nearZ);
            const float zMax = std::min(z + r, farZ);

            // Screen extent of the sphere's view-space bounding box, as NDC (-1..1). A positive edge
            // is widest on screen where the box is nearest; a negative edge where it is farthest
            // from the centre line - so each edge is projected at whichever depth pushes it outward.
            const auto project = [](float lo, float hi, float zNear, float zFar, float tanHalf, float& outLo, float& outHi) {
                outLo = lo / ((lo < 0.0f ? zNear : zFar) * tanHalf);
                outHi = hi / ((hi > 0.0f ? zNear : zFar) * tanHalf);
            };
            float x0, x1, y0, y1;
            project(c.x - r, c.x + r, zMin, zMax, tanX, x0, x1);
            project(c.y - r, c.y + r, zMin, zMax, tanY, y0, y1);
            if (x1 <= -1.0f || x0 >= 1.0f || y1 <= -1.0f || y0 >= 1.0f) { continue; }   // off screen

            const auto tile = [](float ndc, uint32_t count) {
                const float t = (ndc * 0.5f + 0.5f) * static_cast<float>(count);
                return static_cast<uint16_t>(glm::clamp(static_cast<int>(std::floor(t)), 0, static_cast<int>(count) - 1));
            };
            const auto slice = [&](float depth) {
                const float s = std::log(depth / nearZ) * logScale;
                return static_cast<uint16_t>(glm::clamp(static_cast<int>(std::floor(s)), 0, static_cast<int>(kClusterZ) - 1));
            };
            const std::array<uint16_t, 6> range{ tile(x0, kClusterX), tile(x1, kClusterX), tile(y0, kClusterY), tile(y1, kClusterY), slice(zMin), slice(zMax) };

            for (uint32_t cz = range[4]; cz <= range[5]; ++cz) {
                for (uint32_t cy = range[2]; cy <= range[3]; ++cy) {
                    uint32_t* row = &m_clusterCounts[cz * kClusterX * kClusterY + cy * kClusterX];
                    for (uint32_t cx = range[0]; cx <= range[1]; ++cx) { ++row[cx]; }
                }
            }
            m_clusterRanges.push_back(range);
            m_clusterRangeLights.push_back(i);
        }

        // Pass 2: give every cell its slice of the index buffer. If the frame's budget of light
        // references runs out, the remaining cells simply get fewer (or no) lights.
        uint32_t next = static_cast<uint32_t>(m_clusterIndexScratch.size());
        ClusterCell* cells = &m_clusterGridScratch[base];
        for (uint32_t cell = 0; cell < kClustersPerView; ++cell) {
            const uint32_t want = m_clusterCounts[cell];
            const uint32_t room = kMaxClusterIndices - next;
            cells[cell].offset = next;
            cells[cell].count  = 0;                       // refilled in pass 3
            m_clusterCounts[cell] = std::min(want, room); // now: capacity of this cell
            next += m_clusterCounts[cell];
        }
        m_clusterIndexScratch.resize(next);

        // Pass 3: write each light's index into every cell of its range.
        for (size_t n = 0; n < m_clusterRanges.size(); ++n) {
            const auto&    range = m_clusterRanges[n];
            const uint32_t light = m_clusterRangeLights[n];
            for (uint32_t cz = range[4]; cz <= range[5]; ++cz) {
                for (uint32_t cy = range[2]; cy <= range[3]; ++cy) {
                    for (uint32_t cx = range[0]; cx <= range[1]; ++cx) {
                        const uint32_t cell = cz * kClusterX * kClusterY + cy * kClusterX + cx;
                        if (cells[cell].count < m_clusterCounts[cell]) {
                            m_clusterIndexScratch[cells[cell].offset + cells[cell].count++] = light;
                        }
                    }
                }
            }
        }

        return static_cast<uint32_t>(m_clusterRanges.size());

    }   // SceneLights::BuildClusters()

    void SceneLights::UploadClusters(uint32_t frameIndex) {

        if (!m_clusterGridScratch.empty()) {
            const size_t cells = std::min<size_t>(m_clusterGridScratch.size(), size_t{ kMaxClusterViews } * kClustersPerView);
            memcpy(m_clusterGridMapped[frameIndex], m_clusterGridScratch.data(), cells * sizeof(ClusterCell));
        }
        if (!m_clusterIndexScratch.empty()) {
            memcpy(m_clusterIndexMapped[frameIndex], m_clusterIndexScratch.data(), m_clusterIndexScratch.size() * sizeof(uint32_t));
        }

    }   // SceneLights::UploadClusters()

}   // NAMESPACE CRUCIBLE
