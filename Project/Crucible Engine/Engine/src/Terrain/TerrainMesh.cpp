/*************************************************************************
* \file:         TerrainMesh.cpp
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       1.0
* \brief
*                BuildTerrainMesh: a TerrainGrid as flat-shaded triangles.
*                See the header for the layout rules.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "Terrain/TerrainMesh.h"

// STL
#include <cstddef>
#include <cstdint>

namespace {

    using namespace Crucible;

    // The same tangent Primitives::MakePlane gives its +Y faces, so a normal
    // map on a terrain material lies the same way as on a plane.
    const Math::vec4 kTangent(1.0f, 0.0f, 0.0f, -1.0f);

    // One triangle with its own vertices and face normal. UVs are in cells, so
    // a texture on the material repeats once per cell.
    void AddTriangle(MeshData& mesh, const HeightGrid& grid, const Math::vec3& colour,
                     std::uint32_t ax, std::uint32_t az, std::uint32_t bx, std::uint32_t bz, std::uint32_t cx, std::uint32_t cz) {

        const Math::vec3 a = grid.Corner(ax, az);
        const Math::vec3 b = grid.Corner(bx, bz);
        const Math::vec3 c = grid.Corner(cx, cz);
        const Math::vec3 normal = glm::normalize(glm::cross(b - a, c - a));   // up: the corners come counter-clockwise from above

        const auto add = [&](const Math::vec3& position, std::uint32_t x, std::uint32_t z) {
            Vertex vertex{};
            vertex.pos     = position;
            vertex.normal  = normal;
            vertex.uv      = Math::vec2(static_cast<float>(x), static_cast<float>(z));
            vertex.color   = colour;
            vertex.tangent = kTangent;
            mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
            mesh.vertices.push_back(vertex);
        };
        add(a, ax, az);
        add(b, bx, bz);
        add(c, cx, cz);

    }   // AddTriangle()

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    TerrainPalette PaletteOf(const Terrain& terrain) {

        TerrainPalette palette{};
        palette.colours[static_cast<int>(TerrainRegion::Ground)] = terrain.m_groundColour;
        palette.colours[static_cast<int>(TerrainRegion::Cliff)]  = terrain.m_cliffColour;
        palette.colours[static_cast<int>(TerrainRegion::Road)]   = terrain.m_roadColour;
        return palette;

    }   // PaletteOf()

    MeshData BuildTerrainMesh(const TerrainGrid& grid, const TerrainPalette& palette) {

        MeshData mesh;
        if (!grid.IsValid()) return mesh;

        const HeightGrid& heights = grid.heights;
        const std::size_t cells = static_cast<std::size_t>(heights.cols) * heights.rows;
        mesh.vertices.reserve(cells * 6);
        mesh.indices.reserve(cells * 6);

        for (std::uint32_t z = 0; z < heights.rows; ++z) {
            for (std::uint32_t x = 0; x < heights.cols; ++x) {

                const Math::vec3& colour = palette.colours[grid.regions[static_cast<std::size_t>(z) * heights.cols + x]];

                // Split along (x, z) -> (x + 1, z + 1), as Jolt's heightfield is.
                AddTriangle(mesh, heights, colour, x, z,  x, z + 1,      x + 1, z + 1);   // 00, 01, 11
                AddTriangle(mesh, heights, colour, x, z,  x + 1, z + 1,  x + 1, z);       // 00, 11, 10

            }
        }
        return mesh;

    }   // BuildTerrainMesh()

}   // NAMESPACE CRUCIBLE
