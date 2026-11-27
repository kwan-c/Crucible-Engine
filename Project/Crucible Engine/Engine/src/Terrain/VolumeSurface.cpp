/*************************************************************************
* \file:         VolumeSurface.cpp
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          24 November 2026
* \version       1.0
* \brief
*                BuildVolumeSurface: marching cubes over a TerrainVolume.
*                See the header.
*
* \history
*    -v1.0       first version (the marching cubes from TerrainMesh v1.2)
*
**************************************************************************/
#include "Terrain/VolumeSurface.h"
#include "Terrain/MarchingCubesTables.h"

// STL
#include <cstdint>
#include <utility>   // std::swap

namespace {

    using namespace Crucible;

    // Is there rock above sample (x, y, z)?
    bool RockAbove(const TerrainVolume& volume, std::uint32_t x, std::uint32_t y, std::uint32_t z) {
        for (std::uint32_t up = y + 1; up < volume.countY; ++up)
            if (volume.At(x, up, z) > 0.0f) return true;
        return false;
    }

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    // A corner is "in" when it is air (below 0); the surface crosses an edge
    // where its two corners differ, at the point the density reaches 0 along
    // it. Each triangle is turned to face away from the rock (against the
    // density's gradient), so the tables' winding is never relied on.
    std::vector<VolumeTriangle> BuildVolumeSurface(const TerrainVolume& v) {

        std::vector<VolumeTriangle> triangles;
        if (v.IsEmpty()) return triangles;

        const float size = v.voxelSize;
        using namespace MarchingCubes;

        for (std::uint32_t y = 0; y + 1 < v.countY; ++y)
            for (std::uint32_t z = 0; z + 1 < v.countZ; ++z)
                for (std::uint32_t x = 0; x + 1 < v.countX; ++x) {

                    float d[8];
                    unsigned index = 0;
                    for (int c = 0; c < 8; ++c) {
                        d[c] = v.At(x + kCorner[c][0], y + kCorner[c][1], z + kCorner[c][2]);
                        if (d[c] < 0.0f) index |= 1u << c;
                    }
                    if (index == 0 || index == 255) continue;   // all rock or all air: no surface here

                    // Where the surface crosses each edge it crosses.
                    Math::vec3 point[12];
                    const std::uint16_t mask = kEdgeMask[index];
                    for (int e = 0; e < 12; ++e) {
                        if (!(mask & (1u << e))) continue;
                        const int a = kEdge[e][0], b = kEdge[e][1];
                        const float t = d[a] / (d[a] - d[b]);   // the corners differ in sign, so never 0 / 0
                        const Math::vec3 ca(kCorner[a][0], kCorner[a][1], kCorner[a][2]);
                        const Math::vec3 cb(kCorner[b][0], kCorner[b][1], kCorner[b][2]);
                        point[e] = v.origin + (Math::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) + ca + (cb - ca) * t) * size;
                    }

                    // Towards the rock: the density's slope across the cube.
                    Math::vec3 towardRock(0.0f);
                    for (int c = 0; c < 8; ++c)
                        towardRock += Math::vec3(kCorner[c][0] * 2 - 1, kCorner[c][1] * 2 - 1, kCorner[c][2] * 2 - 1) * d[c];

                    for (int k = 0; kTriangles[index][k] >= 0; k += 3) {
                        VolumeTriangle triangle;
                        triangle.a = point[kTriangles[index][k]];
                        triangle.b = point[kTriangles[index][k + 1]];
                        triangle.c = point[kTriangles[index][k + 2]];
                        Math::vec3 n = glm::cross(triangle.b - triangle.a, triangle.c - triangle.a);
                        if (glm::dot(n, n) < 1e-12f) continue;                  // a sliver with no area
                        if (glm::dot(n, towardRock) > 0.0f) { std::swap(triangle.b, triangle.c); n = -n; }

                        triangle.underRock = n.y > 0.0f && RockAbove(v, x, y + 1, z);
                        triangles.push_back(triangle);
                    }
                }

        return triangles;

    }   // BuildVolumeSurface()

}   // NAMESPACE CRUCIBLE
