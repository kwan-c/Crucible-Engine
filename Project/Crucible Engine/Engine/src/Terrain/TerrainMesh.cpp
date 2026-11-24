/*************************************************************************
* \file:         TerrainMesh.cpp
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       1.2
* \brief
*                BuildTerrainMesh: a TerrainGrid as flat-shaded triangles.
*                See the header for the layout rules.
*
* \history
*    -v1.0       first version
*    -v1.1       Tunnel colour in the palette
*    -v1.2       The cave volume: marching cubes in place of the cells
*                under it
*
**************************************************************************/
#include "Terrain/TerrainMesh.h"
#include "Terrain/MarchingCubesTables.h"

// STL
#include <cstddef>
#include <cstdint>
#include <utility>   // std::swap

namespace {

    using namespace Crucible;

    // The same tangent Primitives::MakePlane gives its +Y faces, so a normal
    // map on a terrain material lies the same way as on a plane.
    const Math::vec4 kTangent(1.0f, 0.0f, 0.0f, -1.0f);

    // A volume triangle whose normal points up at least this much is ground
    // (about the 35 degree walkable slope); anything steeper is rock.
    constexpr float kUpFacing = 0.82f;

    // One triangle with its own vertices and face normal (from its winding:
    // counter-clockwise seen from the side it faces). UVs are in cells, so a
    // texture on the material repeats once per cell.
    void AddTriangle(MeshData& mesh, const Math::vec3& colour,
                     const Math::vec3& a, const Math::vec2& uvA,
                     const Math::vec3& b, const Math::vec2& uvB,
                     const Math::vec3& c, const Math::vec2& uvC) {

        const Math::vec3 normal = glm::normalize(glm::cross(b - a, c - a));

        const auto add = [&](const Math::vec3& position, const Math::vec2& uv) {
            Vertex vertex{};
            vertex.pos     = position;
            vertex.normal  = normal;
            vertex.uv      = uv;
            vertex.color   = colour;
            vertex.tangent = kTangent;
            mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
            mesh.vertices.push_back(vertex);
        };
        add(a, uvA);
        add(b, uvB);
        add(c, uvC);

    }   // AddTriangle()

    // A ground triangle from three grid corners: up, as the corners come
    // counter-clockwise from above.
    void AddTriangle(MeshData& mesh, const HeightGrid& grid, const Math::vec3& colour,
                     std::uint32_t ax, std::uint32_t az, std::uint32_t bx, std::uint32_t bz, std::uint32_t cx, std::uint32_t cz) {

        const auto uv = [](std::uint32_t x, std::uint32_t z) { return Math::vec2(static_cast<float>(x), static_cast<float>(z)); };
        AddTriangle(mesh, colour, grid.Corner(ax, az), uv(ax, az), grid.Corner(bx, bz), uv(bx, bz), grid.Corner(cx, cz), uv(cx, cz));

    }   // AddTriangle()

    // Is there rock above sample (x, y, z)? Up-facing ground with rock above is a cave floor.
    bool RockAbove(const TerrainVolume& volume, std::uint32_t x, std::uint32_t y, std::uint32_t z) {
        for (std::uint32_t up = y + 1; up < volume.countY; ++up)
            if (volume.At(x, up, z) > 0.0f) return true;
        return false;
    }

    // The volume as flat-shaded triangles: marching cubes over every cube of 8
    // samples. A corner is "in" when it is air (below 0); the surface crosses
    // an edge where its two corners differ, at the point the density reaches 0
    // along it. Each triangle is turned to face away from the rock (against the
    // density's gradient), so the tables' winding is never relied on. Colour by
    // facing: ground up top, cave floor (Tunnel) where rock is above, rock on
    // anything steeper.
    void AddVolume(MeshData& mesh, const TerrainGrid& grid, const TerrainPalette& palette) {

        const TerrainVolume& v    = grid.volume;
        const float          size = v.voxelSize;
        const float          cell = grid.heights.cellSize;
        const Math::vec3& ground = palette.colours[static_cast<int>(TerrainRegion::Ground)];
        const Math::vec3& rock   = palette.colours[static_cast<int>(TerrainRegion::Cliff)];
        const Math::vec3& floor  = palette.colours[static_cast<int>(TerrainRegion::Tunnel)];
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
                        Math::vec3 a = point[kTriangles[index][k]], b = point[kTriangles[index][k + 1]], c = point[kTriangles[index][k + 2]];
                        Math::vec3 n = glm::cross(b - a, c - a);
                        if (glm::dot(n, n) < 1e-12f) continue;                  // a sliver with no area
                        if (glm::dot(n, towardRock) > 0.0f) { std::swap(b, c); n = -n; }

                        const float up = glm::normalize(n).y;
                        const Math::vec3& colour = up < kUpFacing ? rock : RockAbove(v, x, y + 1, z) ? floor : ground;
                        const auto uv = [cell](const Math::vec3& p) { return Math::vec2(p.x / cell, p.z / cell); };
                        AddTriangle(mesh, colour, a, uv(a), b, uv(b), c, uv(c));
                    }
                }

    }   // AddVolume()

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    TerrainPalette PaletteOf(const Terrain& terrain) {

        TerrainPalette palette{};
        palette.colours[static_cast<int>(TerrainRegion::Ground)] = terrain.m_groundColour;
        palette.colours[static_cast<int>(TerrainRegion::Cliff)]  = terrain.m_cliffColour;
        palette.colours[static_cast<int>(TerrainRegion::Road)]   = terrain.m_roadColour;
        palette.colours[static_cast<int>(TerrainRegion::Tunnel)] = terrain.m_tunnelColour;
        return palette;

    }   // PaletteOf()

    MeshData BuildTerrainMesh(const TerrainGrid& grid, const TerrainPalette& palette) {

        MeshData mesh;
        if (!grid.IsValid()) return mesh;

        const HeightGrid& heights = grid.heights;
        const std::size_t cells = static_cast<std::size_t>(heights.cols) * heights.rows;
        mesh.vertices.reserve(cells * 6);
        mesh.indices.reserve(cells * 6);

        std::uint32_t coveredX0 = 0, coveredX1 = 0, coveredZ0 = 0, coveredZ1 = 0;   // cells the volume replaces
        if (!grid.volume.IsEmpty()) grid.volume.Footprint(heights, coveredX0, coveredX1, coveredZ0, coveredZ1);

        for (std::uint32_t z = 0; z < heights.rows; ++z) {
            for (std::uint32_t x = 0; x < heights.cols; ++x) {

                if (x >= coveredX0 && x < coveredX1 && z >= coveredZ0 && z < coveredZ1) continue;

                const Math::vec3& colour = palette.colours[grid.regions[static_cast<std::size_t>(z) * heights.cols + x]];

                // Split along (x, z) -> (x + 1, z + 1), as Jolt's heightfield is.
                AddTriangle(mesh, heights, colour, x, z,  x, z + 1,      x + 1, z + 1);   // 00, 01, 11
                AddTriangle(mesh, heights, colour, x, z,  x + 1, z + 1,  x + 1, z);       // 00, 11, 10

            }
        }
        if (!grid.volume.IsEmpty()) AddVolume(mesh, grid, palette);
        return mesh;

    }   // BuildTerrainMesh()

}   // NAMESPACE CRUCIBLE
