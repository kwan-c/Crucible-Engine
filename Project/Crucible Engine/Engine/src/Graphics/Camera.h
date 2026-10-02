#pragma once
/*************************************************************************
* \file:         Camera.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                Pure camera state + view/projection matrix math, with no
*                Vulkan, input, or ECS dependencies.
*
* \details
*                Declares Camera: placement (position/yaw/pitch), lens
*                parameters (fovY/aspect/nearZ/farZ), and the
*                view/projection matrices derived from them. Because it
*                has no dependency on Vulkan, input, or the ECS, it can be
*                unit tested and driven by anything - a fly controller
*                (see CameraController.h), an editor orbit camera, a
*                cutscene, or a future ECS component.
*
*                Conventions:
*                  - Right-handed, Y-up world, camera looks down -Z at
*                    yaw = -90 deg.
*                  - Projection is the standard GLM one (Y up). The
*                    viewport already flips Y with a negative height, so
*                    proj[1][1] must NOT be flipped here.
*                  - Assumes GLM_FORCE_DEPTH_ZERO_TO_ONE (Vulkan clip
*                    space).
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include <cmath>
#include "Math/Math.h"   // pulls in glm (same header the renderer already uses)

namespace Crucible {

    /*************************************************************************
     \brief
        Pure camera state (placement + lens) and the view/projection
        matrices derived from it.

     \details
        No Vulkan, input, or ECS dependencies - see the file header for the
        full rationale and the coordinate-system/projection conventions
        this class assumes.
     *************************************************************************/
    class Camera {
    public:
        static constexpr glm::vec3 WorldUp{ 0.0f, 1.0f, 0.0f };

        // Placement
        glm::vec3 position{ 0.0f, 6.0f, 3.5f };
        float     yaw   = -90.0f;   // degrees, rotation around WorldUp
        float     pitch = -45.0f;   // degrees, clamped to +-89 to avoid gimbal flip

        // Lens
        float fovY   = 60.0f;       // degrees
        float aspect = 16.0f / 9.0f;
        float nearZ  = 0.1f;
        float farZ   = 1000.0f;

        // Optional explicit orientation (SetPose): when set, yaw/pitch are ignored and the
        // camera looks along basisForward with basisUp as "up" - so a camera driven by an
        // entity's world matrix keeps its full rotation, roll included.
        bool      useBasis = false;
        glm::vec3 basisForward{ 0.0f, 0.0f, -1.0f };
        glm::vec3 basisUp{ 0.0f, 1.0f, 0.0f };

        /*********************************************************************
         \brief  Places the camera from a world matrix: position = its
                 translation, forward = its local -Z axis, up = its local +Y
                 axis (the same convention Light entities use). Scale is
                 ignored.
         \param[in] world The entity's world matrix.
         *********************************************************************/
        void SetPose(const glm::mat4& world) {
            position = glm::vec3(world[3]);
            glm::vec3 f = -glm::vec3(world[2]);
            glm::vec3 u =  glm::vec3(world[1]);
            const float fl = glm::length(f), ul = glm::length(u);
            basisForward = fl > 1e-6f ? f / fl : glm::vec3(0.0f, 0.0f, -1.0f);
            basisUp      = ul > 1e-6f ? u / ul : glm::vec3(0.0f, 1.0f, 0.0f);
            useBasis     = true;
        }

        /// The "up" reference the view is built with (WorldUp unless SetPose was used).
        glm::vec3 UpReference() const { return useBasis ? basisUp : glm::vec3(WorldUp); }

        /*********************************************************************
         \brief  The camera's forward-facing unit vector, derived from yaw
                 and pitch.
         \return Normalized forward direction.
         *********************************************************************/
        glm::vec3 Forward() const {
            if (useBasis) { return basisForward; }
            const float y = glm::radians(yaw), p = glm::radians(pitch);
            return glm::normalize(glm::vec3{ std::cos(y) * std::cos(p), std::sin(p), std::sin(y) * std::cos(p) });
        }

        /*********************************************************************
         \brief  The camera's right-facing unit vector.
         \return Normalized right direction (cross(Forward(), WorldUp)).
         *********************************************************************/
        glm::vec3 Right() const { return glm::normalize(glm::cross(Forward(), UpReference())); }

        /*********************************************************************
         \brief  The camera's local up unit vector (not necessarily WorldUp).
         \return Normalized up direction (cross(Right(), Forward())).
         *********************************************************************/
        glm::vec3 Up()    const { return glm::normalize(glm::cross(Right(), Forward())); }

        /*********************************************************************
         \brief  Builds the view matrix from the camera's current placement.
         \return A right-handed lookAt matrix.
         *********************************************************************/
        glm::mat4 GetView()       const { return glm::lookAt(position, position + Forward(), UpReference()); }

        /*********************************************************************
         \brief  Builds the perspective projection matrix from the camera's
                 current lens parameters.
         \return A GLM perspective matrix (Vulkan clip space, Y not
                 flipped - see the file header's conventions note).
         *********************************************************************/
        glm::mat4 GetProjection() const { return glm::perspective(glm::radians(fovY), aspect, nearZ, farZ); }

        /*********************************************************************
         \brief  Convenience combination of GetProjection() * GetView().
         \return The combined view-projection matrix.
         *********************************************************************/
        glm::mat4 GetViewProjection() const { return GetProjection() * GetView(); }

        /*********************************************************************
         \brief  Keeps the lens aspect ratio in sync with a render target's
                 current size.
         \param[in] w Target width.
         \param[in] h Target height.
         \note  A zero or negative height is ignored (leaves aspect
                unchanged) rather than dividing by zero.
         *********************************************************************/
        void SetAspect(float w, float h) { if (h > 0.0f) aspect = w / h; }

        /*********************************************************************
         \brief  Applies a relative yaw/pitch rotation, clamping pitch to
                 avoid gimbal flip.
         \param[in] deltaYawDeg   Yaw delta, in degrees.
         \param[in] deltaPitchDeg Pitch delta, in degrees.
         *********************************************************************/
        void Rotate(float deltaYawDeg, float deltaPitchDeg) {
            yaw   = glm::mod(yaw + deltaYawDeg, 360.0f);
            pitch = glm::clamp(pitch + deltaPitchDeg, -89.0f, 89.0f);
        }

        /*********************************************************************
         \brief  Moves the camera relative to its own basis vectors.
         \param[in] local x = right, y = up (world), z = forward.
         *********************************************************************/
        void Translate(const glm::vec3& local) {
            position += Right() * local.x + WorldUp * local.y + Forward() * local.z;
        }
    };

}   // NAMESPACE CRUCIBLE
