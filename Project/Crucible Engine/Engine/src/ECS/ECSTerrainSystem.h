/*************************************************************************
* \file:         ECSTerrainSystem.h
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          10 November 2026
* \version       1.0
* \brief
*                Draws every Terrain: keeps the mesh on its MeshRenderer in
*                step with the ground a script last built.
*
* \details
*                Each frame, per Terrain entity: if its grid or colours
*                changed since the mesh was made, build a new mesh
*                (Terrain/TerrainMesh.h), hand it to the MeshRenderer, and
*                free the old one. Otherwise nothing but a pointer compare,
*                so a built terrain costs nothing to keep.
*
*                The mesh goes into MeshRenderer::mesh with no mesh asset:
*                the path RenderExtractSystem documents for runtime meshes.
*                A mesh or model asset on the same MeshRenderer would win
*                over it, so that is warned about once.
*
*                Collision is not done here: ECSPhysicsSystem builds the
*                heightfield from the same grid (Collider set to
*                HeightField). Runs in Edit too, so a terrain built by an
*                editor tool previews; a grid only lives for the session
*                it was built in.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

// Crucible engine
#include "ECS/Components/Components.h"   // MeshRenderer, Terrain
#include "ECS/Systems/IECSSystems.h"
#include "ECS/Systems/Query.h"
#include "EngineExport.h"
#include "Graphics/MeshHandle.h"
#include "Math/Math.h"

namespace Crucible {

    class Renderer;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL members in an exported class; Engine.dll and its clients share one toolchain
#endif

    class ENGINE_API ECSTerrainSystem final : public IECSSystem {

    public:

        explicit ECSTerrainSystem(Renderer& renderer) : m_renderer(renderer) {}

        // Spelled out: an exported class gets every implicit member compiled,
        // and it owns meshes, so a copy would free them twice.
        ECSTerrainSystem(const ECSTerrainSystem&)            = delete;
        ECSTerrainSystem& operator=(const ECSTerrainSystem&) = delete;

        // --- IECSSystem ---------------------------------------------------- //

        // Before RenderExtractSystem in PreRender: the mesh handle must be on the
        // MeshRenderer before this frame's draws are collected.
        SystemStage Stage() const override { return SystemStage::PreRender; }

        void Describe(SystemDesc& desc) const override;
        void Update(World& world, SystemContext& ctx) override;
        void Shutdown() noexcept override;   // frees every terrain mesh

    private:

        struct Record {
            std::uint32_t                      generation = 0;    // of the entity this belongs to
            std::uint64_t                      seenFrame  = 0;
            std::shared_ptr<const TerrainGrid> grid;              // what the mesh was built from
            Math::vec3                         colours[3]{};      // and with (ground, cliff, road)
            MeshHandle                         mesh;
            bool                               warnedAsset = false;
        };

        using TerrainQuery = Query<const Terrain, MeshRenderer>;

        void Rebuild(Record& record, const Terrain& terrain);
        void Free(Record& record);

        Renderer&                                  m_renderer;
        std::optional<TerrainQuery>                m_query;
        std::unordered_map<std::uint32_t, Record>  m_records;   // by Entity::index
        std::uint64_t                              m_frame = 0;

    };  // ECSTERRAINSYSTEM CLASS

#ifdef _MSC_VER
#pragma warning(pop)
#endif

}   // NAMESPACE CRUCIBLE
