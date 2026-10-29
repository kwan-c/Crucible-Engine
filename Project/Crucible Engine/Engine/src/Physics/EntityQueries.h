/*************************************************************************
* \file:         EntityQueries.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          29 October 2026
* \version       1.0
* \brief
*                What gameplay and AI pass to, and get back from, the
*                queries on ECSPhysicsSystem (Raycast, SphereCast,
*                OverlapSphere...): entities instead of physics bodies.
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

}   // NAMESPACE CRUCIBLE
