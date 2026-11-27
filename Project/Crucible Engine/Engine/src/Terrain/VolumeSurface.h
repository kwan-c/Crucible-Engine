/*************************************************************************
* \file:         VolumeSurface.h
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          24 November 2026
* \version       1.0
* \brief
*                The surface of a TerrainVolume as triangles (marching
*                cubes): the one surface the renderer draws and physics
*                collides with.
*
* \details
*                One function, no state. TerrainMesh colours these
*                triangles; PhysicsShapes turns them into a Jolt mesh. Both
*                take them from here, so the cave you see and the cave you
*                stand in can't drift apart.
*
*                Each triangle is counter-clockwise seen from the side it
*                faces, and it faces the air (away from the rock): the
*                engine's front face, and Jolt's.
*
* \history
*    -v1.0       first version (the marching cubes from TerrainMesh v1.2)
*
**************************************************************************/
#pragma once

// STL
#include <vector>

// Crucible engine
#include "Math/Math.h"
#include "Math/TerrainVolume.h"

namespace Crucible {

    // One triangle of a volume's surface, in the volume's (the grid's) space.
    struct VolumeTriangle {

        Math::vec3 a, b, c;              // counter-clockwise seen from the air
        bool       underRock = false;    // rock somewhere straight above it: a cave floor if it faces up.
                                         // Only worked out for triangles that face up at all; false otherwise.

    };  // VOLUMETRIANGLE STRUCT

    /*!
        \brief
        Marching cubes over every cube of 8 samples of the volume.

        \return
        The triangles; none for an empty volume or one that is all rock or
        all air. Triangles with no area are left out.
    */
    std::vector<VolumeTriangle> BuildVolumeSurface(const TerrainVolume& volume);

}   // NAMESPACE CRUCIBLE
