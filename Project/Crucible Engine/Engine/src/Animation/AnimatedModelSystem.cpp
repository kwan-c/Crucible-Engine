#include "Animation/AnimatedModelSystem.h"
#include "ECS/World.h"
#include "ECS/HierarchyTransformSystem.h"
#include "Graphics/RenderSystem.h"
#include "Resources/ResourceManager.h"
#include "Systems/PlayMode.h"
#include "Diagnostics/Logger.h"
#include <algorithm>
#include <cmath>

namespace Crucible {
    void AnimatedModelSystem::Describe(SystemDesc& desc) const {
        desc.Uses<ModelQuery>().RunAfter<HierarchyTransformSystem>().Affinity(SystemAffinity::MainThread);
    }
    void AnimatedModelSystem::Release(Instance& instance) {
        for (const auto& part : instance.parts) m_renderer.DestroyRenderProxy(part.proxy);
        instance = Instance{};
    }
    void AnimatedModelSystem::CreateParts(Instance& instance, Entity entity) {
        const auto& model = *instance.resource->definition;
        std::vector<uint32_t> pending = model.sceneRoots;
        while (!pending.empty()) {
            const uint32_t nodeIndex = pending.back(); pending.pop_back();
            const auto& node = model.nodes[nodeIndex];
            pending.insert(pending.end(), node.children.begin(), node.children.end());
            if (node.mesh == InvalidModelIndex) continue;
            const auto& mesh = model.meshes[node.mesh];
            for (uint32_t primitive = 0; primitive < mesh.primitives.size(); ++primitive) {
                Part part;
                part.node = nodeIndex; part.primitive = primitive;
                part.proxy = m_renderer.CreateRenderProxy(EntityToUserData(entity));
                const auto& source = mesh.primitives[primitive];
                const bool skinned = source.hasSkinning && node.skin != InvalidModelIndex;
                if (skinned) part.jointBounds.resize(model.skins[node.skin].jointNodes.size());
                // Influence envelopes enclose every weighted result (weights are nonnegative and sum to 1).
                // Runtime bounds need only transform these boxes, not skin the whole mesh on the CPU.
                for (const auto& vertex : source.vertices) {
                    bool weighted = false;
                    if (skinned) for (int influence = 0; influence < 4; ++influence) {
                        if (vertex.weights[influence] <= 0.0f) continue;
                        auto& bounds = part.jointBounds[vertex.joints[influence]];
                        bounds.min = glm::min(bounds.min, vertex.position);
                        bounds.max = glm::max(bounds.max, vertex.position);
                        weighted = true;
                    }
                    if (!weighted) {
                        part.unweightedBounds.min = glm::min(part.unweightedBounds.min, vertex.position);
                        part.unweightedBounds.max = glm::max(part.unweightedBounds.max, vertex.position);
                    }
                }
                instance.parts.push_back(std::move(part));
            }
        }
    }
    void AnimatedModelSystem::Update(World& world, [[maybe_unused]] SystemContext& ctx) {
        if (!m_query) m_query.emplace(world.CreateQuery<const WorldTransform, AnimatedModel>());
        ++m_frame;
        const PlayState state = m_playMode.State();
        const bool stopped = state == PlayState::Edit && m_previousState != PlayState::Edit;
        m_previousState = state;
        m_query->ForEach([&](Entity entity, const WorldTransform& transform, AnimatedModel& animation) {
            auto& instance = m_instances[entity.index];
            if (instance.generation != entity.generation || instance.asset != animation.modelAsset) {
                Release(instance);
                instance.generation = entity.generation; instance.asset = animation.modelAsset;
                instance.resource = m_resources.GetRenderedModel(animation.modelAsset);
                if (instance.resource) {
                    CreateParts(instance, entity);
                    CRU_INFO("[Animation] {}: {} render parts, {} clips (entity {})", m_resources.GetName(animation.modelAsset),
                        instance.parts.size(), instance.resource->definition->animations.size(), entity.index);
                }
                animation.time = 0.0f;
            }
            instance.seen = m_frame;
            if (!instance.resource) return;
            const auto& model = *instance.resource->definition;
            if (instance.clip != animation.clip || stopped) { animation.time = 0.0f; instance.clip = animation.clip; }
            const bool validClip = animation.clip < model.animations.size();
            const float duration = validClip ? model.animations[animation.clip].duration : 0.0f;
            const bool advance = animation.playing && !stopped &&
                (state == PlayState::Playing || (state == PlayState::Edit && animation.previewInEditor));
            if (advance) {
                const float delta = std::isfinite(animation.speed) ? world.DeltaTime() * animation.speed : 0.0f;
                animation.time = AdvanceModelTime(animation.time, delta, duration, animation.loop);
                if (!animation.loop && ((delta > 0 && animation.time >= duration) || (delta < 0 && animation.time <= 0)))
                    animation.playing = false;
            }
            EvaluateModelPose(model, animation.clip, animation.time, instance.pose);
            for (auto& part : instance.parts) {
                const auto& node = model.nodes[part.node];
                const auto& source = model.meshes[node.mesh].primitives[part.primitive];
                const MaterialHandle material = source.material < instance.resource->materials.size()
                    ? instance.resource->materials[source.material] : m_renderer.GetDefaultMaterial();
                const auto& palette = instance.pose.palettes[part.node];
                AABB bounds = part.unweightedBounds;
                for (size_t joint = 0; joint < part.jointBounds.size() && joint < palette.size(); ++joint) {
                    if (part.jointBounds[joint].IsValid()) bounds = AABB::Union(bounds, AABB::Transform(part.jointBounds[joint], palette[joint]));
                }
                const bool visible = animation.visible && transform.valid &&
                    (part.jointBounds.empty() || !palette.empty());
                m_renderer.SetRenderProxy(part.proxy, instance.resource->meshes[node.mesh][part.primitive], material,
                    transform.matrix * instance.pose.global[part.node], visible);
                m_renderer.SetRenderProxyAnimation(part.proxy, palette, bounds.min, bounds.max);
            }
        });
        for (auto it = m_instances.begin(); it != m_instances.end();) {
            if (it->second.seen != m_frame) { Release(it->second); it = m_instances.erase(it); }
            else ++it;
        }
    }
}
