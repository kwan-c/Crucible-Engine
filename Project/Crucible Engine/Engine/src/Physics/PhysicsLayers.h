/*************************************************************************
* \file:         PhysicsLayers.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          22 September 2026
* \version       1.1
* \brief
*                PhysicsLayer: the collision layers, Unity's Layer
*                Collision Matrix. LayerMask: a set of them, for queries.
*
* \details
*                Kept in its own file because it is the designer's list.
*                The rules are written once in PhysicsLayers.cpp, and
*                PhysicsSystem.cpp builds Jolt's lookup tables from them
*                at startup.
*
* \history
*    -v1.0       first version
*    -v1.1       LayerMask (Unity's), for queries
*
**************************************************************************/
#pragma once

// STL
#include <cstddef>
#include <cstdint>
#include <type_traits>  // LayerMask

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

    /*
        Unity's LayerMask: a set of layers, for telling a query which ones it may
        hit. Built from names, so it cannot be got wrong the way 1 << layer can:

            LayerMask(PhysicsLayer::Enemy, PhysicsLayer::NonMoving)   enemies and walls
            LayerMask::All().Without(PhysicsLayer::Player)            everything but players

        One bit per layer underneath, exactly like Unity's.
    */
    class LayerMask {

    public:

        constexpr LayerMask() = default;     // empty: hits nothing

        template <typename... Rest>
            requires (std::is_same_v<Rest, PhysicsLayer> && ...)
        constexpr explicit LayerMask(PhysicsLayer first, Rest... rest) : m_bits((Bit(first) | ... | Bit(rest))) {}

        static constexpr LayerMask All() { LayerMask mask; mask.m_bits = (1u << kPhysicsLayerCount) - 1u; return mask; }

        constexpr LayerMask With(PhysicsLayer layer)    const { LayerMask mask = *this; mask.m_bits |= Bit(layer);  return mask; }
        constexpr LayerMask Without(PhysicsLayer layer) const { LayerMask mask = *this; mask.m_bits &= ~Bit(layer); return mask; }

        constexpr bool          Contains(PhysicsLayer layer) const { return (m_bits & Bit(layer)) != 0u; }
        constexpr bool          Contains(std::uint32_t layer) const { return layer < 32u && ((m_bits >> layer) & 1u) != 0u; }
        constexpr std::uint32_t Bits() const { return m_bits; }

    private:

        static constexpr std::uint32_t Bit(PhysicsLayer layer) { return 1u << static_cast<std::uint32_t>(layer); }

        std::uint32_t m_bits = 0u;

    };  // LAYERMASK CLASS

}   // NAMESPACE CRUCIBLE
