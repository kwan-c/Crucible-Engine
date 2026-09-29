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

        for (const auto& [id, entity] : ordered)
        {
            const std::size_t index = doc.PushObject("entities");
            const std::string base  = "entities/" + std::to_string(index);

            doc.Set(base + "/id", AssetRegistry::GuidToString(id));
            EntityIO::WriteComponents(world, entity, doc, base + "/components");
        }

        if (!doc.SaveToFile(file))
        {
            return false;
        }

        CRU_INFO("Scene: saved '{}' ({} entities) to {}",
                 m_name, ordered.size(), file.string());
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

            components += EntityIO::ReadComponents(world, entity, doc,
                                                   base + "/components");
        }

        CRU_INFO("Scene: loaded '{}' ({} entities, {} components) from {}",
                 m_name, count, components, file.string());
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
