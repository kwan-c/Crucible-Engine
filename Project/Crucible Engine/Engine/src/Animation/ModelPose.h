#pragma once
#include "EngineExport.h"
#include "Resources/ModelResource.h"

namespace Crucible {
    // Per-instance state. The shared ModelResource is never modified by evaluation.
    struct ModelPose {
        std::vector<ModelLocalTransform> local;
        std::vector<glm::mat4> global;
        // One palette per mesh NODE / skin binding, in that skin's joint order.
        std::vector<std::vector<glm::mat4>> palettes;
    };

    // Time is relative to the clip's start; InvalidModelIndex selects the authored rest pose.
    ENGINE_API void EvaluateModelPose(const ModelResource& model, uint32_t clip, float time, ModelPose& pose);
    ENGINE_API float AdvanceModelTime(float time, float delta, float duration, bool loop);
}
