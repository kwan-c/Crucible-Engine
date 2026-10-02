/*************************************************************************
* \file:         ECSPlayerControllerSystem.cpp
* \author:       Hugo
* \date          2 October 2026
* \version       1.0
* \brief
*                Implementation of ECSPlayerControllerSystem. See the
*                header for what happens each frame.
*
* \history
*    -v1.0       WASD, Space, camera-relative
*
**************************************************************************/
#include "ECSPlayerControllerSystem.h"

// Crucible engine
#include "Graphics/Camera.h"
#include "Graphics/RenderSystem.h"
#include "Input/InputSystem.h"
#include "Math/Math.h"
#include "Physics/PhysicsComponents.h"
#include "Systems/Query.h"
#include "World.h"

namespace Crucible {

    ECSPlayerControllerSystem::ECSPlayerControllerSystem(const Renderer& renderer) : m_renderer(renderer) {}

    /*!
        \brief
        Reads the keyboard once, turns it into a world direction using the
        camera, and hands it to every local player's CharacterController.
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
        bool jump = Input::IsPressed(KeyCode::Space);

        // While the right mouse button is held, the editor's Scene view is
        // flying its own camera on these same keys, so the mech ignores them.
        // Zeroed rather than returning early: SimpleMove KEEPS its last value,
        // so an early return would leave the mech walking on by itself.
        // TEMPORARY: delete this once the editor has a Play button. In the
        // game itself it also freezes the mech while RMB is held, which will
        // clash the day RMB becomes "aim".
        if (Input::IsDown(KeyCode::MouseRight)) {
            right   = 0.0f;
            forward = 0.0f;
            jump    = false;
        }

        // THE CAMERA ---------------------------------------------------------------- //
        // The camera's forward points down at the ground, and walking along it
        // would push the mech into the floor and make it slower the steeper the
        // camera looks. So the up part is removed and what is left made length 1
        // again: "away from the camera, along the ground". Camera pitch is
        // clamped to +-89 degrees, so the flattened vector is never zero.
        // When the camera becomes an entity, these two lines are the only ones
        // that change - read its Transform instead of the Renderer.
        const Camera& camera = m_renderer.GetCamera();
        const Math::vec3 cameraForward = glm::normalize(Math::vec3(camera.Forward().x, 0.0f, camera.Forward().z));
        const Math::vec3 cameraRight   = glm::normalize(Math::vec3(camera.Right().x,   0.0f, camera.Right().z));

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
