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

// STL
#include <cstdint>
#include <memory>
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
            Math::quat    m_rotation = Math::quat::fromIdentity();

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
