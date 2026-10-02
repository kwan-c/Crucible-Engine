/*************************************************************************
* \file:         ECSPlayerControllerSystem.cpp
* \author:       Hugo
* \date          2 October 2026
* \version       1.3
* \brief
*                Implementation of ECSPlayerControllerSystem. See the
*                header for what happens each frame.
*
* \history
*    -v1.0       WASD, Space, camera-relative (the Renderer's camera)
*    -v1.1       reads the CameraComponent entity; SimulationOnly
*    -v1.2       third-person camera: CameraOrbit and the cursor lock
*    -v1.3       the mech turns to face the camera (m_faceCamera)
*
**************************************************************************/
#include "ECSPlayerControllerSystem.h"

// STL
#include <algorithm>
#include <cmath>

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
        Locks the cursor when Play or Resume starts, turns the camera pivot
        with the mouse, then reads the keyboard once, turns it into a world
        direction using the main camera, and hands it to every local player's
        CharacterController.
    */
    void ECSPlayerControllerSystem::Update(World& world, [[maybe_unused]] CommandBuffer& commands) {

        // THE CURSOR -------------------------------------------------------------- //
        // A gap in the tick means this system was switched off for a while (Edit
        // or Paused) and Play or Resume has just begun: hide and capture the
        // cursor, the way a Unity game sets Cursor.lockState at start. Only on
        // that first frame - after Esc frees the cursor, clicking the Game view is
        // what takes it back (the editor's job), not this.
        const std::uint64_t tick = world.CurrentTick();
        if (tick != m_lastTick + 1) Input::SetMouseLocked(true);
        m_lastTick = tick;

        // THE CAMERA PIVOT --------------------------------------------------------- //
        // The mouse only turns the camera while the cursor is captured. A free
        // cursor is on its way to the editor's Pause button or the Inspector, and
        // the view spinning as it travels there would be maddening.
        // GetMouseDelta is a distance in pixels, so it is NOT multiplied by dt.
        const Math::vec2 mouse = Input::IsMouseLocked() ? Input::GetMouseDelta() : Math::vec2(0.0f, 0.0f);

        world.CreateQuery<CameraOrbit, const Transform>()
            .ForEach([&](Entity pivot, CameraOrbit& orbit, const Transform& transform) {

                // Mouse left (x < 0) turns the view left, which is + yaw around Y.
                // Mouse down (y > 0, pixels count downwards) tilts it down, which
                // is + pitch. Neither axis is inverted - the common default.
                orbit.m_yaw   -= mouse.x * orbit.m_sensitivity;
                orbit.m_pitch += mouse.y * orbit.m_sensitivity;

                // Yaw goes round and round; wrapping it stops a long session
                // growing it into the millions, where a float has no precision left.
                orbit.m_yaw = std::fmod(orbit.m_yaw, 360.0f);

                // Deliberately NOT std::clamp: clamp is undefined if a designer types
                // the limits the wrong way round, and in Debug it asserts mid-Play.
                // max(min(...)) is always defined; swapped limits just pin the
                // camera to m_minPitch until the typo is fixed.
                orbit.m_pitch = std::max(orbit.m_minPitch, std::min(orbit.m_pitch, orbit.m_maxPitch));

                // Yaw around the world's up, then pitch around the pivot's own
                // sideways axis - the order that keeps the horizon level. The
                // other order tilts the horizon as soon as both are non-zero.
                // Pitch is negated: turning around +X by a positive angle tips the
                // view UP, and + pitch means looking down on the mech.
                // This is the rotation the camera should end up with in the WORLD.
                const Math::quat yawRotation   = glm::angleAxis(glm::radians(orbit.m_yaw),    Math::vec3(0.0f, 1.0f, 0.0f));
                const Math::quat pitchRotation = glm::angleAxis(glm::radians(-orbit.m_pitch), Math::vec3(1.0f, 0.0f, 0.0f));
                const Math::quat worldRotation = yawRotation * pitchRotation;

                // THE MECH. The pivot is a child of the mech, so whatever the mech's
                // rotation is gets added on top of the pivot's own. Turning the mech
                // to the camera AND giving the pivot the full yaw would turn the
                // camera twice - and every frame the mech chases it, it runs away
                // further. So the mech's rotation is cancelled out below, whatever it
                // is: toggle on or off, flipped mid-Play or not, the view never jumps.
                // Assumes the mech is at the top of the hierarchy, so its Transform
                // rotation IS its world rotation - the Mech -> Pivot -> Camera setup.
                Math::quat parentRotation = Math::quat(1.0f, 0.0f, 0.0f, 0.0f);   // no parent: nothing to cancel
                const Entity mech = world.GetParent(pivot);
                if (const Transform* mechTransform = world.Read<Transform>(mech)) {
                    parentRotation = mechTransform->rotation;

                    // Toggle on: the mech turns to the camera's yaw. Yaw only - a
                    // mech that pitched with the camera would lie down to look at the
                    // floor. Written only when it changed, same reason as the pivot.
                    const PlayerController* player = world.Read<PlayerController>(mech);
                    if (player != nullptr && player->m_faceCamera && yawRotation != parentRotation) {
                        if (Transform* writable = world.GetComponent<Transform>(mech)) writable->rotation = yawRotation;
                        parentRotation = yawRotation;
                    }
                }

                // Undo the parent's turn: parent x local = world, so local =
                // inverse(parent) x world. Facing the camera, that leaves just the pitch.
                const Math::quat rotation = glm::inverse(parentRotation) * worldRotation;

                // Written every frame, not only when the mouse moves, so the saved
                // angles take effect on the first frame of Play and an Inspector
                // edit during Play shows up at once. But only when it differs:
                // a mutable GetComponent stamps the Transform as changed, and an
                // untouched camera would otherwise re-stamp itself 60 times a second.
                if (rotation != transform.rotation) {
                    if (Transform* writable = world.GetComponent<Transform>(pivot)) writable->rotation = rotation;
                }
            });

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
