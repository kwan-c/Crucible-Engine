/*************************************************************************
* \file:         TerrainMesh.cpp
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       1.4
* \brief
*                BuildTerrainMesh: a TerrainGrid as flat-shaded triangles.
*                See the header for the layout rules.
*
* \history
*    -v1.0       first version
*    -v1.1       Tunnel colour in the palette
*    -v1.2       The cave volume: marching cubes in place of the cells
*                under it
*    -v1.3       the marching cubes moved to VolumeSurface.cpp, shared
*                with physics; this only colours its triangles
*    -v1.4       the floor slope is TerrainVolume::kFloorNormalY, shared
*                with navigation
*
**************************************************************************/
#include "Terrain/TerrainMesh.h"
#include "Terrain/VolumeSurface.h"

// STL
#include <cstddef>
#include <cstdint>

namespace {

    using namespace Crucible;

    // The same tangent Primitives::MakePlane gives its +Y faces, so a normal
    // map on a terrain material lies the same way as on a plane.
    const Math::vec4 kTangent(1.0f, 0.0f, 0.0f, -1.0f);

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

    // The volume's surface (VolumeSurface.h), coloured by facing: ground up
    // top, cave floor (Tunnel) where rock is above, rock on anything steeper.
    void AddVolume(MeshData& mesh, const TerrainGrid& grid, const TerrainPalette& palette) {

        const float       cell   = grid.heights.cellSize;
        const Math::vec3& ground = palette.colours[static_cast<int>(TerrainRegion::Ground)];
        const Math::vec3& rock   = palette.colours[static_cast<int>(TerrainRegion::Cliff)];
        const Math::vec3& floor  = palette.colours[static_cast<int>(TerrainRegion::Tunnel)];
        const auto uv = [cell](const Math::vec3& p) { return Math::vec2(p.x / cell, p.z / cell); };

        for (const VolumeTriangle& t : BuildVolumeSurface(grid.volume)) {
            const float up = glm::normalize(glm::cross(t.b - t.a, t.c - t.a)).y;
            const Math::vec3& colour = up < TerrainVolume::kFloorNormalY ? rock : t.underRock ? floor : ground;
            AddTriangle(mesh, colour, t.a, uv(t.a), t.b, uv(t.b), t.c, uv(t.c));
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
