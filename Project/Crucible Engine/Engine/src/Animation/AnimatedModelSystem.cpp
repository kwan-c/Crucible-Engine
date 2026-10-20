#include "Animation/AnimatedModelSystem.h"
#include "ECS/World.h"
#include "Resources/ResourceManager.h"
#include "Systems/PlayMode.h"
#include "Diagnostics/Logger.h"

namespace Crucible {
    void AnimatedModelSystem::Describe(SystemDesc& desc) const {
        desc.Uses<ModelQuery>().Affinity(SystemAffinity::MainThread);
    }

    void AnimatedModelSystem::Update(World& world, [[maybe_unused]] SystemContext& ctx) {
        if (!m_query) m_query.emplace(world.CreateQuery<const MeshRenderer, AnimatedModel>());
        const PlayState state = m_playMode.State();
        const bool restart = m_sessionRevision != m_playMode.SessionRevision();
        const bool stopped = restart && state == PlayState::Edit;
        m_sessionRevision = m_playMode.SessionRevision();
        m_query->ForEach([&](Entity, const MeshRenderer& renderer, AnimatedModel& animation) {
            const bool changedAsset = !animation.initialized || animation.evaluatedAsset != renderer.modelAsset;
            const bool changedClip = animation.evaluatedClip != animation.clip;
            const auto model = renderer.modelAsset ? m_resources.GetModel(renderer.modelAsset) : nullptr;
            if (changedAsset || restart) animation.playing = animation.playOnStart;
            animation.initialized = true;
            animation.evaluatedAsset = renderer.modelAsset;
            animation.evaluatedClip = animation.clip;
            if (!model) {
                animation.time = 0.0f; animation.playing = false; animation.poseValid = false;
                animation.pose = ModelPose{};
                animation.poseFailed = false;
                return;
            }
            const bool validClip = animation.clip < model->animations.size();
            const float duration = validClip ? model->animations[animation.clip].duration : 0.0f;
            if (changedAsset || changedClip || restart)
                animation.time = !stopped && animation.playing && animation.speed < 0.0f ? duration : 0.0f;
            const bool advance = animation.playing && !stopped &&
                (state == PlayState::Playing || (state == PlayState::Edit && animation.previewInEditor));
            if (advance && validClip) {
                const auto step = StepModelPlayback(animation.time, world.DeltaTime(), animation.speed, duration, animation.loop);
                animation.time = step.time;
                if (step.finished) animation.playing = false;
            }
            animation.poseValid = EvaluateModelPose(*model, validClip ? animation.clip : InvalidModelIndex,
                                                    animation.time, animation.pose);
            if (!animation.poseValid && !animation.poseFailed)
                CRU_WARN("[Animation] Model {} produced non-finite transforms; skipping its pose", renderer.modelAsset);
            animation.poseFailed = !animation.poseValid;
        });
    }
}
