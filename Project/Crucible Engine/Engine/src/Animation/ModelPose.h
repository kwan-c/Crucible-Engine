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

    struct ModelPlaybackStep {
        float time = 0.0f; // relative to the clip's first key
        bool finished = false;
    };

    // Pure clock calculation; callers own Play/Pause/Stop and gate paused updates.
    // Nonnegative elapsed seconds, signed speed. Invalid elapsed time/speed
    // freezes the clock; invalid time resets to zero. Nonpositive/non-finite
    // duration finishes immediately, even when looping is selected.
    ENGINE_API ModelPlaybackStep StepModelPlayback(float time, double deltaSeconds,
        float speed, float duration, bool loop);
    // Compatibility helper for callers that already have a signed time delta.
    ENGINE_API float AdvanceModelTime(float time, float delta, float duration, bool loop);

    // All evaluation APIs require a validated engine ModelResource (GetModel).
    // Time is relative to the clip's start. Sampling clamps to the clip/key range;
    // looping belongs to the clock. InvalidModelIndex selects authored defaults.
    // Other invalid clip indices return false and leave local at those defaults.
    ENGINE_API bool SampleModelLocalPose(const ModelResource& model, uint32_t clip,
        float time, std::vector<ModelLocalTransform>& local);

    // Consumes pose.local. Future blending can edit local TRS before this step.
    // Rigid nodes and joints use the same hierarchy; palettes are mesh-local.
    // A singular/unrepresentable binding has an empty palette; other bindings
    // survive. Invalid local/global matrices return false and clear evaluated
    // arrays, so callers must skip that pose. No NaN/Inf matrices are exposed.
    ENGINE_API bool BuildModelPose(const ModelResource& model, ModelPose& pose);
    ENGINE_API bool EvaluateModelPose(const ModelResource& model, uint32_t clip,
        float time, ModelPose& pose);

    // InvalidModelIndex selects the complete model in its authored coordinates.
    // A selected subtree is rebased to its root; palettes remain mesh-local.
    ENGINE_API bool IsModelNodeInSubtree(const ModelResource& model, uint32_t node, uint32_t root);
    ENGINE_API bool ModelRootInverse(const ModelPose& pose, uint32_t root, glm::mat4& inverse);
    ENGINE_API bool ModelNodeTransform(const ModelResource& model, const ModelPose& pose,
        uint32_t root, uint32_t node, glm::mat4& transform);
}
