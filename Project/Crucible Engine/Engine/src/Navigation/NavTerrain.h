/*************************************************************************
* \file:         NavTerrain.h
* \author:       Jean David Vidal Cruzado (main author; design and Unity original),
*                Hugo Low Ren Hao (initial engine port)
* \par:          Email: jeandavidvidal.c@digipen.edu,
*                Email: low.h@digipen.edu
* \date          31 October 2026
* \version       1.2
* \brief
*                NavTerrain: the two questions navigation asks the ground
*                (is this a wall, how high is it here). FlatNavTerrain:
*                the answer when no terrain exists.
*
* \details
*                Ported from Jean David's Unity prototype (NavTerrain.cs),
*                where it was a static hook the level generator filled in.
*                Here it is an interface instead: the terrain system
*                implements it, and navigation never includes any
*                level-generation code.
*
* \history
*    -v1.0       first version
*    -v1.1       comment: the step / agent-height band (was "shoulder")
*    -v1.2       Floors: every floor in a column where the ground is a
*                cave volume, not one height
*
**************************************************************************/
#pragma once

// STL
#include <vector>

// Crucible engine
#include "Math/Math.h"

// ---------------------------------------------------------------------------
// Why an interface and not two function pointers:
//
// The generator owns its terrain data (heights, cliffs), so the natural owner
// of the answers is an object, not two loose functions. Whoever builds the
// terrain derives from NavTerrain and hands navigation a reference to it.
//
// Why FlatNavTerrain exists: the engine has no terrain system yet, and test
// scenes never will. Instead of every caller checking "is there a terrain?",
// navigation always has one - the flat one when nothing better was given. So
// the grid bake and the agents' ground snap never need a null check.
// ---------------------------------------------------------------------------

namespace Crucible {

    // One floor in a column of cave: where rock below meets air above.
    struct NavFloor {

        float height   = 0.0f;    // world y
        float headroom = 0.0f;    // up to the rock above it; infinity under open sky
        bool  steep    = false;   // steeper than the walkable slope: a wall to walk on

    };  // NAVFLOOR STRUCT

    class NavTerrain {

    public:

        // Virtual so deleting a terrain through a NavTerrain* runs the real
        // class's destructor too.
        virtual ~NavTerrain() = default;

        // True on a cliff (steeper than the walkable slope) or anywhere off the
        // generated map. The grid bake marks a cell blocked when its centre is a wall.
        virtual bool  IsWall(const Math::vec3& position) const = 0;

        // The ground's height under this point (y is ignored on the way in).
        // Agents use it to stand on the ground; the bake uses it to measure
        // obstacles against the step / agent-height band.
        virtual float GroundHeight(const Math::vec3& position) const = 0;

        // Where the ground is a cave (more than one floor in a column), every
        // floor in the column through this point, lowest first, and true: the
        // grid uses these there instead of GroundHeight and IsWall. False
        // where the plain ground applies - everywhere, unless overridden.
        virtual bool Floors(const Math::vec3& position, std::vector<NavFloor>& floors) const {
            (void)position;
            floors.clear();
            return false;
        }

    };  // NAVTERRAIN CLASS

    // No terrain: no walls anywhere, and the ground is a flat plane at m_groundY.
    class FlatNavTerrain final : public NavTerrain {

    public:

        explicit FlatNavTerrain(float groundY = 0.0f) : m_groundY(groundY) {}

        bool  IsWall(const Math::vec3&) const override       { return false; }
        float GroundHeight(const Math::vec3&) const override { return m_groundY; }

    private:

        float m_groundY = 0.0f;

    };  // FLATNAVTERRAIN CLASS

}   // NAMESPACE CRUCIBLE
