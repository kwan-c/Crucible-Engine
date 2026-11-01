/*************************************************************************
* \file:         PhysicsContacts.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          28 October 2026
* \version       1.0
* \brief
*                Contacts: which bodies are touching, and the Began /
*                Ended list each Step leaves behind - what Unity's
*                OnCollisionEnter/Exit and OnTriggerEnter/Exit are made of.
*
* \details
*                During a Step, PhysicsContactListener (PhysicsInternal.h)
*                only writes down what Jolt reports. ProcessContacts runs
*                after the Step, on the main thread, and decides what it
*                means. Most of the deciding is about one Jolt rule: only
*                AWAKE bodies have contacts. A crate that settles on the
*                floor falls asleep, Jolt drops its contacts and says
*                "removed" - but the crate is still lying there. So a
*                pair that loses its contacts while asleep is kept,
*                marked resting, and no Ended is sent.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "PhysicsSystem.h"

// Jolt - Jolt.h always comes before any other Jolt header
#include <Jolt/Jolt.h>

// Crucible engine
#include "Physics/PhysicsInternal.h"

namespace {

    using namespace Crucible;
    using RawContact = PhysicsContactListener::RawContact;

    ContactEvent ToEvent(const ContactEventType& type, const RawContact& raw) {

        ContactEvent event;
        event.m_type      = type;
        event.m_body1     = BodyHandle{ raw.body1 };
        event.m_body2     = BodyHandle{ raw.body2 };
        event.m_userData1 = raw.userData1;
        event.m_userData2 = raw.userData2;
        event.m_isSensor1 = raw.sensor1;
        event.m_isSensor2 = raw.sensor2;
        event.m_normal    = raw.normal;

        // Where they touched: the middle of the contact points. A crate landing
        // flat touches at four corners, and its middle is where the hit "is".
        if (raw.pointCount > 0) {
            Math::vec3 sum(0.0f, 0.0f, 0.0f);
            for (std::uint32_t i = 0; i < raw.pointCount; ++i) sum += raw.points[i];
            event.m_point = sum / static_cast<float>(raw.pointCount);
        }
        event.m_impactSpeed = type == ContactEventType::Began ? raw.impactSpeed : 0.0f;
        return event;

    }   // ToEvent()

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    /*!
        \brief
        Still touching, only asleep? True when both bodies still exist and
        neither is awake (a static body is never awake, so a crate asleep on
        the floor or inside a trigger counts).
        \details
        One awake body is not "resting": Jolt keeps touching bodies awake
        together, so if one is awake and the contact went, they really parted.
    */
    static bool Resting(JPH::BodyInterface& bodies, const RawContact& pair) {

        const JPH::BodyID one(pair.body1);
        const JPH::BodyID two(pair.body2);
        if (!bodies.IsAdded(one) || !bodies.IsAdded(two)) return false;     // destroyed: really gone
        return !bodies.IsActive(one) && !bodies.IsActive(two);

    }   // Resting()

    /*!
        \brief
        Turns the step's notes into m_touching changes and this step's
        Began / Ended events. After every Step, on the main thread.
    */
    void Physics::Impl::ProcessContacts() {

        m_contactFilter->TakeRaw(m_rawContacts);
        JPH::BodyInterface& bodies = Bodies();

        for (const RawContact& raw : m_rawContacts) {

            const std::uint64_t key = PhysicsContactListener::PairKey(raw.body1, raw.body2);

            switch (raw.kind) {

            // STARTED TOUCHING ---------------------------------------------------- //
            case RawContact::Kind::Added: {
                auto [it, inserted] = m_touching.try_emplace(key);
                Touch& touch   = it->second;
                touch.latest   = raw;
                touch.seenStep = m_stepCount;

                if (inserted) {
                    touch.subShapes = 1;
                    m_contactEvents.push_back(ToEvent(ContactEventType::Began, raw));
                }
                else if (touch.resting) {
                    // Woke up still touching: Jolt found the contact again, but
                    // for the game nothing happened - the crate never left.
                    touch.resting   = false;
                    touch.subShapes = 1;
                }
                else {
                    ++touch.subShapes;      // a second sub-shape of the same pair (compound shapes)
                }
                break;
            }

            // STILL TOUCHING: only the contact points are new --------------------- //
            case RawContact::Kind::Persisted: {
                const auto found = m_touching.find(key);
                if (found == m_touching.end()) break;
                found->second.latest   = raw;
                found->second.seenStep = m_stepCount;
                break;
            }

            // STOPPED TOUCHING - or fell asleep, or was destroyed ------------------ //
            case RawContact::Kind::Removed: {
                const auto found = m_touching.find(key);
                if (found == m_touching.end()) break;

                Touch& touch = found->second;
                if (touch.subShapes > 0) --touch.subShapes;
                if (touch.subShapes > 0) break;           // another sub-shape still touches

                if (Resting(bodies, touch.latest)) {
                    touch.resting  = true;                 // asleep in place: keep it, say nothing
                    touch.seenStep = m_stepCount;
                    break;
                }

                // Ended carries the user values from when they last touched: the
                // body itself may be gone (Jolt sends Removed one step after a
                // body is destroyed, with nothing but its id).
                m_contactEvents.push_back(ToEvent(ContactEventType::Ended, touch.latest));
                m_touching.erase(found);
                break;
            }

            }   // switch

        }

        // A resting pair whose sleeper woke up somewhere else - picked up, pushed
        // off, teleported - gets no word from Jolt at all: there is no contact, so
        // there is nothing to remove. So: no longer asleep, and Jolt did not find
        // the contact again this step, means they parted. One pass over the pairs.
        for (auto it = m_touching.begin(); it != m_touching.end();) {
            Touch& touch = it->second;
            if (touch.resting && touch.seenStep != m_stepCount && !Resting(bodies, touch.latest)) {
                m_contactEvents.push_back(ToEvent(ContactEventType::Ended, touch.latest));
                it = m_touching.erase(it);
            }
            else {
                ++it;
            }
        }

    }   // Physics::Impl::ProcessContacts()

    std::uint32_t Physics::GetContactEventCount() const {
        return static_cast<std::uint32_t>(m_impl->m_contactEvents.size());
    }

    bool Physics::GetContactEvent(std::uint32_t index, ContactEvent& outEvent) const {
        const Impl& impl = *m_impl;
        if (index >= impl.m_contactEvents.size()) return false;
        outEvent = impl.m_contactEvents[index];
        return true;
    }

    /*!
        \brief
        Visits every body this one touches right now, sleepers included.
        \details
        Any valid handle, not only bodies CreateBody made: a character's
        hidden body (GetCharacterBody) is how a mech asks what it is
        standing in. The normal is turned around when the body asked about
        is the pair's second one, so it always points away from it.
    */
    void Physics::ForEachContact(const BodyHandle& body, const std::function<void(const BodyContact&)>& visit) const {

        const Impl& impl = *m_impl;
        if (!impl.m_initialised || !body.IsValid() || !visit) return;

        for (const auto& [key, touch] : impl.m_touching) {

            const RawContact& pair  = touch.latest;
            const bool        first = pair.body1 == body.m_id;
            if (!first && pair.body2 != body.m_id) continue;

            BodyContact contact;
            contact.m_other         = BodyHandle{ first ? pair.body2 : pair.body1 };
            contact.m_otherUserData = first ? pair.userData2 : pair.userData1;
            contact.m_otherIsSensor = first ? pair.sensor2   : pair.sensor1;
            contact.m_selfIsSensor  = first ? pair.sensor1   : pair.sensor2;
            contact.m_normal        = first ? pair.normal    : -pair.normal;
            contact.m_pointCount    = pair.pointCount;
            for (std::uint32_t i = 0; i < pair.pointCount; ++i) contact.m_points[i] = pair.points[i];
            contact.m_resting       = touch.resting;
            visit(contact);
        }

    }   // Physics::ForEachContact()

}   // NAMESPACE CRUCIBLE
