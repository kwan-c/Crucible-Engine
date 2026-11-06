#include "Animation/AnimatedModelSystem.h"
#include "ECS/World.h"
#include "Resources/ResourceManager.h"
#include "Systems/PlayMode.h"
#include "Diagnostics/Logger.h"
#include <algorithm>
#include <cmath>

namespace Crucible {
    bool AnimatedModelSystem::SetParameter(uint32_t index, uint32_t generation, const ScriptAnimatorCommand& command) {
        if (!m_world || !m_playMode.InSession() || command.name.empty() || command.name.size() > 1024 ||
            command.operation < ScriptAnimatorOperation::Bool || command.operation > ScriptAnimatorOperation::ResetTrigger || !std::isfinite(command.number)) return false;
        const Entity entity{index, generation};
        if (!m_world->IsAlive(entity)) return false;
        const auto* animation = m_world->Read<AnimatedModel>(entity);
        const auto* renderer = m_world->Read<MeshRenderer>(entity);
        if (!animation || !renderer || !animation->controllerAsset || !renderer->modelAsset) return false;
        const auto definition = m_resources.GetAnimatorController(animation->controllerAsset);
        if (!definition) return false;
        const auto type = command.operation == ScriptAnimatorOperation::Bool ? AnimatorParameterType::Bool :
            command.operation == ScriptAnimatorOperation::Float ? AnimatorParameterType::Float :
            command.operation == ScriptAnimatorOperation::Integer ? AnimatorParameterType::Integer : AnimatorParameterType::Trigger;
        if (std::none_of(definition->parameters.begin(), definition->parameters.end(), [&](const auto& parameter) {
                return parameter.name == command.name && parameter.type == type;
            })) return false;
        const uint64_t key = (uint64_t(generation) << 32) | index;
        auto& pending = m_pending[key];
        if (pending.model != renderer->modelAsset || pending.controller != animation->controllerAsset ||
            pending.session != m_playMode.SessionRevision() || pending.definition != definition)
            pending = {renderer->modelAsset, animation->controllerAsset, m_playMode.SessionRevision(), definition, {}};
        // Last write wins per parameter, including SetTrigger/ResetTrigger.
        const auto existing = std::find_if(pending.commands.begin(), pending.commands.end(), [&](const auto& value) { return value.name == command.name; });
        if (existing != pending.commands.end()) *existing = command;
        else {
            if (pending.commands.size() >= 4096) return false;
            pending.commands.push_back(command);
        }
        return true;
    }
    void AnimatedModelSystem::Describe(SystemDesc& desc) const {
        desc.Uses<ModelQuery>().Affinity(SystemAffinity::MainThread);
    }

    void AnimatedModelSystem::Update(World& world, [[maybe_unused]] SystemContext& ctx) {
        if (!m_query) m_query.emplace(world.CreateQuery<const MeshRenderer, AnimatedModel>());
        const PlayState state = m_playMode.State();
        const bool restart = m_sessionRevision != m_playMode.SessionRevision();
        const bool stopped = restart && state == PlayState::Edit;
        m_sessionRevision = m_playMode.SessionRevision();
        m_query->ForEach([&](Entity entity, const MeshRenderer& renderer, AnimatedModel& animation) {
            const auto model = renderer.modelAsset ? m_resources.GetModel(renderer.modelAsset) : nullptr;
            // Same id, new definition: its file was hot reloaded, and the controller still points at the old one.
            const bool changedAsset = !animation.initialized || animation.evaluatedAsset != renderer.modelAsset
                || animation.evaluatedModel != model;
            const bool changedClip = animation.evaluatedClip != animation.clip;
            const bool changedController = animation.evaluatedController != animation.controllerAsset;
            if (changedAsset || changedController || restart) animation.playing = animation.playOnStart;
            animation.initialized = true;
            animation.evaluatedAsset = renderer.modelAsset;
            animation.evaluatedModel = model;
            animation.evaluatedClip = animation.clip;
            animation.evaluatedController = animation.controllerAsset;
            if (changedAsset || changedController || restart) {
                animation.controller = AnimatorInstance{};
                animation.controllerDefinition.reset();
                animation.controllerFailed = false;
            }
            if (!model) {
                animation.time = 0.0f; animation.playing = false; animation.poseValid = false;
                animation.pose = ModelPose{};
                animation.controller = AnimatorInstance{};
                animation.controllerDefinition.reset();
                animation.poseFailed = false;
                return;
            }
            const bool advance = animation.playing && !stopped &&
                (state == PlayState::Playing || (state == PlayState::Edit && animation.previewInEditor));
            if (animation.controllerAsset) {
                const auto definition = m_resources.GetAnimatorController(animation.controllerAsset);
                if (definition != animation.controllerDefinition) {
                    animation.controller = AnimatorInstance{};
                    animation.controllerDefinition = definition;
                    animation.controllerFailed = false;
                }
                if (definition && !animation.controller.Initialized() && !animation.controllerFailed) {
                    std::string error;
                    animation.controllerFailed = !animation.controller.Reset(*definition, *model, &error, animation.speed);
                    if (animation.controllerFailed)
                        CRU_WARN("[Animation] Controller {} is incompatible with model {}: {}", animation.controllerAsset, renderer.modelAsset, error);
                }
                const auto pending = m_pending.find((uint64_t(entity.generation) << 32) | entity.index);
                if (pending != m_pending.end() && animation.controller.Initialized() && m_playMode.InSession()) {
                    const auto& values = pending->second;
                    if (values.model == renderer.modelAsset && values.controller == animation.controllerAsset &&
                        values.session == m_playMode.SessionRevision() && values.definition == definition) {
                        for (const auto& command : values.commands) switch (command.operation) {
                        case ScriptAnimatorOperation::Bool: animation.controller.SetBool(command.name, command.integer != 0); break;
                        case ScriptAnimatorOperation::Float: animation.controller.SetFloat(command.name, command.number); break;
                        case ScriptAnimatorOperation::Integer: animation.controller.SetInteger(command.name, command.integer); break;
                        case ScriptAnimatorOperation::Trigger: animation.controller.SetTrigger(command.name); break;
                        case ScriptAnimatorOperation::ResetTrigger: animation.controller.ResetTrigger(command.name); break;
                        }
                    }
                }
                animation.poseValid = definition && !animation.controllerFailed &&
                    animation.controller.Update(world.DeltaTime(), animation.speed, advance, animation.pose);
                animation.time = animation.controller.Time();
                if (!animation.poseValid) animation.pose = ModelPose{};
                if (definition && !animation.controllerFailed && !animation.poseValid && !animation.poseFailed)
                    CRU_WARN("[Animation] Controller {} produced an invalid pose", animation.controllerAsset);
                animation.poseFailed = !animation.poseValid;
                return;
            }
            const bool validClip = animation.clip < model->animations.size();
            const float duration = validClip ? model->animations[animation.clip].duration : 0.0f;
            if (changedAsset || changedClip || changedController || restart)
                animation.time = !stopped && animation.playing && animation.speed < 0.0f ? duration : 0.0f;
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
        m_pending.clear(); // drops requests for removed owners and replaced models/controllers too
    }
}
