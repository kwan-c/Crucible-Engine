#pragma once
/*************************************************************************
* \file:         VineSystem.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          16 October 2026
* \version       1.0
* \brief
*                ECS system that runs every Vine component (proof of concept)
*                and draws its strands and leaves.
*
* \details
*                Per Vine entity it finds the target (the child at
*                Vine::targetChild) and its grip (from the child's world
*                transform and mesh bounds), advances a VineSimulation
*                (Graphics/Vines/VineSimulation.h), and hands the rebuilt
*                geometry to two of the Renderer's dynamic meshes - stem and
*                leaves - submitted in world space with an identity matrix.
*
*                Like ParticleSystem: the per-entity state lives here, keyed
*                by Entity::index and dropped when the entity goes away (its
*                two meshes go back to a pool, since meshes are never
*                destroyed); it runs in PreRender after
*                HierarchyTransformSystem, in Edit mode too, so vines preview
*                in the editor.
*
*                Cost: about 0.2 ms of CPU per vine per frame (3 strands,
*                leaves on) plus ~400 KB of vertex data written for the GPU.
*                Fine for a handful of golems; a horde would want the meshes
*                built on the worker threads, or on the GPU.
*
* \history
*    -v1.0       initial implementation (proof of concept)
**************************************************************************/

#include "ECS/Systems/IECSSystems.h"
#include "ECS/Core/ECSCore.h"
#include "ECS/Components/Components.h"
#include "ECS/Systems/Query.h"
#include "Graphics/RenderTypes.h"
#include "Graphics/Vines/VineSimulation.h"
#include "EngineExport.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Crucible {

    class Renderer;
    class ResourceManager;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL members in an exported class; Engine.dll and its clients share one toolchain
#endif

    class ENGINE_API VineSystem final : public IECSSystem
    {
    public:
        /*********************************************************************
         \param[in] renderer  The Core-owned Renderer (dynamic meshes, draws,
                              the built-in vine materials).
         \param[in] resources The Core-owned ResourceManager: resolves the
                              Vine's material assets and the target's mesh
                              asset (for its bounds). May be null.
         *********************************************************************/
        VineSystem(Renderer& renderer, ResourceManager* resources)
            : m_renderer(renderer), m_resources(resources) {}

        // Not copyable (it owns per-entity state through unique_ptrs). Spelled out because an
        // exported (dllexport) class gets every implicit member compiled, the copy constructor
        // included, which MSVC then fails to build.
        VineSystem(const VineSystem&) = delete;
        VineSystem& operator=(const VineSystem&) = delete;

        SystemStage Stage() const override { return SystemStage::PreRender; }
        int Priority() const override { return 0; }

        /*********************************************************************
         \brief  Reads WorldTransform + Vine (query), HierarchyNode (to find
                 the target), and the target's WorldTransform/MeshRenderer;
                 after HierarchyTransformSystem; main thread (Renderer calls).
         *********************************************************************/
        void Describe(SystemDesc& desc) const override;

        /*********************************************************************
         \brief  Advances every vine by the World's delta time and submits
                 its meshes.
         *********************************************************************/
        void Update(World& world, SystemContext& ctx) override;

    private:
        struct Record {
            std::uint32_t   generation = 0xFFFFFFFFu;   // of the entity this belongs to
            std::uint64_t   seenFrame = 0;
            bool            inUse = false;
            VineSimulation  simulation;
            MeshHandle      stemMesh;
            MeshHandle      leafMesh;
        };

        using VineQuery = Query<const WorldTransform, const Vine>;

        // The grip of `target` for these settings, or nullopt if its transform isn't ready.
        std::optional<VineGrip> GripOf(World& world, Entity target, const Vine& settings);
        void EnsureMaterials();

        Renderer&                            m_renderer;
        ResourceManager*                     m_resources = nullptr;
        std::optional<VineQuery>             m_query;
        std::vector<std::unique_ptr<Record>> m_records;      // indexed by Entity::index (pointers: VineSimulation is big)
        std::vector<std::uint32_t>           m_active;       // indices into m_records that are in use
        std::vector<MeshHandle>              m_freeMeshes;   // dynamic meshes of vines that went away
        MaterialHandle                       m_stemMaterial;
        MaterialHandle                       m_leafMaterial;
        std::uint64_t                        m_frame = 0;
        float                                m_time = 0.0f;

        // Rebuilt every vine, every frame (kept to reuse their memory).
        std::vector<Vertex>                  m_stemVertices;
        std::vector<uint32_t>                m_stemIndices;
        std::vector<Vertex>                  m_leafVertices;
        std::vector<uint32_t>                m_leafIndices;
    };

#ifdef _MSC_VER
#pragma warning(pop)
#endif

}   // NAMESPACE CRUCIBLE
