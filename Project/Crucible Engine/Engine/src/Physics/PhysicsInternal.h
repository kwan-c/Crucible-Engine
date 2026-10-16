/*************************************************************************
* \file:         PhysicsInternal.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          27 September 2026
* \version       1.2
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
#include <cstdint>
#include <memory>
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
        Unity's enableCollision = false: two bodies joined by a joint pass
        through each other. Without it a ragdoll explodes on frame one - the
        capsules of a shin and a thigh overlap at the knee, so they shove each
        other apart as hard as the joint pulls them together.

        Jolt asks this object about every pair of bodies about to touch.
        Answering "reject all contacts for this pair" makes Jolt skip the pair
        for the rest of the step, so it costs one lookup per touching pair.

        Threads: Jolt calls OnContactValidate from its worker threads during
        Step, and they only read. Ignore and Unignore write, and are only called
        between Steps (CreateJoint / DestroyJoint), so the two never overlap.
    */
    class JointContactFilter final : public JPH::ContactListener {

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

    private:

        std::unordered_map<std::uint64_t, std::uint32_t> m_pairs;   // pair key -> how many joints ignore it

    };  // JOINTCONTACTFILTER CLASS

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
        std::unique_ptr<JointContactFilter>                       m_contactFilter;    // the world holds a pointer to it, so it is declared before
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
