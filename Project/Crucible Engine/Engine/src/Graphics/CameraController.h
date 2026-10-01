#pragma once
/*************************************************************************
* \file:         CameraController.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                Input-driven fly camera controller built on top of Camera.
*
* \details
*                Declares CameraInput (the plain-data struct the input
*                system fills once per frame) and FlyCameraController
*                (which consumes a CameraInput and a dt to mutate a
*                Camera). Keeps Camera.h itself free of any notion of
*                input devices or frame timing - see Camera.h's own file
*                header for why that separation exists.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include "Camera.h"

namespace Crucible {

    /*************************************************************************
     \brief
        The ONLY thing the input system has to produce to drive a camera.

     \details
        Whoever writes the input system just fills this struct once per
        frame; the camera code (FlyCameraController below) never touches
        GLFW or key codes directly.
     *************************************************************************/
    struct CameraInput {
        glm::vec3 move{ 0.0f };   // x = right(+)/left(-), y = up(+)/down(-), z = forward(+)/back(-); each in [-1, 1]
        glm::vec2 look{ 0.0f };   // mouse delta in pixels this frame (+x = right, +y = down)
        float     zoom  = 0.0f;   // scroll wheel delta (+ = zoom in)
        bool      boost = false;  // hold-to-sprint (e.g. Shift)
        bool      active = true;  // e.g. false unless right mouse button is held in the editor
    };

    /*************************************************************************
     \brief
        Applies one frame's CameraInput to a Camera: look, move, and zoom.

     \details
        Stateless beyond its own tunable speed/sensitivity fields - all
        per-frame state lives in the CameraInput passed to Update().
     *************************************************************************/
    class FlyCameraController {
    public:
        float moveSpeed        = 4.0f;    // units / second
        float boostMultiplier  = 3.0f;
        float lookSensitivity  = 0.10f;   // degrees per pixel
        float zoomSpeed        = 2.0f;    // degrees of FOV per scroll tick
        float minFov           = 20.0f;
        float maxFov           = 100.0f;

        /*********************************************************************
         \brief
            Applies one frame's look/move/zoom input to the given camera.

         \param[in] cam The camera to mutate.
         \param[in] in  This frame's input (see CameraInput).
         \param[in] dt  Frame delta time, in seconds.

         \details
            Look uses the mouse delta directly (already "per frame", so it
            is NOT scaled by dt). Move is normalized so diagonal input
            isn't faster than axis-aligned input, then scaled by
            moveSpeed * dt (or moveSpeed * boostMultiplier * dt while
            boosting). Zoom adjusts fovY, clamped to [minFov, maxFov].
         *********************************************************************/
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
