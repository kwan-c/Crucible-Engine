#pragma once
#include <memory>
#include "Resources/ModelResource.h"
#include "Graphics/MeshHandle.h"
#include "Graphics/MaterialHandle.h"
#include "Graphics/ModelBounds.h"

namespace Crucible {
    // Shared uploads; mutable clocks, poses and render proxies belong to the instance system.
    struct RenderedModel {
        std::shared_ptr<const ModelResource> definition;
        std::vector<std::vector<MeshHandle>> meshes;
        std::vector<MaterialHandle> materials;
        std::vector<std::vector<ModelPrimitiveBounds>> bounds;
    };
}
