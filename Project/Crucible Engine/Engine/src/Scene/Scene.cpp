/*************************************************************************
  \file       Scene.cpp
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       29 September 2026
  \version    1.0
  \brief
    Scene save/load implementation.
  \details
    Save walks every archetype, asks EntityIO to write each entity's
    components, and sorts the result by guid so the file is stable.
    Load validates the whole file first, then clears the world and rebuilds
    it entity by entity.
  \dependencies
    Scene.h, EntityIO.h, ECS/World.h, ECS/Archetype/ArchetypeRegistry.h,
    Json/JsonWrapper.h, AssetPipe/AssetRegistry.h, Diagnostics/Logger.h
  \see
    Scene.h, EntityIO.cpp, Prefab.cpp
  \history
    - 1.0 (29 September 2026): Initial version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#include "Scene/Scene.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "AssetPipe/AssetRegistry.h"   // GuidToString / GuidFromString
#include "Diagnostics/Logger.h"
#include "ECS/Archetype/Archetype.h"
#include "ECS/Archetype/ArchetypeRegistry.h"
#include "ECS/World.h"
#include "Json/JsonWrapper.h"
#include "Scene/EntityIO.h"

namespace fs = std::filesystem;

namespace Crucible {

    // =======================================================================
    //  Save
    // =======================================================================

    bool Scene::Save(World& world, const fs::path& file)
    {
        JsonWrapper doc;
        doc.Set("version", kSceneVersion);
        doc.Set("name", m_name);

        // --- Gather every live entity, with the guid it should keep -------
        //
        // Building a fresh map rather than editing m_ids in place means
        // entities destroyed since the last save drop out on their own,
        // instead of the map growing for the life of the editor session.
        std::unordered_map<Entity, xresource::instance_guid, EntityHash> fresh;
        std::vector<std::pair<xresource::instance_guid, Entity>> ordered;

        for (const Archetype* archetype : world.Archetypes().All())
        {
            if (archetype == nullptr)
            {
                continue;
            }

            for (const Entity entity : archetype->Entities())
            {
                if (!world.IsAlive(entity))
                {
                    continue;
                }

                // Keep the guid this entity already had; only a genuinely
                // new entity gets a new one. This is what makes the file
                // stable across saves.
                const auto known = m_ids.find(entity);
                const xresource::instance_guid id =
                    (known != m_ids.end())
                        ? known->second
                        : xresource::instance_guid::GenerateGUIDCopy();

                fresh.emplace(entity, id);
                ordered.emplace_back(id, entity);
            }
        }

        m_ids.swap(fresh);

        // Archetype order shifts whenever a component is added or removed,
        // so it cannot be the file's order. Sorting by guid gives an order
        // that only changes when entities do.
        std::sort(ordered.begin(), ordered.end(),
                  [](const auto& lhs, const auto& rhs)
                  {
                      return lhs.first.m_Value < rhs.first.m_Value;
                  });

        // --- Write them out -----------------------------------------------
        doc.EnsureArray("entities");

        std::size_t parented = 0;

        for (const auto& [id, entity] : ordered)
        {
            const std::size_t index = doc.PushObject("entities");
            const std::string base  = "entities/" + std::to_string(index);

            doc.Set(base + "/id", AssetRegistry::GuidToString(id));

            // The tree is stored as a guid reference, never as the raw
            // HierarchyNode links - those are Entity handles, which are slot
            // indices that mean nothing after a reload. The parent is always
            // alive and therefore always in m_ids.
            const Entity parent = world.GetParent(entity);
            if (parent.IsValid())
            {
                const auto parentId = m_ids.find(parent);
                if (parentId != m_ids.end())
                {
                    doc.Set(base + "/parent", AssetRegistry::GuidToString(parentId->second));

                    // Entities are written in guid order, not tree order, so
                    // the position among siblings has to be recorded
                    // explicitly or the Hierarchy panel order would shuffle.
                    doc.Set(base + "/siblingIndex",
                            static_cast<int>(world.GetSiblingIndex(entity)));
                    ++parented;
                }
            }

            EntityIO::WriteComponents(world, entity, doc, base + "/components");
        }

        if (!doc.SaveToFile(file))
        {
            return false;
        }

        CRU_INFO("Scene: saved '{}' ({} entities, {} parented) to {}",
                 m_name, ordered.size(), parented, file.string());
        return true;
    }

    // =======================================================================
    //  Load
    // =======================================================================

    bool Scene::Load(World& world, const fs::path& file)
    {
        // Everything that can fail happens before the world is touched, so
        // a bad file costs nothing.
        JsonWrapper doc;
        if (!doc.ReadFromFile(file))
        {
            return false;   // ReadFromFile already logged why
        }

        const int version = doc.Get<int>("version", 0);
        if (version > kSceneVersion)
        {
            CRU_ERROR("Scene: '{}' is version {}, this build understands {} - "
                      "not loading it",
                      file.string(), version, kSceneVersion);
            return false;
        }
        if (version == 0)
        {
            CRU_WARN("Scene: '{}' has no version field - reading it as {}",
                     file.string(), kSceneVersion);
        }

        // --- Past this line the world is being replaced -------------------
        world.DestroyAllEntities();
        m_ids.clear();

        m_name = doc.Get<std::string>("name", file.stem().string());

        const std::size_t count = doc.GetListSize("entities");
        std::size_t components = 0;

        // A parent can appear later in the file than its child (the file is
        // in guid order, not tree order), so every entity has to exist
        // before any link can be made. Links are collected here and applied
        // in a second pass.
        struct PendingLink
        {
            Entity        child;
            std::uint64_t parentId;
            std::uint32_t siblingIndex;
        };
        std::vector<PendingLink> links;
        std::unordered_map<std::uint64_t, Entity> byId;
        byId.reserve(count);

        // --- Pass 1: create every entity and its components ---------------
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::string base = "entities/" + std::to_string(i);

            const Entity entity = world.CreateEntity();

            // A hand-written scene file may leave the id out; give it one
            // so the next save is stable anyway.
            const std::string idText = doc.Get<std::string>(base + "/id", std::string{});
            const xresource::instance_guid id =
                idText.empty()
                    ? xresource::instance_guid::GenerateGUIDCopy()
                    : AssetRegistry::GuidFromString(idText);

            m_ids.emplace(entity, id);
            byId.emplace(id.m_Value, entity);

            components += EntityIO::ReadComponents(world, entity, doc,
                                                   base + "/components");

            const std::string parentText = doc.Get<std::string>(base + "/parent", std::string{});
            if (!parentText.empty())
            {
                links.push_back({ entity,
                                  AssetRegistry::GuidFromString(parentText).m_Value,
                                  static_cast<std::uint32_t>(doc.Get<int>(base + "/siblingIndex", 0)) });
            }
        }

        // --- Pass 2: rebuild the tree -------------------------------------
        //
        // TrySetParent appends the child as its parent's LAST child. Applying
        // links sorted by (parent, siblingIndex) therefore reproduces the
        // saved sibling order with no extra SetSiblingIndex calls.
        std::sort(links.begin(), links.end(),
                  [](const PendingLink& lhs, const PendingLink& rhs)
                  {
                      if (lhs.parentId != rhs.parentId) return lhs.parentId < rhs.parentId;
                      return lhs.siblingIndex < rhs.siblingIndex;
                  });

        std::size_t parented = 0;
        for (const PendingLink& link : links)
        {
            const auto parent = byId.find(link.parentId);
            if (parent == byId.end())
            {
                // The parent is not in this file - a hand edit, or a merge
                // that dropped it. Keep the child as a root rather than fail.
                CRU_WARN("Scene: an entity's parent is missing from '{}' - left as a root",
                         file.string());
                continue;
            }

            // worldPositionStays = false: the file holds each entity's LOCAL
            // Transform, which must be kept as-is. With true, SetParent would
            // recompute it to preserve the world position the entity had as
            // a root - which is not where it was saved.
            const ParentResult result = world.TrySetParent(link.child, parent->second, false);
            if (result == ParentResult::Ok)
            {
                ++parented;
            }
            else
            {
                // A cycle can only come from a corrupted or hand-edited file.
                CRU_WARN("Scene: could not restore a parent link in '{}' ({}) - left as a root",
                         file.string(), ToString(result));
            }
        }

        CRU_INFO("Scene: loaded '{}' ({} entities, {} components, {} parented) from {}",
                 m_name, count, components, parented, file.string());
        return true;
    }

    // =======================================================================
    //  Identity
    // =======================================================================

    void Scene::Forget()
    {
        m_ids.clear();
    }

    xresource::instance_guid Scene::IdOf(Entity entity) const
    {
        const auto found = m_ids.find(entity);
        return (found == m_ids.end()) ? xresource::instance_guid{} : found->second;
    }

}
