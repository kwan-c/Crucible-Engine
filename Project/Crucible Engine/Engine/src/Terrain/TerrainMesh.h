/*************************************************************************
* \file:         TerrainMesh.h
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       1.0
* \brief
*                Turns a TerrainGrid into the mesh that draws it: one
*                function, no state, so it can be tested on its own.
*
* \details
*                Low-poly and flat-shaded, as in the Unity prototype: every
*                cell is two triangles with their own three vertices, so
*                each face has one normal and the ground reads as facets.
*                Colour is per vertex, one per TerrainRegion, which the
*                default shader multiplies into the material's albedo.
*
*                Triangles follow HeightGrid's diagonal, (x, z) to
*                (x + 1, z + 1), which is the one Jolt's heightfield uses,
*                so the drawn ground and the collision ground match.
*                Counter-clockwise seen from above (the engine's front
*                face), like Primitives::MakePlane.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// Crucible engine
#include "Graphics/RenderTypes.h"          // MeshData
#include "Math/Math.h"
#include "Terrain/TerrainComponents.h"

namespace Crucible {

    // The colour of each TerrainRegion, indexed by its value.
    struct TerrainPalette {
        Math::vec3 colours[static_cast<int>(TerrainRegion::Count)];
    };  // TERRAINPALETTE STRUCT

    // A Terrain component's colours as a palette.
    TerrainPalette PaletteOf(const Terrain& terrain);

    /*!
        \brief
        The mesh of a terrain, in the grid's own space (centred on its
        entity, as HeightGrid describes).

        \param grid
        A valid grid (TerrainGrid::IsValid); an invalid one gives an empty
        mesh.

        \return
        6 vertices and 6 indices per cell.
    */
    MeshData BuildTerrainMesh(const TerrainGrid& grid, const TerrainPalette& palette);

}   // NAMESPACE CRUCIBLE
