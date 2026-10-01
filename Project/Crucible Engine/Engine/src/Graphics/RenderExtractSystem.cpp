/*************************************************************************
* \file:         RenderExtractSystem.cpp
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                See RenderExtractSystem.h for what this system does and
*                why it exists.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include "RenderExtractSystem.h"

// IECSSystems.h only forward-declares World (it's a heavy header most systems don't need in
// full); CreateQuery<>() below needs the complete type.
#include "ECS/World.h"

// The concrete type behind RunAfter<HierarchyTransformSystem>() must be complete here (a
// forward declaration is not enough - see IECSSystems.h's note on RunAfter/RunBefore), hence
// this include living in the .cpp rather than the header.
#include "ECS/HierarchyTransformSystem.h"
#include "Resources/ResourceManager.h"

namespace Crucible {

    void RenderExtractSystem::Describe(SystemDesc& desc) const {

        desc.Uses<ExtractQuery>()                    // reads WorldTransform + MeshRenderer
            .Uses<LightQuery>()                      // reads WorldTransform + Light
            .Uses<SkyboxQuery>()                     // reads Skybox
            .RunAfter<HierarchyTransformSystem>()    // WorldTransform must already be this frame's
            .Affinity(SystemAffinity::MainThread);   // touches the Renderer; not thread-safe today

    }   // RenderExtractSystem::Describe()

    void RenderExtractSystem::Update(World& world, [[maybe_unused]] SystemContext& ctx) {

        // Built once, against this World, then reused every frame (same pattern as
        // HierarchyTransformSystem's own NodeQuery / m_nodes).
        if (!m_query) { m_query.emplace(world.CreateQuery<const WorldTransform, const MeshRenderer>()); }

        // Meshes are PERSISTENT render proxies, not per-frame Submit()s: the Renderer keeps them in
        // its SpatialIndex for per-view culling and editor picking. Every frame each entity's proxy
        // is refreshed (SetRenderProxy is a no-op when nothing changed), and proxies whose entity
        // wasn't seen this frame are destroyed.
        ++m_frame;
        m_query->ForEach([this](Entity entity, const WorldTransform& worldTransform, const MeshRenderer& meshRenderer) {

            if (entity.index >= m_records.size()) { m_records.resize(static_cast<size_t>(entity.index) + 1); }
            ProxyRecord& record = m_records[entity.index];
            if (!record.proxy.IsValid()) {
                record.proxy      = m_renderer.CreateRenderProxy(EntityToUserData(entity));   // Raycast() hands this back
                record.generation = entity.generation;
                m_activeRecords.push_back(entity.index);
            } else if (record.generation != entity.generation) {
                // The slot was reused by a new entity before the sweep noticed the old one died:
                // replace the proxy (the index is already in m_activeRecords).
                m_renderer.DestroyRenderProxy(record.proxy);
                record.proxy      = m_renderer.CreateRenderProxy(EntityToUserData(entity));
                record.generation = entity.generation;
            }
            record.seenFrame = m_frame;

            // Hidden, or not yet computed this frame (e.g. spawned during PreRender - see
            // WorldTransform's own doc comment): keep the proxy, but out of culling and picking.
            if (!meshRenderer.visible || !worldTransform.valid) {
                m_renderer.SetRenderProxy(record.proxy, {}, {}, worldTransform.matrix, false);
                return;
            }

            // Asset ids win over handles when set (see MeshRenderer): one cached hash lookup
            // each, loading the asset on first use. This is what makes scene-loaded entities -
            // which carry ids but no handles - draw without any extra resolve step.
            MeshHandle     mesh     = meshRenderer.mesh;
            MaterialHandle material = meshRenderer.material;
            if (m_resources) {
                if (meshRenderer.meshAsset != 0)     { mesh     = m_resources->GetMesh(meshRenderer.meshAsset); }
                if (meshRenderer.materialAsset != 0) { material = m_resources->GetMaterial(meshRenderer.materialAsset); }
            }

            // Invalid material (never assigned, or its asset failed to load) falls back to the
            // Renderer's default material rather than skipping the draw - an untextured/neutral-
            // grey cube beats an invisible one.
            if (!material.IsValid()) { material = m_renderer.GetDefaultMaterial(); }

            m_renderer.SetRenderProxy(record.proxy, mesh, material, worldTransform.matrix, mesh.IsValid());

        });

        // Sweep: entities that no longer have a MeshRenderer (or no longer exist).
        for (size_t i = m_activeRecords.size(); i-- > 0;) {
            ProxyRecord& record = m_records[m_activeRecords[i]];
            if (record.seenFrame == m_frame) { continue; }
            m_renderer.DestroyRenderProxy(record.proxy);
            record = ProxyRecord{};
            m_activeRecords[i] = m_activeRecords.back();
            m_activeRecords.pop_back();
        }

        // Lights: position = the world matrix's translation, direction = its local -Z axis (see
        // the Light component's doc comment). Normalizing the axis (done in SubmitLight) strips
        // any scale the entity or a parent carries.
        if (!m_lightQuery) { m_lightQuery.emplace(world.CreateQuery<const WorldTransform, const Light>()); }

        m_lightQuery->ForEach([this](Entity, const WorldTransform& worldTransform, const Light& light) {

            if (!light.enabled || !worldTransform.valid) { return; }

            const glm::mat4& m = worldTransform.matrix;
            LightData data;
            data.type         = light.type;
            data.position     = glm::vec3(m[3]);
            data.direction    = -glm::vec3(m[2]);   // local +Z column, negated = forward
            data.colour       = light.colour;
            data.intensity    = light.intensity;
            data.range        = light.range;
            data.innerConeDeg = light.innerConeDeg;
            data.outerConeDeg = light.outerConeDeg;
            data.castShadows  = light.castShadows;
            m_renderer.SubmitLight(data);

        });

        // Environment: the scene's Skybox (last one visited wins if there are several). With none,
        // the Renderer simply keeps its defaults for the frame.
        if (!m_skyboxQuery) { m_skyboxQuery.emplace(world.CreateQuery<const Skybox>()); }

        m_skyboxQuery->ForEach([this](Entity, const Skybox& skybox) {

            EnvironmentSettings env;
            env.skybox           = (m_resources && skybox.skyboxAsset != 0)
                                       ? m_resources->GetCubemap(skybox.skyboxAsset)
                                       : skybox.cubemap;   // Invalid -> the Renderer's built-in sky
            env.drawSkybox       = skybox.visible;
            env.skyboxIntensity  = skybox.intensity;
            env.ambientSky       = skybox.ambientSky;
            env.ambientGround    = skybox.ambientGround;
            env.ambientIntensity = skybox.ambientIntensity;
            env.exposure         = skybox.exposure;
            m_renderer.SetEnvironment(env);

        });

    }   // RenderExtractSystem::Update()

}   // NAMESPACE CRUCIBLE
