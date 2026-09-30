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

        m_query->ForEach([this](Entity, const WorldTransform& worldTransform, const MeshRenderer& meshRenderer) {

            // Skip: hidden, not yet computed this frame (e.g. spawned during PreRender - see
            // WorldTransform's own doc comment), or no mesh uploaded yet (model still loading).
            if (!meshRenderer.visible || !worldTransform.valid || !meshRenderer.mesh.IsValid()) { return; }

            // Invalid material (never assigned, or its texture/material asset is still
            // loading) falls back to the Renderer's default material rather than skipping
            // the draw - an untextured/neutral-grey cube beats an invisible one.
            const MaterialHandle material = meshRenderer.material.IsValid()
                ? meshRenderer.material
                : m_renderer.GetDefaultMaterial();

            m_renderer.Submit(meshRenderer.mesh, material, worldTransform.matrix);

        });

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
            m_renderer.SubmitLight(data);

        });

        // Environment: the scene's Skybox (last one visited wins if there are several). With none,
        // the Renderer simply keeps its defaults for the frame.
        if (!m_skyboxQuery) { m_skyboxQuery.emplace(world.CreateQuery<const Skybox>()); }

        m_skyboxQuery->ForEach([this](Entity, const Skybox& skybox) {

            EnvironmentSettings env;
            env.skybox           = skybox.cubemap;   // Invalid -> the Renderer's built-in sky
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
