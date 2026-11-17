/*************************************************************************
* \file:         TerrainGridNavTerrain.h
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          17 November 2026
* \version       1.0
* \brief
*                TerrainGridNavTerrain: the generated terrain's answers to
*                navigation's two questions (is this a wall, how high is the
*                ground here), read straight from the Terrain component's
*                grid - the hand-off Unity's generator made through
*                NavTerrain.Register.
*
* \details
*                The generator already hands the engine everything needed
*                (Terrain.Build: corner heights and a region per cell), so
*                navigation reads that instead of measuring the terrain again
*                with rays:
*                  - GroundHeight: the exact height on the triangle under the
*                    point (HeightGrid::SampleHeight), the same triangles as
*                    the mesh and the HeightField collider;
*                  - IsWall: the generator cell under the point is a Cliff,
*                    or the point is off the terrain.
*                One definition of a cliff, then: the generator's own, also
*                used for the terrain's colours and the player's slope limit.
*                The nav grid asks at each of its cell centres, so each nav
*                cell takes the flag of the generator cell under its centre
*                (with 4 m generator cells and 2 m nav cells, exactly 2 x 2
*                nav cells per generator cell).
*
*                No physics, so it works in Edit mode too: Generate, then the
*                Inspector's Bake shows the real cliffs.
*
*                Not exported (no ENGINE_API), like NavGrid: only the engine
*                uses it.
*
* \history
*    -v1.0       first version (F2: replaces the ray-sampled
*                HeightfieldNavTerrain)
*
**************************************************************************/
#pragma once

// STL
#include <memory>       // std::shared_ptr

// Crucible engine
#include "Math/Math.h"
#include "Navigation/NavTerrain.h"
#include "Terrain/TerrainComponents.h"   // TerrainGrid, TerrainRegion

namespace Crucible {

    class TerrainGridNavTerrain final : public NavTerrain {

    public:

        /*!
            \brief
            Points it at a built terrain.

            \param grid
            The Terrain component's grid. Shared, so regenerating the terrain
            (a new grid) never frees the one this still reads; the next bake
            picks up the new one.

            \param toWorld
            The terrain entity's world matrix (position, rotation about Y and
            scale all apply, as for its mesh and collider).

            \param offTerrainHeight
            The height GroundHeight gives off the terrain (where IsWall is
            true anyway): the Nav Surface's height.

            \return
            False (and it reads as empty) if the grid is missing or invalid.
        */
        bool Reset(std::shared_ptr<const TerrainGrid> grid, const Math::mat4& toWorld, float offTerrainHeight);

        // Forgets the grid: everywhere is a wall, at offTerrainHeight.
        void Clear();

        bool HasGrid() const { return m_grid != nullptr; }

        bool  IsWall(const Math::vec3& position) const override;
        float GroundHeight(const Math::vec3& position) const override;

    private:

        // The point in the grid's own space (x, z), from a world position.
        Math::vec2 ToLocal(const Math::vec3& position) const;

        std::shared_ptr<const TerrainGrid> m_grid;
        Math::mat4 m_toWorld = Math::mat4(1.0f);
        Math::mat4 m_toLocal = Math::mat4(1.0f);
        float      m_offTerrainHeight = 0.0f;

    };  // TERRAINGRIDNAVTERRAIN CLASS

}   // NAMESPACE CRUCIBLE
