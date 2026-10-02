/*************************************************************************
* \file:         ECSPlayerControllerSystem.cpp
* \author:       Hugo
* \date          2 October 2026
* \version       1.1
* \brief
*                Implementation of ECSPlayerControllerSystem. See the
*                header for what happens each frame.
*
* \history
*    -v1.0       WASD, Space, camera-relative (the Renderer's camera)
*    -v1.1       reads the CameraComponent entity; SimulationOnly
*
**************************************************************************/
#include "ECSPlayerControllerSystem.h"

// Crucible engine
#include "Components/Components.h"
#include "Input/InputSystem.h"
#include "Math/Math.h"
#include "Physics/PhysicsComponents.h"
#include "Systems/Query.h"
#include "World.h"

namespace Crucible {

    void ECSPlayerControllerSystem::Describe(SystemDesc& desc) const {
        desc.SimulationOnly();      // Playing only; Edit and Paused leave the keyboard to the editor
    }

    /*!
        \brief
        Reads the keyboard once, turns it into a world direction using the
        main camera, and hands it to every local player's CharacterController.
    */
    void ECSPlayerControllerSystem::Update(World& world, [[maybe_unused]] CommandBuffer& commands) {

        // THE KEYS ---------------------------------------------------------------- //
        // Each axis is -1, 0 or +1. Holding both A and D cancels out to 0, which
        // is what Unity's Input.GetAxisRaw("Horizontal") does too.
        float right   = 0.0f;
        float forward = 0.0f;
        if (Input::IsDown(KeyCode::D)) right   += 1.0f;
        if (Input::IsDown(KeyCode::A)) right   -= 1.0f;
        if (Input::IsDown(KeyCode::W)) forward += 1.0f;
        if (Input::IsDown(KeyCode::S)) forward -= 1.0f;

        // IsPressed, not IsDown: one jump per press. Holding Space would
        // otherwise jump again the moment the mech lands - a pogo stick.
        const bool jump = Input::IsPressed(KeyCode::Space);

        // THE CAMERA -------------------------------------------------------------- //
        // Unity's Camera.main: the enabled camera with the highest priority, the
        // first one found on a tie. The SAME rule RenderExtractSystem uses to pick
        // what the Game view shows - if one changes, change the other, or W walks
        // relative to a camera nobody is looking through.
        //
        // WorldTransform, not Transform: the camera is usually a child of the
        // mech, and its Transform is then only its offset from the mech. It is
        // last frame's (HierarchyTransformSystem fills it in PreRender, after
        // this), which is one frame of camera turn - nothing anyone can feel.
        bool       found        = false;
        int        bestPriority = 0;
        Math::vec3 cameraRight  = Math::vec3(1.0f, 0.0f, 0.0f);   // no camera: W is world -Z, D is world +X

        world.CreateQuery<const WorldTransform, const CameraComponent>()
            .ForEach([&](Entity, const WorldTransform& worldTransform, const CameraComponent& camera) {

                if (!camera.enabled || !worldTransform.valid) return;
                if (found && camera.priority <= bestPriority) return;

                // Column 0 of a world matrix is the entity's local +X axis in
                // world space: the camera's right. Flattened, because a tilted
                // or rolled camera would otherwise steer the mech into the floor.
                const Math::vec3 flat(worldTransform.matrix[0].x, 0.0f, worldTransform.matrix[0].z);
                if (glm::dot(flat, flat) < 1.0e-6f) return;     // rolled onto its side: no usable heading

                found        = true;
                bestPriority = camera.priority;
                cameraRight  = glm::normalize(flat);
            });

        // Forward comes from right, not from the camera's own forward. A camera
        // looking straight down at the mech has a forward of (0, -1, 0), which
        // flattens to nothing; its right is still perfectly level. Up x right is
        // the direction 90 degrees to the left of right, along the ground -
        // with a right-handed, Y-up world that is exactly "away from the camera".
        const Math::vec3 cameraForward = glm::cross(Math::vec3(0.0f, 1.0f, 0.0f), cameraRight);

        // W+D gives a vector about 1.41 long, so diagonal walking would be 41%
        // faster. Shrink it back to 1. Only when over 1, so a gamepad stick
        // pushed halfway can still mean "walk slowly" later.
        Math::vec3 direction = cameraRight * right + cameraForward * forward;
        if (glm::dot(direction, direction) > 1.0f) direction = glm::normalize(direction);

        // THE MECHS --------------------------------------------------------------- //
        // Only entities with BOTH components. PlayerController is const because
        // this system only reads its settings; CharacterController is not,
        // because SimpleMove and Jump write to it.
        world.CreateQuery<const PlayerController, CharacterController>()
            .ForEach([&](Entity, const PlayerController& player, CharacterController& controller) {

                // Every frame, even with no keys held: SimpleMove(zero) is how
                // the mech stops (see CharacterController in PhysicsComponents.h).
                controller.SimpleMove(direction * player.m_moveSpeed);

                // Physics only honours this if the mech is on the ground at its
                // next step, so there is no need to check m_isGrounded here.
                if (jump && player.m_jumpHeight > 0.0f) controller.Jump(player.m_jumpHeight);
            });

    }   // ECSPlayerControllerSystem::Update()

}   // NAMESPACE CRUCIBLE
