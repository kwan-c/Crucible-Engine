/*************************************************************************
* \file:         PhysicsEvents.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          28 October 2026
* \version       1.0
* \brief
*                Unity's OnCollisionEnter/Exit and OnTriggerEnter/Exit, as
*                events on the ENGINE bus, plus EntityContact for "what is
*                touching this right now" (ECSPhysicsSystem::GetContacts).
*
* \details
*                ECSPhysicsSystem sends these after each physics step. They
*                are native facts, so they go on the engine bus (Abhishek's
*                routing rule, EventBuses.h); deciding what a hit MEANS -
*                damage, a sound, a pickup - is gameplay's job:
*
*                    m_onEnter = core.Events().engine.Subscribe<PhysicsEvents::TriggerEnter>(
*                        [this](const PhysicsEvents::TriggerEnter& e) {
*                            if (e.trigger == m_healthPack) GiveHealth(e.other);
*                        });
*
*                Every listener hears every event, so check whether it is
*                about your entity. Delivered later in the same frame, so an
*                entity named in one may be destroyed by then: check
*                World::IsAlive before touching it.
*
*                There is no Stay event: with a pile of crates or a horde
*                standing around, one every physics step per touching pair
*                would flood the bus. "Who is inside right now" is asked
*                instead, by whoever wants it: GetTriggerOccupants and
*                GetContacts on ECSPhysicsSystem.
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>

// Crucible engine
#include "ECS/Core/ECSCore.h"
#include "Math/Math.h"

namespace Crucible {

    namespace PhysicsEvents {

        // Two solid colliders started touching. Unity's OnCollisionEnter, sent
        // once for the pair (Unity calls it on both objects).
        struct CollisionEnter {

            Entity     a;
            Entity     b;
            Math::vec3 point       = Math::vec3(0.0f, 0.0f, 0.0f);   // where they touched
            Math::vec3 normal      = Math::vec3(0.0f, 1.0f, 0.0f);   // from a toward b
            float      impactSpeed = 0.0f;   // how fast they closed in along the normal, m/s: scale impact damage or volume by it

            bool  Involves(const Entity& entity) const { return a == entity || b == entity; }
            // The one that is not `self` (only meaningful when Involves(self)).
            Entity Other(const Entity& self) const { return a == self ? b : a; }

        };  // COLLISIONENTER STRUCT

        // They stopped touching, or one of them was destroyed. NOT sent when a
        // body merely falls asleep - a crate settled on the floor is still on it.
        struct CollisionExit {

            Entity a;
            Entity b;

            bool   Involves(const Entity& entity) const { return a == entity || b == entity; }
            Entity Other(const Entity& self) const { return a == self ? b : a; }

        };  // COLLISIONEXIT STRUCT

        // `other` entered the trigger collider `trigger` (Collider::m_isTrigger).
        // Unity's OnTriggerEnter. Mechs count: a character is seen by triggers.
        struct TriggerEnter {

            Entity trigger;
            Entity other;

        };  // TRIGGERENTER STRUCT

        // `other` left, or was destroyed while inside.
        struct TriggerExit {

            Entity trigger;
            Entity other;

        };  // TRIGGEREXIT STRUCT

    }   // NAMESPACE PHYSICSEVENTS

    // Something an entity is touching right now (ECSPhysicsSystem::GetContacts):
    // what Unity hands OnCollisionStay as a Collision.
    struct EntityContact {

        static constexpr std::uint32_t kMaxPoints = 4;

        Entity        other;
        bool          isTrigger   = false;       // either side a trigger: an overlap, not a collision
        Math::vec3    normal      = Math::vec3(0.0f, 1.0f, 0.0f);   // from the entity asked about toward `other`
        Math::vec3    points[kMaxPoints] {};      // Unity's Collision.contacts
        std::uint32_t pointCount  = 0;
        bool          resting     = false;       // asleep: still touching, the points are from before it slept

    };  // ENTITYCONTACT STRUCT

}   // NAMESPACE CRUCIBLE
