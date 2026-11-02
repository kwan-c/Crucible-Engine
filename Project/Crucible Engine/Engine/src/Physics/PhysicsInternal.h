/*************************************************************************
* \file:         PhysicsInternal.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          27 September 2026
* \version       1.3
* \brief
*                Engine-private: struct Physics::Impl, every Jolt object
*                the physics system owns.
*
* \details
*                Only the .cpp files in Physics/ may include it. The order
*                of the members matters, because they are destroyed in
*                reverse order and Jolt's world must go before what it
*                points at.
*
* \history
*    -v1.0       first version
*    -v1.1       maths types moved to glm
*    -v1.2       joints: slots, broken list, and the contact filter that
*                lets jointed bodies pass through each other
*    -v1.3       the contact filter became PhysicsContactListener: it also
*                records contacts for collision and trigger messages;
*                m_touching and m_contactEvents
*
**************************************************************************/
#pragma once

// Jolt - Jolt.h always comes before any other Jolt header
#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>

// STL
#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Crucible engine
#include "Physics/PhysicsSystem.h"

// ---------------------------------------------------------------------------
// Engine-private: the inside of Crucible::Physics.
//
// PhysicsSystem.h promises Game that it will never show it a Jolt header, and
// keeps that promise by declaring `struct Impl;` and holding a pointer to it.
// This file is where Impl actually gets defined, so every Jolt object the
// system owns lives here.
//
// Only Physics/*.cpp may include it. The day something outside Physics/ needs
// it, that is a sign the class is missing a function, not that this header
// should travel.
// ---------------------------------------------------------------------------

namespace Crucible {

    /*
        Jolt has room for exactly one contact listener, so this one does two jobs.

        1. Unity's enableCollision = false: two bodies joined by a joint pass
        through each other. Without it a ragdoll explodes on frame one - the
        capsules of a shin and a thigh overlap at the knee, so they shove each
        other apart as hard as the joint pulls them together.
        Jolt asks this object about every pair of bodies about to touch.
        Answering "reject all contacts for this pair" makes Jolt skip the pair
        for the rest of the step, so it costs one lookup per touching pair.

        2. Collision and trigger messages: it writes down every pair that starts
        touching, keeps touching, or stops touching (RawContact). Nothing is
        decided here - Impl::ProcessContacts turns the notes into events after
        the step, on the main thread.

        Threads: Jolt calls all of these from its worker threads during Step,
        several at once. OnContactValidate only reads; Ignore and Unignore
        write, but only between Steps (CreateJoint / DestroyJoint), so the two
        never overlap. The notes are written under m_rawMutex, and read after
        the step, when no worker is running.
    */
    class PhysicsContactListener final : public JPH::ContactListener {

    public:

        // One key per pair, the same whichever body is asked about first.
        static std::uint64_t PairKey(std::uint32_t a, std::uint32_t b) {
            const std::uint64_t low  = a < b ? a : b;
            const std::uint64_t high = a < b ? b : a;
            return (high << 32) | low;
        }

        // Counted, not a set: two joints between the same pair (a hinge plus a
        // spring) must both be destroyed before the pair collides again.
        void Ignore(std::uint32_t a, std::uint32_t b) { ++m_pairs[PairKey(a, b)]; }

        void Unignore(std::uint32_t a, std::uint32_t b) {
            const auto found = m_pairs.find(PairKey(a, b));
            if (found != m_pairs.end() && --found->second == 0) m_pairs.erase(found);
        }

        void Clear() { m_pairs.clear(); }

        JPH::ValidateResult OnContactValidate(const JPH::Body& body1, const JPH::Body& body2, JPH::RVec3Arg,
                                              const JPH::CollideShapeResult&) override {
            if (m_pairs.empty()) return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;   // the common case: no joints at all

            const std::uint64_t key = PairKey(body1.GetID().GetIndexAndSequenceNumber(), body2.GetID().GetIndexAndSequenceNumber());
            return m_pairs.contains(key) ? JPH::ValidateResult::RejectAllContactsForThisBodyPair
                                         : JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
        }

        // --- the notes for collision and trigger messages --------------------- //

        // One thing Jolt said about one pair, as it said it. Jolt reports per
        // SUB-SHAPE pair; with our single-shape bodies that is one per body pair.
        struct RawContact {

            enum class Kind : std::uint8_t { Added, Persisted, Removed };

            Kind          kind        = Kind::Added;
            std::uint32_t body1       = 0;          // index and sequence number, as BodyHandle::m_id
            std::uint32_t body2       = 0;
            // The rest is filled for Added and Persisted only: by Removed, a body
            // may already be destroyed, and Jolt hands over nothing but the ids.
            std::uint64_t userData1   = 0;
            std::uint64_t userData2   = 0;
            bool          sensor1     = false;
            bool          sensor2     = false;
            Math::vec3    normal      = Math::vec3(0.0f, 1.0f, 0.0f);   // from body 1 toward body 2
            Math::vec3    points[BodyContact::kMaxPoints] {};
            std::uint32_t pointCount  = 0;
            float         impactSpeed = 0.0f;       // Added only

        };  // RAWCONTACT STRUCT

        // Two bodies started touching. The velocities are still the ones from
        // before the hit - the solver has not run yet - which is what makes the
        // impact speed measurable here and nowhere later.
        void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold,
                            JPH::ContactSettings&) override {
            RawContact raw = Describe(RawContact::Kind::Added, body1, body2, manifold);
            const JPH::RVec3 at = manifold.GetWorldSpaceContactPointOn1(0);
            const JPH::Vec3  closing = body1.GetPointVelocity(at) - body2.GetPointVelocity(at);
            raw.impactSpeed = std::max(closing.Dot(manifold.mWorldSpaceNormal), 0.0f);   // the normal points 1 -> 2
            Record(raw);
        }

        // Still touching: only the contact points are new. Once per touching pair
        // per step - which is why this does nothing but write them down.
        void OnContactPersisted(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold,
                                JPH::ContactSettings&) override {
            Record(Describe(RawContact::Kind::Persisted, body1, body2, manifold));
        }

        // Stopped touching - or one of them fell asleep, or was destroyed. Which
        // one it was can only be told after the step (ProcessContacts).
        void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
            RawContact raw;
            raw.kind  = RawContact::Kind::Removed;
            raw.body1 = pair.GetBody1ID().GetIndexAndSequenceNumber();
            raw.body2 = pair.GetBody2ID().GetIndexAndSequenceNumber();
            Record(raw);
        }

        // The step's notes, for ProcessContacts. Swapped out, not copied: the
        // vector keeps its memory for the next step.
        void TakeRaw(std::vector<RawContact>& out) {
            std::lock_guard<std::mutex> lock(m_rawMutex);
            out.swap(m_raw);
            m_raw.clear();
        }

    private:

        static RawContact Describe(RawContact::Kind kind, const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold) {
            RawContact raw;
            raw.kind       = kind;
            raw.body1      = body1.GetID().GetIndexAndSequenceNumber();
            raw.body2      = body2.GetID().GetIndexAndSequenceNumber();
            raw.userData1  = body1.GetUserData();
            raw.userData2  = body2.GetUserData();
            raw.sensor1    = body1.IsSensor();
            raw.sensor2    = body2.IsSensor();
            raw.normal     = Math::vec3(manifold.mWorldSpaceNormal.GetX(), manifold.mWorldSpaceNormal.GetY(), manifold.mWorldSpaceNormal.GetZ());
            raw.pointCount = std::min<std::uint32_t>(static_cast<std::uint32_t>(manifold.mRelativeContactPointsOn1.size()), BodyContact::kMaxPoints);
            for (std::uint32_t i = 0; i < raw.pointCount; ++i) {
                const JPH::RVec3 point = manifold.GetWorldSpaceContactPointOn1(i);
                raw.points[i] = Math::vec3(static_cast<float>(point.GetX()), static_cast<float>(point.GetY()), static_cast<float>(point.GetZ()));
            }
            return raw;
        }

        void Record(const RawContact& raw) {
            std::lock_guard<std::mutex> lock(m_rawMutex);
            m_raw.push_back(raw);
        }

        std::unordered_map<std::uint64_t, std::uint32_t> m_pairs;   // pair key -> how many joints ignore it

        std::mutex              m_rawMutex;
        std::vector<RawContact> m_raw;          // this step's notes, in whatever order the workers wrote them

    };  // PHYSICSCONTACTLISTENER CLASS

    struct Physics::Impl {

        // --- THE JOLT WORLD ------------------------------------------------ //
        // Declaration order is load-bearing. Jolt's PhysicsSystem keeps
        // references to the three layer tables, so they are declared before it
        // and therefore destroyed after it. Swap two of these lines and
        // shutdown starts crashing in a way that looks unrelated.

        PhysicsSettings                                           m_settings {};
        std::unique_ptr<JPH::ObjectLayerPairFilterTable>          m_layerPairs;       // which layers collide
        std::unique_ptr<JPH::BroadPhaseLayerInterfaceTable>       m_layerToTree;      // which tree each layer lives in
        std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable>  m_layerVsTree;      // which trees a layer must search
        std::unique_ptr<JPH::TempAllocatorImpl>                   m_tempAllocator;    // the per-step scratch stack
        std::unique_ptr<JPH::JobSystemThreadPool>                 m_jobSystem;        // the worker threads
        std::unique_ptr<PhysicsContactListener>                   m_contactFilter;    // the world holds a pointer to it, so it is declared before
        std::unique_ptr<JPH::PhysicsSystem>                       m_world;            // Jolt's world itself

        bool m_initialised = false;

        // --- CHARACTERS ---------------------------------------------------- //
        // Declared after m_world so they are destroyed BEFORE it: a character
        // holds a pointer to the world, and its inner body lives inside it.

        std::unique_ptr<JPH::CharacterVsCharacterCollisionSimple> m_characterVsCharacter;   // lets mechs bump into each other

        struct Character {

            JPH::Ref<JPH::CharacterVirtual> character;
            float                           stepOffset = 0.3f;
            JPH::ObjectLayer                layer      = 0;
            JPH::Vec3                       velocity   = JPH::Vec3::sZero();   // what the last move achieved
            std::uint32_t                   generation = 1;
            bool                            inUse      = false;

        };  // CHARACTER STRUCT

        std::vector<Character>     m_characters;            // slot 0 is never handed out
        std::vector<std::uint32_t> m_freeCharacterSlots;

        // --- JOINTS ---------------------------------------------------------- //
        // Slots and generations, the same scheme as characters. A Jolt
        // constraint holds raw pointers to its two bodies, so a joint must be
        // removed BEFORE either body is destroyed - RemoveJointsOn is what
        // DestroyBody calls first, and Shutdown removes every joint before
        // any body.

        static constexpr std::uint32_t kNoBody = 0xFFFFFFFFu;   // bodyB of a joint to the world

        struct Joint {

            JPH::Ref<JPH::TwoBodyConstraint> constraint;
            JointType                        type        = JointType::Fixed;
            std::uint32_t                    bodyA       = kNoBody;   // body ids, to find the joints on a body
            std::uint32_t                    bodyB       = kNoBody;
            float                            breakForce  = 0.0f;      // 0 = never
            float                            breakTorque = 0.0f;
            std::uint64_t                    userData    = 0;
            bool                             ignoring    = false;     // registered with m_contactFilter
            std::uint32_t                    generation  = 1;
            bool                             inUse       = false;

        };  // JOINT STRUCT

        std::vector<Joint>         m_joints;                // slot 0 is never handed out
        std::vector<std::uint32_t> m_freeJointSlots;

        // The joints that broke during the last Step, refilled every Step.
        struct Broken {

            JointHandle   m_joint    {};
            std::uint64_t m_userData = 0;

        };  // BROKEN STRUCT

        std::vector<Broken> m_brokenJoints;

        // The live joint behind a handle, or null if the handle is stale.
        Joint* Slot(const JointHandle& handle) {
            if (!m_initialised || !handle.IsValid() || handle.m_index >= m_joints.size()) return nullptr;
            Joint& slot = m_joints[handle.m_index];
            return (slot.inUse && slot.generation == handle.m_generation) ? &slot : nullptr;
        }

        const Joint* Slot(const JointHandle& handle) const {
            return const_cast<Impl*>(this)->Slot(handle);
        }

        // PhysicsJoints.cpp. RemoveJoint takes one out of Jolt and frees its
        // slot; RemoveJointsOn does that for every joint touching a body;
        // BreakJoints runs after each Step and removes any that held too much.
        void RemoveJoint(std::uint32_t index);
        void RemoveJointsOn(std::uint32_t bodyId);
        void BreakJoints(float fixedDt);

        // --- THE TWO SEARCH TREES ------------------------------------------ //
        // Before Jolt tests shapes, it asks a much cheaper question: are these
        // two even near each other? It answers that with bounding-box trees,
        // and it wants bodies sorted into a small number of them. Two is right
        // for us: the arena, which is built once and then never moves, and
        // everything else, which is rebuilt constantly.

        static constexpr JPH::BroadPhaseLayer kTreeStatic { 0 };
        static constexpr JPH::BroadPhaseLayer kTreeMoving { 1 };
        static constexpr JPH::uint            kTreeCount = 2;

        // --- WHAT WE CREATED ----------------------------------------------- //

        // Every body CreateBody made and DestroyBody has not yet removed.
        // Two jobs: it is the "is this handle one of ours" check behind every
        // public function, and it is the list Shutdown destroys.
        std::unordered_set<std::uint32_t> m_bodies;

        // One entry per body that moved during the last Step, refilled every
        // Step. Only what GetMovedBody hands back, nothing else.
        struct Moved {

            std::uint64_t m_userData = 0;
            Math::vec3    m_position = Math::vec3(0.0f, 0.0f, 0.0f);
            Math::quat    m_rotation = Math::quat(1.0f, 0.0f, 0.0f, 0.0f);

        };  // MOVED STRUCT

        std::vector<Moved> m_movedBodies;

        // --- CONTACTS (PhysicsContacts.cpp) ---------------------------------- //

        // Every pair touching right now, keyed by PairKey of the two body ids.
        // Kept across steps: it is what "who is inside this trigger" reads, and
        // the only place a pair's user values survive until its Removed, when
        // Jolt hands over bare ids. A pair whose moving body fell asleep stays,
        // marked resting - the crate is still on the floor.
        struct Touch {

            PhysicsContactListener::RawContact latest {};   // the last Added/Persisted: ids, user values, sensors, points
            std::uint32_t subShapes = 0;     // Added minus Removed; 1 for our single-shape bodies
            bool          resting   = false; // contacts dropped because a body fell asleep
            std::uint64_t seenStep  = 0;     // the last step Jolt said anything about it

        };  // TOUCH STRUCT

        std::unordered_map<std::uint64_t, Touch>                m_touching;
        std::vector<ContactEvent>                               m_contactEvents;   // the last Step's, refilled every Step
        std::vector<PhysicsContactListener::RawContact>         m_rawContacts;     // scratch, reused every Step
        std::uint64_t                                           m_stepCount = 0;

        // Turns the step's notes into m_touching changes and m_contactEvents.
        // After every Step, on the main thread.
        void ProcessContacts();

        // --- HELPERS THE .CPP FILES SHARE ----------------------------------- //

        // Jolt's front desk: bodies are created, moved and pushed through this.
        // Const because reading it does not change the Impl - what it hands
        // back is free to modify bodies, which is Jolt's design, not ours.
        JPH::BodyInterface& Bodies() const { return m_world->GetBodyInterface(); }

        static JPH::BodyID ToId(const BodyHandle& body) { return JPH::BodyID(body.m_id); }

        // Turns a ShapeDesc into a Jolt shape (PhysicsShapes.cpp). Returns null,
        // and says why in the log, if the description cannot make one.
        static JPH::RefConst<JPH::Shape> BuildShape(const ShapeDesc& desc);

        // The first line of every public function: is the world up, is the
        // handle valid, and is this body one we made? Anything else is a
        // stale handle, and stale handles do nothing rather than crash.
        // The character behind a handle, or null if the handle is stale. The
        // generation must match, which is what stops a recycled slot answering
        // to a handle from the mech that used to live there.
        Character* Slot(const CharacterHandle& handle) {
            if (!m_initialised || !handle.IsValid() || handle.m_index >= m_characters.size()) return nullptr;
            Character& slot = m_characters[handle.m_index];
            return (slot.inUse && slot.generation == handle.m_generation) ? &slot : nullptr;
        }

        // The const version, written once by borrowing the non-const one.
        const Character* Slot(const CharacterHandle& handle) const {
            return const_cast<Impl*>(this)->Slot(handle);
        }

        bool Has(const BodyHandle& body) const {
            return m_initialised && body.IsValid() && m_bodies.contains(body.m_id);
        }

    };  // PHYSICS::IMPL STRUCT

}   // NAMESPACE CRUCIBLE
