#pragma once

// STL
#include <cstdint>

// Crucible engine
#include "EngineExport.h"

// ---------------------------------------------------------------------------
// Collision layers.
//
// A layer answers one question: what is this body allowed to touch. It is the
// same idea as Unity's Layer Collision Matrix, and it is the cheapest filter in
// the engine - two bodies whose layers do not collide are rejected before any
// shape maths happens at all.
//
// The list lives in its own file because it is the designer's list, not the
// physics programmer's. The matrix itself (who collides with whom) is built in
// PhysicsSystem.cpp, because that is where Jolt's table wants to live.
// ---------------------------------------------------------------------------

namespace Crucible {

    enum class Layer : std::uint16_t {

        NonMoving = 0,  // the arena: floors, walls, ramps. Never moves
        Moving,         // crates and props: anything dynamic that is not a character
        Player,         // the mech
        Enemy,          // the horde
        Projectile,     // for when bullets become real bodies; unused while weapons are hitscan
        Trigger,        // sensors: pickups and volumes. Reports touches, blocks nothing

        Count           // keep last. The number of layers, used to size the matrix

    };  // LAYER ENUM

    // For logs, and later for the editor's dropdown.
    ENGINE_API const char* ToString(const Layer& layer);

}   // NAMESPACE CRUCIBLE
