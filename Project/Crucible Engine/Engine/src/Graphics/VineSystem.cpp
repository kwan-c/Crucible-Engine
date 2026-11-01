/*************************************************************************
* \file:         VineSystem.cpp
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          16 October 2026
* \version       1.0
* \brief
*                See VineSystem.h.
*
* \history
*    -v1.0       initial implementation (proof of concept)
**************************************************************************/

#include "Graphics/VineSystem.h"

#include <algorithm>
#include <span>

#include "ECS/World.h"
#include "ECS/HierarchyTransformSystem.h"
#include "Graphics/RenderSystem.h"
#include "Resources/ResourceManager.h"

namespace Crucible {

    namespace {

        // Targets are told apart by their entity (index + generation): never 0 for a real entity.
        std::uint64_t KeyOf(Entity e) { return (static_cast<std::uint64_t>(e.generation) << 32) | e.index; }
        Entity EntityOf(std::uint64_t key) { return Entity{ static_cast<std::uint32_t>(key & 0xFFFFFFFFu), static_cast<std::uint32_t>(key >> 32) }; }

        // A target with no mesh (or one still loading) is treated as a 1 m rod, 6 cm thick.
        const glm::vec3 kRodMin{ -0.03f, -0.5f, -0.03f };
        const glm::vec3 kRodMax{  0.03f,  0.5f,  0.03f };

    }   // anonymous namespace

    void VineSystem::Describe(SystemDesc& desc) const {

        desc.Uses<VineQuery>()                                   // WorldTransform + Vine of the vine entity
            .Reads<HierarchyNode, WorldTransform, MeshRenderer>() // its children; the target's transform and mesh
            .RunAfter<HierarchyTransformSystem>()                // WorldTransform must already be this frame's
            .Affinity(SystemAffinity::MainThread);               // touches the Renderer/ResourceManager

    }   // VineSystem::Describe()

    void VineSystem::EnsureMaterials() {

        if (m_stemMaterial.IsValid()) { return; }

        // Plain, non-metallic, fully rough and white: what the strands look like comes from their
        // vertex colours (Vine::rootColour...), with as little shine as the lighting allows. A
        // Material asset on the Vine replaces these.
        MaterialData stem;
        stem.factors.metallicFactor  = 0.0f;
        stem.factors.roughnessFactor = 1.0f;
        m_stemMaterial = m_renderer.CreateMaterial(stem);

        MaterialData leaf;
        leaf.factors.metallicFactor  = 0.0f;
        leaf.factors.roughnessFactor = 1.0f;
        m_leafMaterial = m_renderer.CreateMaterial(leaf);

    }   // VineSystem::EnsureMaterials()

    std::optional<VineGrip> VineSystem::GripOf(World& world, Entity target, const Vine& settings) {

        const WorldTransform* transform = world.Read<WorldTransform>(target);
        if (!transform || !transform->valid) { return std::nullopt; }

        glm::vec3 boundsMin = kRodMin, boundsMax = kRodMax;
        if (const MeshRenderer* renderer = world.Read<MeshRenderer>(target)) {
            MeshHandle mesh = renderer->mesh;
            if (renderer->meshAsset != 0 && m_resources) { mesh = m_resources->GetMesh(renderer->meshAsset); }
            glm::vec3 mn, mx;
            if (mesh.IsValid() && m_renderer.GetMeshBounds(mesh, mn, mx)) { boundsMin = mn; boundsMax = mx; }
        }
        return MakeVineGrip(transform->matrix, boundsMin, boundsMax, settings);

    }   // VineSystem::GripOf()

    void VineSystem::Update(World& world, [[maybe_unused]] SystemContext& ctx) {

        if (!m_query) { m_query.emplace(world.CreateQuery<const WorldTransform, const Vine>()); }
        EnsureMaterials();

        ++m_frame;
        const float dt = std::max(world.DeltaTime(), 0.0f);
        m_time += dt;

        m_query->ForEach([&](Entity entity, const WorldTransform& worldTransform, const Vine& settings) {

            if (!worldTransform.valid) { return; }

            // Find (or start) this entity's state. A reused slot with a new generation is a
            // different entity: start clean (keeping the meshes).
            if (entity.index >= m_records.size()) { m_records.resize(static_cast<size_t>(entity.index) + 1); }
            std::unique_ptr<Record>& slot = m_records[entity.index];
            if (!slot) { slot = std::make_unique<Record>(); }
            Record& record = *slot;
            if (!record.inUse || record.generation != entity.generation) {
                if (!record.inUse) { m_active.push_back(entity.index); }
                const MeshHandle stem = record.stemMesh, leaf = record.leafMesh;
                record = Record{};
                record.inUse      = true;
                record.generation = entity.generation;
                record.stemMesh   = stem;
                record.leafMesh   = leaf;
            }
            record.seenFrame = m_frame;
            if (!record.stemMesh.IsValid()) {
                if (m_freeMeshes.size() >= 2) {
                    record.stemMesh = m_freeMeshes.back(); m_freeMeshes.pop_back();
                    record.leafMesh = m_freeMeshes.back(); m_freeMeshes.pop_back();
                } else {
                    record.stemMesh = m_renderer.CreateDynamicMesh("Vine stems");
                    record.leafMesh = m_renderer.CreateDynamicMesh("Vine leaves");
                }
            }

            // What it should hold (the chosen child, if attached) and what it holds now.
            VineSimulation::Inputs in;
            in.root = worldTransform.matrix;
            if (settings.attached && settings.targetChild >= 0) {
                const Entity child = world.GetChild(entity, static_cast<std::uint32_t>(settings.targetChild));
                if (child.IsValid()) {
                    if (std::optional<VineGrip> grip = GripOf(world, child, settings)) {
                        in.desiredKey = KeyOf(child);
                        in.desired    = *grip;
                    }
                }
            }
            if (const std::uint64_t held = record.simulation.BoundKey(); held != 0) {
                if (held == in.desiredKey) {
                    in.boundAlive = true; in.boundGripValid = true; in.bound = in.desired;
                } else if (const Entity target = EntityOf(held); world.IsAlive(target)) {
                    in.boundAlive = true;
                    if (std::optional<VineGrip> grip = GripOf(world, target, settings)) { in.boundGripValid = true; in.bound = *grip; }
                }
            }

            record.simulation.Update(settings, in, dt, m_time);

            m_stemVertices.clear(); m_stemIndices.clear();
            m_leafVertices.clear(); m_leafIndices.clear();
            record.simulation.BuildMeshes(settings, m_time, m_stemVertices, m_stemIndices, m_leafVertices, m_leafIndices);
            m_renderer.UpdateDynamicMesh(record.stemMesh, m_stemVertices, m_stemIndices);
            m_renderer.UpdateDynamicMesh(record.leafMesh, m_leafVertices, m_leafIndices);

            // Material assets win when set; otherwise the plain built-in ones.
            MaterialHandle stemMaterial = m_stemMaterial, leafMaterial = m_leafMaterial;
            if (m_resources) {
                if (settings.materialAsset != 0) {
                    if (const MaterialHandle m = m_resources->GetMaterial(settings.materialAsset); m.IsValid()) { stemMaterial = m; }
                }
                if (settings.leafMaterialAsset != 0) {
                    if (const MaterialHandle m = m_resources->GetMaterial(settings.leafMaterialAsset); m.IsValid()) { leafMaterial = m; }
                }
            }
            const glm::mat4 identity(1.0f);   // the vertices are already in world space
            if (!m_stemIndices.empty()) { m_renderer.Submit(record.stemMesh, stemMaterial, identity); }
            if (!m_leafIndices.empty()) { m_renderer.Submit(record.leafMesh, leafMaterial, identity); }
        });

        // Vines whose entity is gone (destroyed, or lost its component): free the state, keep
        // the meshes for the next vine (emptied, so nothing stale is ever drawn).
        for (size_t i = m_active.size(); i-- > 0;) {
            std::unique_ptr<Record>& slot = m_records[m_active[i]];
            if (slot->seenFrame == m_frame) { continue; }
            if (slot->stemMesh.IsValid()) {
                m_renderer.UpdateDynamicMesh(slot->stemMesh, {}, {});
                m_renderer.UpdateDynamicMesh(slot->leafMesh, {}, {});
                m_freeMeshes.push_back(slot->leafMesh);
                m_freeMeshes.push_back(slot->stemMesh);
            }
            slot.reset();
            m_active[i] = m_active.back();
            m_active.pop_back();
        }

    }   // VineSystem::Update()

}   // NAMESPACE CRUCIBLE
