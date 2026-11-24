/*************************************************************************
* \file:         ECSTerrainSystem.cpp
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       1.0
* \brief
*                Implementation of ECSTerrainSystem. See the header for
*                what happens each frame.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "ECS/ECSTerrainSystem.h"

// STL
#include <string>
#include <vector>

// Crucible engine
#include "Diagnostics/Logger.h"
#include "ECS/World.h"
#include "Graphics/RenderExtractSystem.h"
#include "Graphics/RenderSystem.h"
#include "Terrain/TerrainMesh.h"

namespace {

    using namespace Crucible;

    bool SameColour(const Math::vec3& a, const Math::vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

    bool SameColours(const Math::vec3 (&recorded)[3], const Terrain& terrain) {
        return SameColour(recorded[0], terrain.m_groundColour) && SameColour(recorded[1], terrain.m_cliffColour)
            && SameColour(recorded[2], terrain.m_roadColour);
    }

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    void ECSTerrainSystem::Describe(SystemDesc& desc) const {

        desc.Uses<TerrainQuery>()                  // reads Terrain, writes MeshRenderer::mesh
            .RunBefore<RenderExtractSystem>()      // which collects this frame's draws from MeshRenderer
            .Affinity(SystemAffinity::MainThread); // creates and frees Renderer meshes

    }   // ECSTerrainSystem::Describe()

    void ECSTerrainSystem::Update(World& world, [[maybe_unused]] SystemContext& ctx) {

        if (!m_query) m_query.emplace(world.CreateQuery<const Terrain, MeshRenderer>());
        ++m_frame;

        m_query->ForEach([&](Entity entity, const Terrain& terrain, MeshRenderer& renderer) {

            // A reused index with a new generation is a different entity: start clean.
            Record& record = m_records[entity.index];
            if (record.seenFrame != 0 && record.generation != entity.generation) {
                Free(record);
                record = Record{};
            }
            record.generation = entity.generation;
            record.seenFrame  = m_frame;

            if (record.grid != terrain.m_grid || !SameColours(record.colours, terrain)) Rebuild(record, terrain);

            // Every frame, not only on a rebuild: anything that copies the
            // MeshRenderer (a scene reload, an undo) brings back an old handle.
            renderer.mesh = record.mesh;

            if ((renderer.meshAsset != 0 || renderer.modelAsset != 0) && !record.warnedAsset) {
                record.warnedAsset = true;
                CRU_WARN("Terrain: entity {} has a mesh or model on its MeshRenderer, which is drawn instead of the terrain - clear it",
                         entity.index);
            }
        });

        // Terrains gone since last frame (entity destroyed, Terrain removed): free their meshes.
        for (auto it = m_records.begin(); it != m_records.end();) {
            if (it->second.seenFrame == m_frame) { ++it; continue; }
            Free(it->second);
            it = m_records.erase(it);
        }

    }   // ECSTerrainSystem::Update()

    void ECSTerrainSystem::Shutdown() noexcept {

        for (auto& [index, record] : m_records) Free(record);
        m_records.clear();
        m_query.reset();

    }   // ECSTerrainSystem::Shutdown()

    // A new mesh from the terrain as it is now, replacing the old one. No grid
    // (nothing built yet, or the session that built it ended) is no mesh.
    void ECSTerrainSystem::Rebuild(Record& record, const Terrain& terrain) {

        Free(record);
        record.grid       = terrain.m_grid;
        record.colours[0] = terrain.m_groundColour;
        record.colours[1] = terrain.m_cliffColour;
        record.colours[2] = terrain.m_roadColour;

        if (record.grid == nullptr) return;

        const MeshData mesh = BuildTerrainMesh(*record.grid, PaletteOf(terrain));
        if (mesh.vertices.empty()) return;
        record.mesh = m_renderer.CreateMesh(mesh, "Terrain");

    }   // ECSTerrainSystem::Rebuild()

    // The GPU memory goes a few frames later, once no frame in flight uses it
    // (Renderer::DestroyMesh), so this is safe mid-frame.
    void ECSTerrainSystem::Free(Record& record) {

        if (record.mesh.IsValid()) m_renderer.DestroyMesh(record.mesh);
        record.mesh = MeshHandle{};

    }   // ECSTerrainSystem::Free()

}   // NAMESPACE CRUCIBLE
