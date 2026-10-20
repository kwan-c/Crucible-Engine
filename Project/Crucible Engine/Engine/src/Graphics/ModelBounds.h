#pragma once
#include "Resources/ModelResource.h"
#include "Spatial/Bounds.h"
#include <span>

namespace Crucible {
    // Immutable influence envelopes shared by every instance of a primitive.
    // Joint indices are skin-local; the binding supplies its own palette later.
    struct ModelPrimitiveBounds {
        AABB rest;
        AABB unweighted;
        std::vector<AABB> joints;
    };

    inline ModelPrimitiveBounds BuildModelPrimitiveBounds(const ModelPrimitive& primitive) {
        ModelPrimitiveBounds result;
        for (const auto& vertex : primitive.vertices) {
            result.rest.min = glm::min(result.rest.min, vertex.position);
            result.rest.max = glm::max(result.rest.max, vertex.position);
            bool weighted = false;
            if (primitive.hasSkinning) for (int i = 0; i < 4; ++i) {
                if (vertex.weights[i] <= 0.0f) continue;
                const auto joint = vertex.joints[i];
                if (joint >= result.joints.size()) result.joints.resize(size_t(joint) + 1);
                auto& bounds = result.joints[joint];
                bounds.min = glm::min(bounds.min, vertex.position);
                bounds.max = glm::max(bounds.max, vertex.position);
                weighted = true;
            }
            if (!weighted) {
                result.unweighted.min = glm::min(result.unweighted.min, vertex.position);
                result.unweighted.max = glm::max(result.unweighted.max, vertex.position);
            }
        }
        return result;
    }

    // Round outward using the magnitudes BEFORE cancellation. The shader combines
    // world and blended palette matrices before transforming a vertex, whereas
    // bounds transform envelopes in stages; their float rounding can differ.
    // 32 eps covers the four-influence blend and affine matrix/vector operations.
    inline AABB PadModelBounds(const AABB& bounds, const glm::dvec3& arithmeticMagnitude = glm::dvec3(0.0)) {
        if (!bounds.IsValid()) return {};
        const auto magnitude = glm::max(glm::dvec3(1.0), glm::max(arithmeticMagnitude,
            glm::max(glm::abs(glm::dvec3(bounds.min)), glm::abs(glm::dvec3(bounds.max)))));
        const auto padding = magnitude * (32.0 * std::numeric_limits<float>::epsilon());
        AABB result;
        for (int i = 0; i < 3; ++i) {
            result.min[i] = std::nextafter(static_cast<float>(double(bounds.min[i]) - padding[i]),
                                         -std::numeric_limits<float>::infinity());
            result.max[i] = std::nextafter(static_cast<float>(double(bounds.max[i]) + padding[i]),
                                         std::numeric_limits<float>::infinity());
            if (!std::isfinite(result.min[i]) || !std::isfinite(result.max[i])) return {};
        }
        return result;
    }

    inline AABB TransformModelBounds(const AABB& source, const glm::mat4& transform) {
        if (!source.IsValid()) return {};
        const glm::dmat4 matrix(transform);
        const glm::dvec3 minimum(source.min), maximum(source.max);
        const auto center = glm::dvec3(matrix * glm::dvec4((minimum + maximum) * 0.5, 1.0));
        const glm::dmat3 absolute(glm::abs(glm::dvec3(matrix[0])), glm::abs(glm::dvec3(matrix[1])),
                                  glm::abs(glm::dvec3(matrix[2])));
        const auto extents = absolute * ((maximum - minimum) * 0.5);
        const auto arithmeticMagnitude = absolute * glm::max(glm::abs(minimum), glm::abs(maximum)) +
                                         glm::abs(glm::dvec3(matrix[3]));
        return PadModelBounds(AABB(glm::vec3(center - extents), glm::vec3(center + extents)), arithmeticMagnitude);
    }

    inline AABB EvaluateModelPrimitiveBounds(const ModelPrimitiveBounds& source,
                                             std::span<const glm::mat4> palette) {
        if (palette.empty()) return source.rest; // rigid/unbound mesh
        if (palette.size() < source.joints.size()) return {};
        AABB result = source.unweighted;
        for (size_t joint = 0; joint < source.joints.size(); ++joint) {
            if (!source.joints[joint].IsValid()) continue;
            const auto transformed = TransformModelBounds(source.joints[joint], palette[joint]);
            if (!transformed.IsValid()) return {}; // never retain a partial box after overflow
            result = AABB::Union(result, transformed);
        }
        return PadModelBounds(result);
    }
}
