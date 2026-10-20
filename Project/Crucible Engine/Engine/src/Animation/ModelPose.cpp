#include "Animation/ModelPose.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Crucible {
    float AdvanceModelTime(float time, float delta, float duration, bool loop) {
        if (!std::isfinite(time) || !std::isfinite(delta) || !(duration > 0.0f)) return 0.0f;
        const double next = double(time) + delta;
        if (!loop) return static_cast<float>(std::clamp(next, 0.0, double(duration)));
        double wrapped = std::fmod(next, double(duration));
        if (wrapped < 0.0) wrapped += duration;
        return static_cast<float>(wrapped);
    }

    void EvaluateModelPose(const ModelResource& model, uint32_t clipIndex, float time, ModelPose& pose) {
        auto& local = pose.local;
        local.clear();
        local.reserve(model.nodes.size());
        for (const auto& node : model.nodes) local.push_back(node.local);
        if (clipIndex < model.animations.size()) {
            const auto& clip = model.animations[clipIndex];
            const float at = clip.startTime + std::clamp(std::isfinite(time) ? time : 0.0f, 0.0f, clip.duration);
            for (const auto& channel : clip.channels) {
                const auto& sampler = clip.samplers[channel.sampler];
                const auto upper = std::upper_bound(sampler.times.begin(), sampler.times.end(), at);
                const size_t a = upper == sampler.times.begin() ? 0 : size_t(upper - sampler.times.begin() - 1);
                const size_t b = std::min(a + 1, sampler.times.size() - 1);
                float t = 0.0f;
                if (sampler.interpolation == AnimationInterpolation::Linear && a != b)
                    t = std::clamp((at - sampler.times[a]) / (sampler.times[b] - sampler.times[a]), 0.0f, 1.0f);
                auto& transform = local[channel.node];
                const glm::vec4 value = glm::mix(sampler.values[a], sampler.values[b], t);
                if (channel.target == AnimationTarget::Translation) transform.translation = glm::vec3(value);
                else if (channel.target == AnimationTarget::Scale) transform.scale = glm::vec3(value);
                else {
                    const auto& qa = sampler.values[a]; const auto& qb = sampler.values[b];
                    transform.rotation = glm::normalize(glm::slerp(glm::quat(qa.w, qa.x, qa.y, qa.z),
                                                                 glm::quat(qb.w, qb.x, qb.y, qb.z), t));
                }
            }
        }
        pose.global.resize(model.nodes.size());
        pose.palettes.resize(model.nodes.size());
        for (uint32_t index : model.nodeOrder) {
            const auto& node = model.nodes[index]; const auto& transform = local[index];
            const glm::mat4 matrix = transform.hasMatrix ? transform.matrix :
                glm::translate(glm::mat4(1), transform.translation) * glm::mat4_cast(transform.rotation) *
                glm::scale(glm::mat4(1), transform.scale);
            pose.global[index] = node.parent == InvalidModelIndex ? matrix : pose.global[node.parent] * matrix;
        }
        for (size_t index = 0; index < model.nodes.size(); ++index) {
            auto& palette = pose.palettes[index];
            palette.clear();
            const auto& node = model.nodes[index];
            if (node.mesh == InvalidModelIndex || node.skin == InvalidModelIndex) continue;
            const auto& skin = model.skins[node.skin];
            // A zero scale can make mesh space singular. Do not upload NaN matrices.
            if (std::abs(glm::determinant(pose.global[index])) < 1e-12f) continue;
            const glm::mat4 meshInverse = glm::inverse(pose.global[index]);
            palette.resize(skin.jointNodes.size());
            for (size_t joint = 0; joint < palette.size(); ++joint)
                palette[joint] = meshInverse * pose.global[skin.jointNodes[joint]] * skin.inverseBindMatrices[joint];
        }
    }
}
