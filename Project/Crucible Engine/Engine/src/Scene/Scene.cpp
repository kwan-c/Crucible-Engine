/*************************************************************************
  \file       Scene.cpp
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       29 September 2026
  \version    1.1
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
    - 1.1 (2 October 2026): In-memory snapshots for play mode
      (CaptureSnapshot / RestoreSnapshot) and EntityOf, in their own
      section. Save and Load are unchanged.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#include "Scene/Scene.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <unordered_set>
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

    bool Scene::Load(World& world, const fs::path& file, const std::vector<Entity>& keepRoots)
    {
        // Everything that can fail happens before the world is touched, so
        // a bad file costs nothing.
        JsonWrapper doc;
        if (!doc.ReadFromFile(file))
        {
            return false;   // ReadFromFile already logged why
        }

        return LoadDocument(world, doc, file, keepRoots);
    }

    bool Scene::LoadFromText(World& world, const std::string& text, const fs::path& source,
                             const std::vector<Entity>& keepRoots)
    {
        // Same as Load: parse first, so a bad document costs nothing.
        JsonWrapper doc;
        if (!doc.ReadFromString(text))
        {
            CRU_ERROR("Scene: '{}' could not be parsed - not loading it", source.string());
            return false;   // ReadFromString logged the parse error itself
        }

        return LoadDocument(world, doc, source, keepRoots);
    }

    void Scene::Clear(World& world, const std::vector<Entity>& keepRoots)
    {
        if (keepRoots.empty())
        {
            world.DestroyAllEntities();
            m_ids.clear();
            return;
        }

        world.DestroyAllEntitiesExcept(keepRoots);
        // Only the kept entities' guids are still about anything.
        std::erase_if(m_ids, [&world](const auto& entry) { return !world.IsAlive(entry.first); });
    }

    bool Scene::LoadDocument(World& world, const JsonWrapper& doc, const fs::path& file,
                             const std::vector<Entity>& keepRoots)
    {
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
        Clear(world, keepRoots);

        // Kept entities by guid. Reloading the scene a kept entity came from
        // brings its guid back with the file's copy; the kept one then gets
        // a new guid, so every guid still names one entity (EntityOf).
        std::unordered_map<std::uint64_t, Entity> keptById;
        keptById.reserve(m_ids.size());
        for (const auto& [entity, id] : m_ids)
        {
            keptById.emplace(id.m_Value, entity);
        }
        const std::size_t keptCount = m_ids.size();

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

            if (const auto clash = keptById.find(id.m_Value); clash != keptById.end())
            {
                m_ids[clash->second] = xresource::instance_guid::GenerateGUIDCopy();
                keptById.erase(clash);
            }

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

        CRU_INFO("Scene: loaded '{}' ({} entities, {} components, {} parented, {} kept guids) from {}",
                 m_name, count, components, parented, keptCount, file.string());
        return true;
    }

    // =======================================================================
    //  Snapshots (play mode)
    //
    //  Kept apart from Save/Load, which are untouched: the same document
    //  layout, built and read in memory instead of through a file.
    // =======================================================================

    namespace {

        // Logged as the "file" a snapshot came from.
        constexpr const char* kSnapshotSource = "the play-mode snapshot";

        // A snapshot is only ever read by the build that wrote it, so anything
        // but the current version, the expected entity count and one unique id
        // per entity means the text was damaged.
        bool CheckSnapshotDocument(const JsonWrapper& doc, std::size_t expectedCount)
        {
            if (doc.Get<int>("version", 0) != kSceneVersion)
            {
                CRU_ERROR("Scene: {} has the wrong version", kSnapshotSource);
                return false;
            }

            const std::size_t count = doc.GetListSize("entities");
            if (count != expectedCount)
            {
                CRU_ERROR("Scene: {} holds {} entities, expected {}", kSnapshotSource, count, expectedCount);
                return false;
            }

            std::unordered_set<std::uint64_t> seen;
            seen.reserve(count);
            for (std::size_t i = 0; i < count; ++i)
            {
                const std::string idText = doc.Get<std::string>("entities/" + std::to_string(i) + "/id", std::string{});
                const std::uint64_t id = idText.empty() ? 0 : AssetRegistry::GuidFromString(idText).m_Value;
                if (id == 0 || !seen.insert(id).second)
                {
                    CRU_ERROR("Scene: {} has a missing or repeated entity id", kSnapshotSource);
                    return false;
                }
            }
            return true;
        }
    }

    bool Scene::CaptureSnapshot(World& world, SceneSnapshot& out)
    {
        SceneSnapshot snapshot;
        try
        {
            JsonWrapper doc;
            doc.Set("version", kSceneVersion);
            doc.Set("name", m_name);

            // Every live entity with the guid it keeps, the way Save picks them:
            // a known entity keeps its guid, a new one gets one that later Saves keep.
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

            // Guid order, as in a saved file.
            std::sort(ordered.begin(), ordered.end(),
                      [](const auto& lhs, const auto& rhs)
                      {
                          return lhs.first.m_Value < rhs.first.m_Value;
                      });

            doc.EnsureArray("entities");

            for (const auto& [id, entity] : ordered)
            {
                const std::size_t index = doc.PushObject("entities");
                const std::string base  = "entities/" + std::to_string(index);

                doc.Set(base + "/id", AssetRegistry::GuidToString(id));

                // The tree as guid references plus the sibling position, exactly
                // as Save stores it.
                const Entity parent = world.GetParent(entity);
                if (parent.IsValid())
                {
                    const auto parentId = m_ids.find(parent);
                    if (parentId != m_ids.end())
                    {
                        doc.Set(base + "/parent", AssetRegistry::GuidToString(parentId->second));
                        doc.Set(base + "/siblingIndex",
                                static_cast<int>(world.GetSiblingIndex(entity)));
                    }
                }

                EntityIO::WriteComponents(world, entity, doc, base + "/components");
            }

            snapshot.entityCount = ordered.size();
            snapshot.document    = doc.ToString(false);

            // Read it back the way RestoreSnapshot will, so a snapshot that
            // could not be restored is refused now, while nothing is lost.
            JsonWrapper check;
            if (!check.ReadFromString(snapshot.document)
                || !CheckSnapshotDocument(check, snapshot.entityCount))
            {
                CRU_ERROR("Scene: '{}' could not be captured - the snapshot did not read back", m_name);
                return false;
            }
        }
        catch (const std::exception& e)
        {
            CRU_ERROR("Scene: '{}' could not be captured - {}", m_name, e.what());
            return false;
        }

        out = std::move(snapshot);
        CRU_INFO("Scene: captured '{}' ({} entities, {} bytes)", m_name, out.entityCount, out.document.size());
        return true;
    }

    bool Scene::RestoreSnapshot(World& world, const SceneSnapshot& snapshot)
    {
        // Everything that can fail cheaply happens before the world is touched.
        if (!snapshot.IsValid())
        {
            CRU_ERROR("Scene: there is no snapshot to restore");
            return false;
        }

        JsonWrapper doc;
        if (!doc.ReadFromString(snapshot.document) || !CheckSnapshotDocument(doc, snapshot.entityCount))
        {
            CRU_ERROR("Scene: {} is damaged - the world was left as it is", kSnapshotSource);
            return false;
        }

        // --- Past this line the world is being replaced -------------------
        std::size_t components = 0;
        std::size_t parented   = 0;
        try
        {
            world.DestroyAllEntities();
            m_ids.clear();

            m_name = doc.Get<std::string>("name", m_name);

            const std::size_t count = doc.GetListSize("entities");

            // Every entity first (a parent can come after its child in guid
            // order), each with the guid it had at capture; links afterwards.
            struct PendingLink
            {
                Entity        child;
                std::uint64_t parentId;
                std::uint32_t siblingIndex;
            };
            std::vector<PendingLink> links;
            std::unordered_map<std::uint64_t, Entity> byId;
            byId.reserve(count);

            for (std::size_t i = 0; i < count; ++i)
            {
                const std::string base = "entities/" + std::to_string(i);

                const Entity entity = world.CreateEntity();
                const xresource::instance_guid id =
                    AssetRegistry::GuidFromString(doc.Get<std::string>(base + "/id", std::string{}));

                m_ids.emplace(entity, id);
                byId.emplace(id.m_Value, entity);

                components += EntityIO::ReadComponents(world, entity, doc, base + "/components");

                const std::string parentText = doc.Get<std::string>(base + "/parent", std::string{});
                if (!parentText.empty())
                {
                    links.push_back({ entity,
                                      AssetRegistry::GuidFromString(parentText).m_Value,
                                      static_cast<std::uint32_t>(doc.Get<int>(base + "/siblingIndex", 0)) });
                }
            }

            // Appending in (parent, siblingIndex) order reproduces the sibling order.
            std::sort(links.begin(), links.end(),
                      [](const PendingLink& lhs, const PendingLink& rhs)
                      {
                          if (lhs.parentId != rhs.parentId) return lhs.parentId < rhs.parentId;
                          return lhs.siblingIndex < rhs.siblingIndex;
                      });

            for (const PendingLink& link : links)
            {
                const auto parent = byId.find(link.parentId);
                if (parent == byId.end())
                {
                    CRU_WARN("Scene: an entity's parent is missing from {} - left as a root", kSnapshotSource);
                    continue;
                }

                // LOCAL Transforms were captured: keep them (worldPositionStays = false).
                const ParentResult result = world.TrySetParent(link.child, parent->second, false);
                if (result == ParentResult::Ok)
                {
                    ++parented;
                }
                else
                {
                    CRU_WARN("Scene: could not restore a parent link from {} ({}) - left as a root",
                             kSnapshotSource, ToString(result));
                }
            }
        }
        catch (const std::exception& e)
        {
            // Only allocation failure gets here. The world holds whatever was
            // rebuilt so far; the guid map matches it.
            CRU_ERROR("Scene: restoring {} failed part-way - {}", kSnapshotSource, e.what());
            return false;
        }

        CRU_INFO("Scene: restored '{}' ({} entities, {} components, {} parented)",
                 m_name, snapshot.entityCount, components, parented);
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

    xresource::instance_guid Scene::EnsureId(Entity entity)
    {
        if (!entity.IsValid())
        {
            return xresource::instance_guid{};
        }
        // try_emplace only generates when the entity is new to the map, so an
        // entity that already has a guid keeps it.
        const auto [it, inserted] = m_ids.try_emplace(entity, xresource::instance_guid{});
        if (inserted)
        {
            it->second = xresource::instance_guid::GenerateGUIDCopy();
        }
        return it->second;
    }

    Entity Scene::EntityOf(const xresource::instance_guid& id) const
    {
        if (id.m_Value == 0)
        {
            return kNullEntity;
        }
        for (const auto& [entity, known] : m_ids)
        {
            if (known.m_Value == id.m_Value)
            {
                return entity;
            }
        }
        return kNullEntity;
    }

}
