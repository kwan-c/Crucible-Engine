/*************************************************************************
* \file:         EntityQueries.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          29 October 2026
* \version       1.1
* \brief
*                What gameplay and AI pass to, and get back from, the
*                queries on ECSPhysicsSystem (Raycast, SphereCast,
*                OverlapSphere...): entities instead of physics bodies.
*                Also ForceMode and Explosion, for its explosion force.
*
* \details
*                The same queries exist on Physics, in bodies and user
*                values. These are the entity versions: the filter's
*                `ignore` is an Entity (a mech is resolved to its hidden
*                character body for you), and every hit names its Entity.
*
*                    EntityFilter sight;
*                    sight.layers = LayerMask(PhysicsLayer::Player, PhysicsLayer::NonMoving);
*                    sight.ignore = self;
*                    EntityHit hit;
*                    const bool seesPlayer = physics.Raycast(eyes, toPlayer, hit, range, sight)
*                                            && hit.entity == player;
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>

// Crucible engine
#include "ECS/Core/ECSCore.h"
#include "Math/Math.h"
#include "Physics/PhysicsLayers.h"

namespace Crucible {

    // Which layers a query may hit, whether triggers count, and one entity to
    // see through - usually the one asking.
    struct EntityFilter {

        LayerMask layers      = LayerMask::All();
        bool      hitTriggers = false;     // false: pickup zones and other triggers are invisible to it
        Entity    ignore      {};

    };  // ENTITYFILTER STRUCT

    // Unity's RaycastHit, for the entity versions of the casts.
    struct EntityHit {

        Entity     entity;
        Math::vec3 point    = Math::vec3(0.0f, 0.0f, 0.0f);
        Math::vec3 normal   = Math::vec3(0.0f, 1.0f, 0.0f);   // which way the surface faces where it was hit
        float      distance = 0.0f;                          // along the cast, in metres

    };  // ENTITYHIT STRUCT

    // Unity's ForceMode, the two that matter here.
    enum class ForceMode : std::uint8_t {

        Force,      // newtons: a push that lasts this frame's physics steps, like RigidBody::AddForce
        Impulse     // kg*m/s: one instant kick. What an explosion almost always wants

    };  // FORCEMODE ENUM

    // One explosion, for ECSPhysicsSystem::Explode: everything inside the sphere
    // is pushed away from its centre, and listed so gameplay can damage it.
    struct Explosion {

        Math::vec3   position        = Math::vec3(0.0f, 0.0f, 0.0f);
        float        radius          = 5.0f;      // metres: full strength at the centre, nothing at the edge
        float        force           = 500.0f;    // at the centre, in the units of `mode`
        float        upwardsModifier = 0.0f;      // metres: as if the blast were this much lower, so things are thrown UP
        ForceMode    mode            = ForceMode::Impulse;
        EntityFilter filter          {};          // which layers it reaches; triggers never count
        bool         blockedByWalls  = false;     // true: arena geometry (NonMoving) between blast and body shields it

    };  // EXPLOSION STRUCT

}   // NAMESPACE CRUCIBLE
