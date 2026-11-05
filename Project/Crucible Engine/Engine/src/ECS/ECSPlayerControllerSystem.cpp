/*************************************************************************
* \file:         ECSPlayerControllerSystem.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          2 October 2026
* \version       1.8
* \brief
*                Implementation of ECSPlayerControllerSystem. See the
*                header for what happens each frame.
*
* \history
*    -v1.0       WASD, Space, camera-relative (the Renderer's camera)
*    -v1.1       reads the CameraComponent entity; SimulationOnly
*    -v1.2       third-person camera: CameraOrbit and the cursor lock
*    -v1.3       the mech turns to face the camera (m_faceCamera)
*    -v1.4       hierarchy-aware camera orbit: the mech no longer has to be
*                a root - its world rotation (and the frame its own
*                Transform is in) come from its parents
*    -v1.5       gamepad: left stick walks (analog), right stick turns the
*                camera (CameraOrbit::m_stickSpeed), A jumps
*    -v1.6       the player's look sensitivity and invert Y (UserSettings)
*    -v1.7       the keys come from the player's keybinds (InputSettings::
*                forwardKey .. jumpButton) instead of hardcoded WASD /
*                Space / A (Tan Chun Kwang Xavier)
*    -v1.8       split (Networking Abhishek Akilandeswari
*                Patchamuthu): the cursor, camera-pivot, keys and camera
*                blocks moved unchanged to LocalPlayerInputSystem, which
*                writes PlayerInput; this system applies it (SimpleMove,
*                Jump, facing the camera's yaw)
*
**************************************************************************/
#include "ECSPlayerControllerSystem.h"

// Crucible engine
#include "Components/Components.h"       // Transform, PlayerInput (Network/NetComponents.h)
#include "Math/Math.h"
#include "Physics/PhysicsComponents.h"   // CharacterController
#include "Player/PlayerComponents.h"     // PlayerController, CameraOrbit
#include "Systems/Query.h"
#include "World.h"

namespace Crucible {

    namespace {

        // The rotation of the frame an entity's Transform is relative to: its parent's world
        // rotation (sockets included, via World::TryGetParentFrame). Exactly the identity for
        // a root, so a root mech's maths below is bit-for-bit what it was before hierarchies.
        Math::quat FrameRotation(const World& world, Entity entity) {
            const Math::quat identity(1.0f, 0.0f, 0.0f, 0.0f);
            Math::mat4 frame(1.0f);
            if (!world.TryGetParentFrame(entity, frame) || frame == Math::mat4(1.0f)) return identity;
            Math::vec3 translation, scale;
            Math::quat rotation;
            return Detail::DecomposeTRS(frame, translation, rotation, scale) ? rotation : identity;
        }
    }

    void ECSPlayerControllerSystem::Describe(SystemDesc& desc) const {
        desc.SimulationOnly();      // Playing only; Edit and Paused leave the keyboard to the editor
    }

    // No longer driven by the keyboard: stand still, and drop a jump asked for this frame.
    // Switched back on, the query below drives it again from the next Update.
    void ECSPlayerControllerSystem::OnComponentDeactivated(World& world, Entity entity, [[maybe_unused]] ComponentId component) {
        if (CharacterController* controller = world.GetComponent<CharacterController>(entity)) {
            controller->SimpleMove(Math::vec3(0.0f, 0.0f, 0.0f));
            controller->m_jumpHeight = 0.0f;
        }
    }

    /*!
        \brief
        Turns every mech to face its camera, and hands every player's
        PlayerInput to its CharacterController.
        \details
        The keyboard, mouse and gamepad, the cursor lock and the camera pivot
        are LocalPlayerInputSystem's now (PreUpdate, the same code): it writes
        PlayerInput, and this applies it - whoever wrote it.
    */
    void ECSPlayerControllerSystem::Update(World& world, [[maybe_unused]] CommandBuffer& commands) {

        // THE MECH FACING THE CAMERA ------------------------------------------------ //
        // Toggle on: the mech turns to the camera's yaw. Yaw only - a mech that
        // pitched with the camera would lie down to look at the floor. Written only
        // when it changed: a mutable GetComponent stamps the Transform as changed.
        // The yaw is the camera pivot's (PlayerInput::yawDegrees, from the sampler).
        // Every player with a pivot, like before: a switched-off PlayerController, or
        // one without a CharacterController, still faces its camera.
        world.BuildQuery<const PlayerController, const PlayerInput>().IncludeDisabled().Build()
            .ForEach([&](Entity mech, const PlayerController& player, const PlayerInput& input) {

                if (!player.m_faceCamera || !input.hasYaw) return;
                const Transform* mechTransform = world.Read<Transform>(mech);
                if (mechTransform == nullptr) return;
                const Math::quat yawRotation = glm::angleAxis(glm::radians(input.yawDegrees), Math::vec3(0.0f, 1.0f, 0.0f));
                const Math::quat mechFrame = FrameRotation(world, mech);   // what the mech's Transform is relative to
                // yawRotation is a world rotation; the mech's Transform is in
                // mechFrame, so local = inverse(frame) x world (just yawRotation
                // for a root mech).
                if (yawRotation != mechFrame * mechTransform->rotation) {
                    const Math::quat localYaw = glm::inverse(mechFrame) * yawRotation;
                    if (localYaw != mechTransform->rotation) {
                        if (Transform* writable = world.GetComponent<Transform>(mech)) writable->rotation = localYaw;
                    }
                }
            });

        // THE MECHS --------------------------------------------------------------- //
        // Only entities with all three. PlayerController is const because this system
        // only reads its settings; CharacterController is not, because SimpleMove and
        // Jump write to it; PlayerInput is not, because a jump is used up here.
        world.CreateQuery<const PlayerController, CharacterController, PlayerInput>()
            .ForEach([&](Entity, const PlayerController& player, CharacterController& controller, PlayerInput& input) {

                // Every frame, even with no keys held: SimpleMove(zero) is how
                // the mech stops (see CharacterController in PhysicsComponents.h).
                controller.SimpleMove(input.moveDirection * player.m_moveSpeed);

                // Physics only honours this if the mech is on the ground at its
                // next step, so there is no need to check m_isGrounded here.
                if (input.jump && player.m_jumpHeight > 0.0f) controller.Jump(player.m_jumpHeight);
                input.jump = false;   // used up: the sampler holds a press only until here
            });

    }   // ECSPlayerControllerSystem::Update()

}   // NAMESPACE CRUCIBLE
