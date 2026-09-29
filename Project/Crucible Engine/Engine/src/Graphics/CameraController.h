#pragma once

#include "Camera.h"

namespace Crucible {

    // The ONLY thing the input system has to produce. Whoever writes the input system just
    // fills this struct once per frame; the camera code never touches GLFW or key codes.
    struct CameraInput {
        glm::vec3 move{ 0.0f };   // x = right(+)/left(-), y = up(+)/down(-), z = forward(+)/back(-); each in [-1, 1]
        glm::vec2 look{ 0.0f };   // mouse delta in pixels this frame (+x = right, +y = down)
        float     zoom  = 0.0f;   // scroll wheel delta (+ = zoom in)
        bool      boost = false;  // hold-to-sprint (e.g. Shift)
        bool      active = true;  // e.g. false unless right mouse button is held in the editor
    };

    class FlyCameraController {
    public:
        float moveSpeed        = 4.0f;    // units / second
        float boostMultiplier  = 3.0f;
        float lookSensitivity  = 0.10f;   // degrees per pixel
        float zoomSpeed        = 2.0f;    // degrees of FOV per scroll tick
        float minFov           = 20.0f;
        float maxFov           = 100.0f;

        void Update(Camera& cam, const CameraInput& in, float dt) const {
            if (!in.active) return;

            // Look: mouse delta is already "per frame", so it is NOT scaled by dt.
            cam.Rotate(in.look.x * lookSensitivity, -in.look.y * lookSensitivity);

            // Move: normalise so diagonals aren't faster, then scale by speed * dt.
            glm::vec3 dir = in.move;
            if (glm::dot(dir, dir) > 1.0f) dir = glm::normalize(dir);
            const float speed = moveSpeed * (in.boost ? boostMultiplier : 1.0f);
            cam.Translate(dir * speed * dt);

            // Zoom
            if (in.zoom != 0.0f)
                cam.fovY = glm::clamp(cam.fovY - in.zoom * zoomSpeed, minFov, maxFov);
        }
    };

}   // NAMESPACE CRUCIBLE
