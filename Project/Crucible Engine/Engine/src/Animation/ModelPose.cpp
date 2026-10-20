#include "Animation/ModelPose.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Crucible {
    namespace {
        float ResolveTime(double next, float duration, bool loop) {
            if (!loop) return static_cast<float>(std::clamp(next, 0.0, double(duration)));
            double wrapped = std::fmod(next, double(duration));
            if (wrapped < 0.0) wrapped += duration;
            // Narrowing a value just below duration can round up to duration.
            // Preserve [0,duration), including the end of a reverse loop.
            return std::min(static_cast<float>(wrapped), std::nextafter(duration, 0.0f));
        }

        bool Finite(const glm::mat4& matrix) {
            for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
                if (!std::isfinite(matrix[c][r])) return false;
            return true;
        }

        bool ClearEvaluated(ModelPose& pose) {
            pose.global.clear();
            pose.palettes.clear();
            return false;
        }
    }

    float AdvanceModelTime(float time, float delta, float duration, bool loop) {
        if (!std::isfinite(duration) || !(duration > 0.0f)) return 0.0f;
        if (!std::isfinite(time)) time = 0.0f;
        if (!std::isfinite(delta)) delta = 0.0f;
        return ResolveTime(double(time) + delta, duration, loop);
    }

    ModelPlaybackStep StepModelPlayback(float time, double deltaSeconds, float speed,
                                       float duration, bool loop) {
        if (!std::isfinite(duration) || !(duration > 0.0f)) return {0.0f, true};
        if (!std::isfinite(time)) time = 0.0f;
        ModelPlaybackStep result{std::clamp(time, 0.0f, duration), false};
        // Preserve a scrubbed final key when no time actually advances.
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0 ||
            !std::isfinite(speed) || speed == 0.0f || deltaSeconds == 0.0) return result;
        const double delta = deltaSeconds * double(speed);
        const double next = double(result.time) + delta;
        if (!std::isfinite(next)) return result;
        result.time = ResolveTime(next, duration, loop);
        result.finished = !loop && ((delta > 0.0 && next >= duration) || (delta < 0.0 && next <= 0.0));
        return result;
    }

    bool SampleModelLocalPose(const ModelResource& model, uint32_t clipIndex, float time,
                              std::vector<ModelLocalTransform>& local) {
        local.resize(model.nodes.size());
        for (size_t i = 0; i < local.size(); ++i) local[i] = model.nodes[i].local;
        if (clipIndex == InvalidModelIndex) return true;
        if (clipIndex >= model.animations.size()) return false;
        const auto& clip = model.animations[clipIndex];
        // Preserve fractional times when original timestamps are very large.
        const double at = double(clip.startTime) +
            std::clamp(std::isfinite(time) ? double(time) : 0.0, 0.0, double(clip.duration));
        for (const auto& channel : clip.channels) {
            const auto& sampler = clip.samplers[channel.sampler];
            const auto upper = std::upper_bound(sampler.times.begin(), sampler.times.end(), at);
            const size_t a = upper == sampler.times.begin() ? 0 : size_t(upper - sampler.times.begin() - 1);
            const size_t b = std::min(a + 1, sampler.times.size() - 1);
            float t = 0.0f;
            if (sampler.interpolation == AnimationInterpolation::Linear && a != b)
                t = static_cast<float>(std::clamp((at - sampler.times[a]) /
                    (double(sampler.times[b]) - sampler.times[a]), 0.0, 1.0));
            auto& transform = local[channel.node];
            if (channel.target == AnimationTarget::Rotation) {
                const auto& va = sampler.values[a]; const auto& vb = sampler.values[b];
                // Import permits small quantization/export error in unit keys.
                // Normalize both endpoints before slerp, not just its result.
                const auto qa = glm::normalize(glm::quat(va.w, va.x, va.y, va.z));
                const auto qb = glm::normalize(glm::quat(vb.w, vb.x, vb.y, vb.z));
                transform.rotation = t == 0.0f ? qa : glm::normalize(glm::slerp(qa, qb, t));
            } else {
                const glm::vec3 value = glm::mix(glm::vec3(sampler.values[a]), glm::vec3(sampler.values[b]), t);
                if (channel.target == AnimationTarget::Translation) transform.translation = value;
                else transform.scale = value;
            }
        }
        return true;
    }

    bool BuildModelPose(const ModelResource& model, ModelPose& pose) {
        if (pose.local.size() != model.nodes.size()) return ClearEvaluated(pose);
        pose.global.resize(model.nodes.size());
        pose.palettes.resize(model.nodes.size());
        for (auto& palette : pose.palettes) palette.clear();
        for (uint32_t index : model.nodeOrder) {
            const auto& node = model.nodes[index]; const auto& transform = pose.local[index];
            const glm::mat4 matrix = transform.hasMatrix ? transform.matrix :
                glm::translate(glm::mat4(1), transform.translation) * glm::mat4_cast(transform.rotation) *
                glm::scale(glm::mat4(1), transform.scale);
            if (!Finite(matrix)) return ClearEvaluated(pose);
            pose.global[index] = node.parent == InvalidModelIndex ? matrix : pose.global[node.parent] * matrix;
            if (!Finite(pose.global[index])) return ClearEvaluated(pose);
        }
        for (size_t index = 0; index < model.nodes.size(); ++index) {
            auto& palette = pose.palettes[index];
            const auto& node = model.nodes[index];
            if (node.mesh == InvalidModelIndex || node.skin == InvalidModelIndex) continue;
            const auto& skin = model.skins[node.skin];
            // Test actual singularity in double precision. Small invertible
            // scales must not disappear because a float determinant is tiny.
            const glm::dmat4 meshGlobal(pose.global[index]);
            const double determinant = glm::determinant(meshGlobal);
            if (!std::isfinite(determinant) || determinant == 0.0) continue;
            const glm::dmat4 meshInverse = glm::inverse(meshGlobal);
            palette.resize(skin.jointNodes.size());
            for (size_t joint = 0; joint < palette.size(); ++joint) {
                palette[joint] = glm::mat4(meshInverse * glm::dmat4(pose.global[skin.jointNodes[joint]]) *
                                          glm::dmat4(skin.inverseBindMatrices[joint]));
                if (!Finite(palette[joint])) { palette.clear(); break; }
            }
        }
        return true;
    }

    bool EvaluateModelPose(const ModelResource& model, uint32_t clipIndex, float time, ModelPose& pose) {
        if (!SampleModelLocalPose(model, clipIndex, time, pose.local)) return ClearEvaluated(pose);
        return BuildModelPose(model, pose);
    }
}
