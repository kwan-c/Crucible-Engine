#pragma once

// STL
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
