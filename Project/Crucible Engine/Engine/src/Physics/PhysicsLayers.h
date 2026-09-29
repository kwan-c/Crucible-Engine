#pragma once

// STL
#include <cstddef>
#include <cstdint>

// Crucible engine
#include "EngineExport.h"

// ---------------------------------------------------------------------------
// Collision layers.
//
// A layer answers one question: what is this body allowed to touch. It is the
// same idea as Unity's Layer Collision Matrix, and it is the cheapest filter in
// the engine - two bodies whose layers do not collide are thrown away before any
// shape maths happens at all.
//
// The list lives in its own file because it is the designer's list, not the
// physics programmer's. The matrix lives next to it, in PhysicsLayers.cpp, and
// PhysicsSystem.cpp builds Jolt's lookup tables from it at startup - so the
// rules are written once, here, and Jolt just gets told about them.
//
// Named PhysicsLayer, not Layer: Crucible::Layer is already the application
// layer class in Systems/Layer.h, and two things cannot share one name in one
// namespace.
// ---------------------------------------------------------------------------

namespace Crucible {

    // Jolt gives us 5 layers
    enum class PhysicsLayer : std::uint16_t {

        NonMoving = 0,  // the arena: floors, walls, ramps. Never moves
        Moving,         // crates and props: anything dynamic that is not a character
        Player,         // the mech
        Enemy,          // the horde
        Projectile,     // for when bullets become real bodies; unused while weapons are hitscan

        Count           // keep last: how many layers there are, used to size Jolt's tables

    };  // PHYSICSLAYER ENUM

    // How many real layers exist. Jolt wants this as a plain number.
    inline constexpr std::size_t kPhysicsLayerCount = static_cast<std::size_t>(PhysicsLayer::Count);

    // The matrix: may these two layers touch at all? Order does not matter.
    ENGINE_API bool LayersCollide(const PhysicsLayer& a, const PhysicsLayer& b);

    // For logs, and later for the editor's dropdown.
    ENGINE_API const char* ToString(const PhysicsLayer& layer);

}   // NAMESPACE CRUCIBLE
