#pragma once

#include "RenderTypes.h"

namespace Crucible {

    // Attach to an entity (together with your Transform component) to have it drawn.
    // It stores only a handle: the ECS never owns or touches Vulkan objects.
    struct MeshRendererComponent {
        MeshHandle mesh;
        bool       visible = true;
        // Future: MaterialHandle material;  uint32_t layerMask;  bool castShadows; ...
    };

}   // NAMESPACE CRUCIBLE

/*
    ---- Example extraction system (adapt the iteration syntax to your ECS) ----

    // Runs once per frame BEFORE Renderer::Update(dt). It's the "POPULATE RENDER QUEUE" step.
    void RenderExtractSystem(Registry& registry, Renderer& renderer) {
        registry.ForEach<TransformComponent, MeshRendererComponent>(
            [&](Entity, TransformComponent& tf, MeshRendererComponent& mr) {
                if (!mr.visible || !mr.mesh.IsValid()) return;
                renderer.Submit(mr.mesh, tf.GetMatrix());   // T * R * S
            });
    }

    // Loading a mesh (once, e.g. from an asset manager or model loader):
    MeshHandle h = renderer.CreateMesh(meshData);
    registry.Add<MeshRendererComponent>(entity, { h });
*/
