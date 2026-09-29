#pragma once

#include <cmath>
#include "Math/Math.h"   // pulls in glm (same header the renderer already uses)

namespace Crucible {

    // Pure camera state + matrix math. No Vulkan, no input, no ECS dependencies, so it can be
    // unit tested and driven by anything (fly controller, editor orbit cam, cutscene, ECS component...).
    //
    // Conventions:
    //   - Right-handed, Y-up world, camera looks down -Z at yaw = -90 deg.
    //   - Projection is the standard GLM one (Y up). Your viewport already flips Y with a negative
    //     height, so do NOT flip proj[1][1] here.
    //   - Assumes GLM_FORCE_DEPTH_ZERO_TO_ONE (Vulkan clip space), as your current code does.
    class Camera {
    public:
        static constexpr glm::vec3 WorldUp{ 0.0f, 1.0f, 0.0f };

        // Placement
        glm::vec3 position{ 0.0f, 1.5f, 4.0f };
        float     yaw   = -90.0f;   // degrees, rotation around WorldUp
        float     pitch = -15.0f;   // degrees, clamped to +-89 to avoid gimbal flip

        // Lens
        float fovY   = 60.0f;       // degrees
        float aspect = 16.0f / 9.0f;
        float nearZ  = 0.1f;
        float farZ   = 1000.0f;

        // --- Directions (derived from yaw/pitch) ---
        glm::vec3 Forward() const {
            const float y = glm::radians(yaw), p = glm::radians(pitch);
            return glm::normalize(glm::vec3{ std::cos(y) * std::cos(p), std::sin(p), std::sin(y) * std::cos(p) });
        }
        glm::vec3 Right() const { return glm::normalize(glm::cross(Forward(), WorldUp)); }
        glm::vec3 Up()    const { return glm::normalize(glm::cross(Right(), Forward())); }

        // --- Matrices ---
        glm::mat4 GetView()       const { return glm::lookAt(position, position + Forward(), WorldUp); }
        glm::mat4 GetProjection() const { return glm::perspective(glm::radians(fovY), aspect, nearZ, farZ); }
        glm::mat4 GetViewProjection() const { return GetProjection() * GetView(); }

        // --- Mutators ---
        void SetAspect(float w, float h) { if (h > 0.0f) aspect = w / h; }

        void Rotate(float deltaYawDeg, float deltaPitchDeg) {
            yaw   = glm::mod(yaw + deltaYawDeg, 360.0f);
            pitch = glm::clamp(pitch + deltaPitchDeg, -89.0f, 89.0f);
        }

        // Move relative to the camera: x = right, y = up (world), z = forward.
        void Translate(const glm::vec3& local) {
            position += Right() * local.x + WorldUp * local.y + Forward() * local.z;
        }
    };

}   // NAMESPACE CRUCIBLE
