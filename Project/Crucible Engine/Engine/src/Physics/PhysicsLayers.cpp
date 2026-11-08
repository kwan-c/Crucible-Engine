/*************************************************************************
* \file:         PhysicsLayers.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          22 September 2026
* \version       1.0
* \brief
*                The collision matrix: which layers may touch which.
*
* \details
*                Everything collides with everything except the listed
*                pairs, so a new layer behaves sensibly the day it is
*                added.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "PhysicsLayers.h"

namespace Crucible {

    /*!
        \brief
        The collision matrix. Everything collides with everything, apart from the
        two exceptions below, so a new layer works sensibly the day it is added.

        \param a, b
        The two layers, in either order.
        
        \return
        True if a body on layer a may touch a body on layer b.
    */
    bool LayersCollide(const PhysicsLayer& a, const PhysicsLayer& b) {

        // Is this the pair {x, y}, whichever way round it was asked?
        const auto pair = [&](const PhysicsLayer& x, const PhysicsLayer& y) {
            return (a == x && b == y) || (a == y && b == x);
        };

        // A dead body lands on the floor and nothing else: the player and the
        // horde walk through it, and corpses don't pile up on each other.
        if (a == PhysicsLayer::Corpse) return b == PhysicsLayer::NonMoving;
        if (b == PhysicsLayer::Corpse) return a == PhysicsLayer::NonMoving;

        // The arena never moves, so two pieces of it can never touch for the
        // first time. Jolt would otherwise test every wall against every floor.
        if (pair(PhysicsLayer::NonMoving, PhysicsLayer::NonMoving)) return false;

        // Bullets fly through each other.
        if (pair(PhysicsLayer::Projectile, PhysicsLayer::Projectile)) return false;

        return true;

    }   // LayersCollide()

    const char* ToString(const PhysicsLayer& layer) {

        switch (layer) {
            case PhysicsLayer::NonMoving:   return "NonMoving";
            case PhysicsLayer::Moving:      return "Moving";
            case PhysicsLayer::Player:      return "Player";
            case PhysicsLayer::Enemy:       return "Enemy";
            case PhysicsLayer::Projectile:  return "Projectile";
            case PhysicsLayer::Corpse:      return "Corpse";
            case PhysicsLayer::Count:       break;
        }

        return "unknown";       // a layer that is not in the enum, so the log says so instead of crashing

    }   // ToString()

}   // NAMESPACE CRUCIBLE
