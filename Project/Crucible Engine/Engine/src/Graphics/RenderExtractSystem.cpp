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

    }   // RenderExtractSystem::Update()

}   // NAMESPACE CRUCIBLE
