/*************************************************************************
* \file:         PhysicsComponents.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          27 September 2026
* \version       1.5
* \brief
*                The physics components the designer and gameplay touch:
*                Collider, CharacterController, PlayerController,
*                CameraOrbit, ConstantForce and PhysicsJoint.
*
* \details
*                Sizes are full sizes, the way Unity states them;
*                ECSPhysicsSystem halves them for Jolt in one place.
*                ScaledCollider applies the Transform's scale, so what you
*                see is what you hit. Also holds the Entity to user-data
*                packing.
*
* \history
*    -v1.0       Collider and CharacterController
*    -v1.1       maths types moved to glm
*    -v1.2       PlayerController; Collider scales with the Transform
*    -v1.3       CameraOrbit and face-the-camera
*    -v1.4       ConstantForce
*    -v1.5       PhysicsJoint
*
**************************************************************************/
#pragma once

// STL
#include <algorithm>    // ScaledCollider()
#include <cmath>        // ScaledCollider()
#include <cstdint>

// Crucible engine
#include "ECS/Components/ComponentKind.h"
#include "ECS/Core/ECSCore.h"
#include "Math/Math.h"
#include "Physics/PhysicsSystem.h"

// ---------------------------------------------------------------------------
// The physics components: what gameplay and the designer actually touch.
//
//     Entity crate = world.CreateEntity();
//     world.AddComponent(crate, Transform{ .positions = Math::vec3(0, 5, 0) });
//     world.AddComponent(crate, Collider{});          // a 1m cube
//     world.AddComponent(crate, RigidBody{});         // ...that falls
//
//   a Collider on its own       static: the arena. It only moves if you move it
//   a Collider + a RigidBody    dynamic: physics moves it and writes its Transform
//
// Sizes here are FULL sizes, the way Unity states them - a 1 metre cube is
// size (1, 1, 1). Jolt wants half sizes, and ECSPhysicsSystem does that halving
// in one place so nobody else has to think about it.
// ---------------------------------------------------------------------------

namespace Crucible {

    /*
        Unity's BoxCollider / SphereCollider / CapsuleCollider, folded into one
        component with a shape field. One component rather than three because an
        entity may only have one collider anyway, and a shape dropdown is easier
        to put in an inspector than "remove this component, add that one".

        Which fields matter depends on m_type: a sphere reads m_radius and
        ignores the rest.
    */
    struct Collider {

        ShapeType    m_type       = ShapeType::Box;

        Math::vec3   m_size       = Math::vec3(1.0f, 1.0f, 1.0f);   // Box:     the full width, height and depth
        float        m_radius     = 0.5f;                           // Sphere, Capsule
        float        m_height     = 2.0f;                           // Capsule: the total height, rounded ends included
        Math::vec3   m_center     = Math::vec3(0.0f, 0.0f, 0.0f);   // offset from the entity's position

        float        m_friction   = 0.2f;                           // 0 is ice, 1 is glue
        float        m_bounciness = 0.0f;                           // 0 is a sandbag, 1 is a superball

        // The arena is the common case, so it is the default. Anything that
        // moves wants Moving, Player or Enemy - two NonMoving bodies never
        // collide with each other, by design (PhysicsLayers.cpp).
        PhysicsLayer m_layer      = PhysicsLayer::NonMoving;

    };  // COLLIDER STRUCT

    CRUCIBLE_COMPONENT(Collider, Collider)

    /*!
        \brief
        The collider as it really is in the world: its sizes multiplied by the
        entity's Transform scale, the way Unity always does it. A cube mesh
        scaled to (15, 0.1, 15) with the default 1m Collider gets a 15 x 0.1 x 15
        collider - what you see is what you hit.

        The ONE place this rule lives. Physics builds bodies from it and debug
        draw draws wireframes from it; two copies would drift apart and the
        wireframe would lie about what the mech actually walks into.
        \param collider
        The component as the designer typed it, in the entity's local units.
        \param scale
        Transform::scale. Negative axes count as positive: a mirrored crate is
        still the same size.
        \return
        A copy with every size scaled. The component itself is never changed,
        so editing the scale back to 1 gives back the typed sizes.
    */
    inline Collider ScaledCollider(const Collider& collider, const Math::vec3& scale) {

        const Math::vec3 size(std::abs(scale.x), std::abs(scale.y), std::abs(scale.z));

        Collider scaled = collider;
        scaled.m_size   = collider.m_size * size;       // per axis
        scaled.m_center = collider.m_center * scale;    // signed: a mirrored offset flips side with the mesh

        // Round shapes cannot stretch into an egg, so they grow to fit the
        // biggest axis instead - Unity's rule. A sphere uses all three axes; a
        // capsule stands along Y, so Y sets its height and only X and Z its radius.
        switch (collider.m_type) {
            case ShapeType::Box:
                break;
            case ShapeType::Sphere:
                scaled.m_radius = collider.m_radius * std::max({ size.x, size.y, size.z });
                break;
            case ShapeType::Capsule:
                scaled.m_radius = collider.m_radius * std::max(size.x, size.z);
                scaled.m_height = collider.m_height * size.y;
                break;
        }

        return scaled;

    }   // ScaledCollider()

    /*
        Unity's CharacterController: a capsule that walks. It slides along walls,
        climbs steps and shoves crates, and nothing in the simulation pushes it
        back - it goes where you tell it, and the world can only block it.

            CharacterController& mech = *world.GetComponent<CharacterController>(player);
            mech.SimpleMove(Math::vec3(input.x * 6.0f, 0.0f, input.z * 6.0f));
            if (jumpPressed && mech.m_isGrounded) mech.Jump(1.5f);

        The capsule's centre is the Transform's position plus m_center, so set
        m_center to (0, height / 2, 0) to put the Transform at the mech's feet.
        Writing Transform.positions teleports it - that is a respawn.

        An entity has a Collider or a CharacterController, never both.
    */
    struct CharacterController {

        // SETTINGS ---------------------------------------------------------- //

        float        m_height       = 2.0f;         // the whole capsule, rounded ends included
        float        m_radius       = 0.5f;
        Math::vec3   m_center       = Math::vec3(0.0f, 0.0f, 0.0f);
        float        m_slopeLimit   = 45.0f;        // degrees: steeper and it slides back down
        float        m_stepOffset   = 0.3f;         // metres it steps up without jumping
        float        m_pushStrength = 100.0f;       // newtons it shoves crates with. Raise it a lot for a mech
        PhysicsLayer m_layer        = PhysicsLayer::Player;

        // MOVING IT ----------------------------------------------------------- //

        // Walk at this velocity, in m/s. The up part is ignored: gravity and
        // jumping own that. It KEEPS walking at it until you say otherwise, so
        // SimpleMove(zero) is how you stop.
        void SimpleMove(const Math::vec3& velocity) { m_moveVelocity = velocity; }

        // Jump this high, in metres, if it is on the ground at the next step.
        // Unity makes you work out the launch speed yourself; this does it for you.
        void Jump(const float& height) { m_jumpHeight = height > 0.0f ? height : 0.0f; }

        // RESULTS: physics writes these every step ----------------------------- //

        bool         m_isGrounded    = false;
        Math::vec3   m_velocity      = Math::vec3(0.0f, 0.0f, 0.0f);   // what it really moved at, after walls

        // STATE: kept here so it survives from step to step --------------------- //

        Math::vec3   m_moveVelocity  = Math::vec3(0.0f, 0.0f, 0.0f);   // set by SimpleMove
        float        m_verticalSpeed = 0.0f;                           // + is up. Gravity pulls on it every step
        float        m_jumpHeight    = 0.0f;                           // set by Jump, used up by the next step

    };  // CHARACTERCONTROLLER STRUCT

    CRUCIBLE_COMPONENT(CharacterController, CharacterController)

    /*
        Marks the mech THIS machine controls: ECSPlayerControllerSystem reads
        the keyboard and drives the CharacterController on the same entity.
        Unity's equivalent is a PlayerMovement script calling SimpleMove.

            world.AddComponent(mech, CharacterController{});
            world.AddComponent(mech, PlayerController{});     // ...and WASD walks it

        A separate component, not a flag on CharacterController, because in
        multiplayer every mech has a CharacterController but only the local
        one reads the keyboard. The other mechs are moved by network packets,
        so they simply never get this component.

        Does nothing without a CharacterController next to it.
    */
    struct PlayerController {

        float m_moveSpeed  = 6.0f;      // m/s on the ground. Diagonals are no faster
        float m_jumpHeight = 1.5f;      // metres, measured from the feet. 0 disables jumping

        // Turn the whole mech to face where the camera looks (yaw only - it stays
        // upright). Needs the CameraOrbit pivot to be a child of this entity. Off:
        // the mech keeps its rotation and the camera orbits freely round it.
        // Safe to flip mid-Play: the pivot cancels the mech's turn, so the view
        // never jumps either way.
        bool  m_faceCamera = true;

    };  // PLAYERCONTROLLER STRUCT

    CRUCIBLE_COMPONENT(PlayerController, PlayerController)

    /*
        The third-person camera. The mouse turns the entity this sits on, and
        that entity is a PIVOT - an empty point the camera hangs off - not the
        camera itself:

            Mech            CharacterController, PlayerController
            '- Pivot        CameraOrbit, local position about (0, 1.5, 0): shoulder height
               '- Camera    CameraComponent, local position about (0, 0, 5): 5m behind

        Turning the pivot swings "5m behind me" around, so the camera circles
        the mech and the hierarchy does all the maths; nothing ever touches the
        camera entity. Unity's Starter Assets do the same with PlayerCameraRoot.
        Putting this on the camera instead would only spin it in place - it
        would look away from the mech rather than go round it.

        ECSPlayerControllerSystem writes the pivot's Transform.rotation from
        m_yaw and m_pitch every frame while Playing, so rotate the pivot with
        these two fields, not with the gizmo. They are saved: what they hold
        in the editor is the view the game starts with.
    */
    struct CameraOrbit {

        // SETTINGS ---------------------------------------------------------- //

        float m_sensitivity = 0.1f;     // degrees turned per pixel of mouse movement
        float m_minPitch    = -30.0f;   // degrees. Below 0 the camera dips under the pivot and looks up
        float m_maxPitch    =  70.0f;   // degrees. Short of 90, or at the top the view flips over

        // STATE: the current view, in degrees -------------------------------- //

        float m_yaw         = 0.0f;     // round the mech. + turns the view left, as the mouse moving left does
        float m_pitch       = 0.0f;     // + raises the camera to look down on the mech, as the mouse moving down does

    };  // CAMERAORBIT STRUCT

    CRUCIBLE_COMPONENT(CameraOrbit, CameraOrbit)

    /*
        Unity's ConstantForce: a steady push a designer sets in the Inspector -
        wind on a crate, a thruster, an updraft - with no script at all.

            world.AddComponent(crate, RigidBody{});
            world.AddComponent(crate, ConstantForce{ .m_force = { 0, 20, 0 } });   // ...and it drifts upward

        The world-space pair pushes along the world's axes, whichever way the
        body faces. The relative pair pushes along the body's OWN axes and turns
        with it, so a rocket with relative force (0, 0, -100) always thrusts
        out of its own front (-Z is forward in a right-handed, Y-up engine).

        Handed to every physics step, so it pushes the same at any frame rate.
        Like Unity's, it needs a RigidBody on the same entity (only a dynamic
        body can be pushed), and it keeps that body awake for as long as it pushes.
    */
    struct ConstantForce {

        Math::vec3 m_force          = Math::vec3(0.0f, 0.0f, 0.0f);   // newtons, world axes
        Math::vec3 m_relativeForce  = Math::vec3(0.0f, 0.0f, 0.0f);   // newtons, the body's own axes
        Math::vec3 m_torque         = Math::vec3(0.0f, 0.0f, 0.0f);   // newton-metres, world axes
        Math::vec3 m_relativeTorque = Math::vec3(0.0f, 0.0f, 0.0f);   // newton-metres, the body's own axes

    };  // CONSTANTFORCE STRUCT

    CRUCIBLE_COMPONENT(ConstantForce, ConstantForce)

    /*
        Unity's joints - FixedJoint, HingeJoint, CharacterJoint, SpringJoint -
        folded into one component with a type field, the way Collider folds
        the three collider shapes. It goes on one of the two entities and names
        the other; both need a Collider, and at least one a RigidBody.

            Forearm   Collider, RigidBody, PhysicsJoint{ Hinge, connected = UpperArm }
            Door      Collider, RigidBody, PhysicsJoint{ Hinge, connected = none }   // hinged to the world

        CONNECTED BODY. Two ways to name it, because two kinds of code make joints:
          - m_connectedBody: the other entity's scene id. What the Inspector's
            picker writes and the scene file saves. 0 = the world.
          - m_connectedEntity: the other entity itself, for code. An entity
            spawned during Play has no scene id, so a ragdoll built in code sets
            this instead. Not saved. Wins when both are set.

        Anchor and axes are in THIS entity's local space, like Unity's, and
        follow its Transform. The joint is built from the pose both entities
        are in when Play starts (or when it is added), and every limit is
        measured from that pose.

        Changing a setting during Play rebuilds the joint from the pose things
        are in at that moment, the same as Collider rebuilding its body.
    */
    struct PhysicsJoint {

        JointType     m_type            = JointType::Hinge;
        std::uint64_t m_connectedBody   = 0;        // scene id of the other entity (Scene::IdOf). 0 = the world
        Entity        m_connectedEntity {};         // runtime: set from code. Not saved, not in the Inspector

        Math::vec3    m_anchor          = Math::vec3(0.0f, 0.0f, 0.0f);   // the joint point, this entity's local space
        Math::vec3    m_axis            = Math::vec3(1.0f, 0.0f, 0.0f);   // Hinge: turns around it. Character: the twist axis, along the limb
        Math::vec3    m_swingAxis       = Math::vec3(0.0f, 1.0f, 0.0f);   // Character: swing1 turns around it
        bool          m_enableCollision = false;    // Unity's enableCollision: let the two bodies collide with each other

        // Hinge, in degrees. Min 0 or below, max 0 or above (the starting pose must be allowed).
        // Positive = THIS entity turning the right-hand-rule way around m_axis (point your right
        // thumb along the axis: your fingers curl the positive way), relative to the connected
        // body. A knee whose axis points to the body's right bends backwards with positive angles.
        bool          m_useLimits       = false;
        float         m_minAngle        = -90.0f;
        float         m_maxAngle        =  90.0f;

        // Character, in degrees: twist around m_axis, swing1 around m_swingAxis,
        // swing2 around the axis at right angles to both.
        float         m_lowTwistLimit   = -20.0f;
        float         m_highTwistLimit  =  20.0f;
        float         m_swing1Limit     =  40.0f;
        float         m_swing2Limit     =  40.0f;

        // Spring. m_connectedAnchor is the far end of the spring, in the OTHER
        // entity's local space (world space when connected to the world).
        // Distances 0 = as far apart as they start.
        Math::vec3    m_connectedAnchor = Math::vec3(0.0f, 0.0f, 0.0f);
        float         m_spring          = 10.0f;    // stiffness, N/m. 0 = a rigid rod
        float         m_damper          = 0.2f;     // N*s/m
        float         m_minDistance     = 0.0f;
        float         m_maxDistance     = 0.0f;

        // Breaking. 0 = never (Unity uses Infinity, which a JSON scene file cannot store).
        float         m_breakForce      = 0.0f;     // newtons
        float         m_breakTorque     = 0.0f;     // newton-metres

        // Unity's solver knobs. 0 = engine default. Raise solverIterations (default 2)
        // when the joint rests slightly wrong under weight - limbs apart, a knee lying
        // past its limit; 8 suits a ragdoll. Raise solverVelocityIterations (default 10)
        // when a light body on a long lever swings visibly past a limit.
        std::uint32_t m_solverIterations         = 0;
        std::uint32_t m_solverVelocityIterations = 0;

        // Runtime: set when the joint broke during Play, so it is not rebuilt.
        // Read it the way Unity code waits for OnJointBreak. Not saved, so Stop
        // brings every joint back.
        bool          m_isBroken        = false;

    };  // PHYSICSJOINT STRUCT

    CRUCIBLE_COMPONENT(PhysicsJoint, PhysicsJoint)

    // ------------------------------------------------------------------- //
    // ENTITY <-> USER DATA
    // ------------------------------------------------------------------- //
    // Every body carries one 64-bit number that Jolt never looks at. We put the
    // Entity in it, so a raycast hit or a moved body can say which entity it is
    // without any lookup table. An Entity is two 32-bit halves, so it fits
    // exactly: index up top, generation underneath.

    inline std::uint64_t EntityToUserData(const Entity& entity) {
        return (static_cast<std::uint64_t>(entity.index) << 32) | entity.generation;
    }

    inline Entity UserDataToEntity(const std::uint64_t& userData) {
        return Entity(static_cast<std::uint32_t>(userData >> 32), static_cast<std::uint32_t>(userData & 0xFFFFFFFFu));
    }

}   // NAMESPACE CRUCIBLE
