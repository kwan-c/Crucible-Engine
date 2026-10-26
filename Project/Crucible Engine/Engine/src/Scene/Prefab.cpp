/*************************************************************************
  \file       Prefab.cpp
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       29 September 2026
  \version    3.0
  \brief
    Prefab save and instantiate implementation.
  \details
    Still thin: the component work is all EntityIO, the same code Scene
    uses. What this file adds is the tree - walking the subtree depth-first
    on save, and re-attaching each entity to its parent by array index on
    instantiate. A prefab is a small scene without the identity.
  \dependencies
    Prefab.h, EntityIO.h, ECS/World.h, ECS/Components/Components.h,
    Json/JsonWrapper.h, Diagnostics/Logger.h
  \see
    Prefab.h, Scene.cpp, EntityIO.cpp
  \history
    - 1.0 (29 September 2026): Initial version.
    - 1.1 (17 October 2026): an instance whose saved name is a default
      ("Entity 3") gets a fresh one (World::ReissueDefaultName).
    - 2.0 (23 October 2026): Multi-entity prefabs ("entities" array with
      parent indices), root position zeroed on save, optional parent on
      Instantiate, tree validated at load. Version 1 files still load.
    - 3.0 (25 October 2026): Stable per-entry ids; instances are linked to
      their file (PrefabInstance); the link is stripped when saving.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#include "Scene/Prefab.h"

#include "AssetPipe/AssetRegistry.h"
#include "Diagnostics/Logger.h"
#include "ECS/Components/Components.h"
#include "ECS/World.h"
#include "Scene/EntityIO.h"

namespace fs = std::filesystem;

namespace {

    // Version 1: where the single entity's components live.
    constexpr const char* PREFAB_V1_COMPONENTS = "entity/components";

    // Version 2: the entity array.
    constexpr const char* PREFAB_ENTITIES = "entities";

    /*************************************************************************
     \brief  "entities/<index>" - the base path of one entity in a v2 file.
     *************************************************************************/
    std::string EntityBase(std::size_t index)
    {
        return std::string(PREFAB_ENTITIES) + "/" + std::to_string(index);
    }
}

namespace Crucible {

    // =======================================================================
    //  Authoring
    // =======================================================================

    bool Prefab::SaveFrom(World& world,
                          Entity root,
                          const fs::path& file,
                          const std::string& name,
                          std::vector<std::pair<Entity, std::uint64_t>>* outIds)
    {
        if (!world.IsAlive(root))
        {
            CRU_ERROR("Prefab: cannot save from a dead entity (index {}, gen {})",
                      root.index, root.generation);
            return false;
        }

        JsonWrapper doc;
        doc.Set("version", kPrefabVersion);
        doc.Set("name", name.empty() ? file.stem().string() : name);
        doc.EnsureArray(PREFAB_ENTITIES);

        // Depth-first, parent before children, children first-to-last (the
        // same order Hierarchy::CopySubtree uses). Instantiate relies on it:
        // every parent index is smaller than its child's, and appending
        // children in file order restores the sibling order.
        auto visit = [&](auto&& self, Entity entity, int parentIndex) -> void
        {
            const std::size_t index = doc.PushObject(PREFAB_ENTITIES);
            const std::string base  = EntityBase(index);

            // A fresh random id per entry: what instances link to (PrefabInstance::fileId).
            const std::uint64_t id = xresource::instance_guid::GenerateGUIDCopy().m_Value;
            doc.Set(base + "/id", AssetRegistry::GuidToString(xresource::instance_guid{ id }));
            if (outIds != nullptr) outIds->emplace_back(entity, id);

            doc.Set(base + "/parent", parentIndex);
            EntityIO::WriteComponents(world, entity, doc, base + "/components");
            // The link belongs to the scene the entity is in, never to the template.
            doc.Remove(base + "/components/" + std::string(ComponentTraits<PrefabInstance>::name));

            const int self_index = static_cast<int>(index);
            world.ForEachChild(entity, [&](Entity child) { self(self, child, self_index); });
        };
        visit(visit, root, -1);

        // Zero the root's position (rotation and scale stay), so an instance
        // lands at the origin, or exactly on the parent it is given.
        // Children are untouched - their Transforms are relative to the root.
        if (const Transform* source = world.Read<Transform>(root))
        {
            Transform rootLocal = *source;
            rootLocal.positions = { 0.f, 0.f, 0.f };
            doc.SetObject(EntityBase(0) + "/components/" + std::string(ComponentTraits<Transform>::name),
                          rootLocal);
        }

        if (!doc.SaveToFile(file))
        {
            return false;   // already logged
        }

        CRU_INFO("Prefab: saved '{}' ({} entities) to {}",
                 doc.Get<std::string>("name", std::string{}),
                 doc.GetListSize(PREFAB_ENTITIES),
                 file.string());
        return true;
    }

    // =======================================================================
    //  Loading
    // =======================================================================

    bool Prefab::Load(const fs::path& file)
    {
        m_valid = false;

        if (!m_doc.ReadFromFile(file))
        {
            return false;   // already logged
        }

        // The file's asset guid, so copies can link back to it. A file outside the asset
        // index (no .meta yet) still loads; its copies are just not linked.
        m_asset = 0;
        std::error_code ec;
        const fs::path meta = AssetRegistry::MetaPathFor(file);
        AssetMeta info;
        if (fs::exists(meta, ec) && AssetRegistry::ReadMeta(meta, info)) m_asset = info.guid.m_Value;
        m_hash = AssetRegistry::HashFileContents(file);   // which version of the file this is

        return Validate(file.string(), file.stem().string());
    }

    bool Prefab::LoadFromString(const std::string& json)
    {
        m_valid = false;

        if (!m_doc.ReadFromString(json))
        {
            return false;
        }

        m_asset = 0;   // text has no .meta: its copies are not linked
        m_hash  = 0;
        return Validate("text", "Prefab");
    }

    bool Prefab::Validate(const std::string& source, const std::string& fallbackName)
    {
        m_valid = false;
        m_parents.clear();
        m_ids.clear();

        m_version = m_doc.Get<int>("version", 0);
        if (m_version > kPrefabVersion)
        {
            CRU_ERROR("Prefab: '{}' is version {}, this build understands {} - "
                      "not loading it",
                      source, m_version, kPrefabVersion);
            return false;
        }

        if (m_version <= 1)
        {
            // Version 1: always exactly one entity. (0 = no version field;
            // treated the same, as the only format that predates the field
            // being checked.)
            m_parents.push_back(-1);
            m_ids.push_back(1);
        }
        else
        {
            // Version 2: check the whole tree now, so Instantiate never has
            // to stop half way and leave a partial copy in the world.
            const std::size_t count = m_doc.GetListSize(PREFAB_ENTITIES);
            if (count == 0)
            {
                CRU_ERROR("Prefab: '{}' has no entities", source);
                return false;
            }

            m_parents.reserve(count);
            for (std::size_t i = 0; i < count; ++i)
            {
                const int parent = m_doc.Get<int>(EntityBase(i) + "/parent", -1);

                const bool ok = (i == 0)
                    ? parent == -1                                         // [0] is the root
                    : parent >= 0 && static_cast<std::size_t>(parent) < i; // parent comes first

                if (!ok)
                {
                    CRU_ERROR("Prefab: '{}' entity {} has parent {} - the root must be "
                              "first with parent -1, and every other entity's parent "
                              "must come before it",
                              source, i, parent);
                    m_parents.clear();
                    m_ids.clear();
                    return false;
                }
                m_parents.push_back(parent);

                // Version 3 entries carry a stable id; older ones use their position.
                const std::string idText = m_doc.Get<std::string>(EntityBase(i) + "/id", std::string{});
                std::uint64_t id = idText.empty() ? 0 : AssetRegistry::GuidFromString(idText).m_Value;
                if (id == 0) id = static_cast<std::uint64_t>(i) + 1;
                m_ids.push_back(id);
            }
        }

        m_name  = m_doc.Get<std::string>("name", fallbackName);
        m_valid = true;
        return true;
    }

    int Prefab::IndexOf(std::uint64_t fileId) const
    {
        if (!m_valid) return -1;
        for (std::size_t i = 0; i < m_ids.size(); ++i)
        {
            if (m_ids[i] == fileId) return static_cast<int>(i);
        }
        return -1;
    }

    std::string Prefab::ComponentsPath(std::size_t index) const
    {
        return (m_version <= 1) ? std::string(PREFAB_V1_COMPONENTS) : EntityBase(index) + "/components";
    }

    // =======================================================================
    //  Instantiating
    // =======================================================================

    Entity Prefab::Instantiate(World& world, Entity parent) const
    {
        if (!m_valid)
        {
            CRU_ERROR("Prefab: Instantiate called on a prefab that is not loaded");
            return kNullEntity;
        }

        std::vector<Entity> created;
        created.reserve(m_parents.size());

        for (std::size_t i = 0; i < m_parents.size(); ++i)
        {
            const Entity entity = world.CreateEntity();

            const std::string components = (m_version <= 1)
                ? std::string(PREFAB_V1_COMPONENTS)
                : EntityBase(i) + "/components";
            EntityIO::ReadComponents(world, entity, m_doc, components);
            world.ReissueDefaultName(entity);   // "Entity 3" was the source's default, not this instance's; custom names stay

            // The link back to the file: which prefab, which entry of it, and whether this is
            // the instance's root. Only for a file loaded from a registered asset.
            if (m_asset != 0)
            {
                PrefabInstance link{ m_asset, m_ids[i], i == 0 };
                if (i == 0) link.prefabHash = m_hash;   // the version of the file this instance matches
                world.AddComponent<PrefabInstance>(entity, std::move(link));
            }

            // The root goes under the caller's parent (if any); everyone else
            // under an entity created earlier in this loop. false keeps the
            // saved LOCAL Transform as-is, the same as Scene::Load.
            const Entity attachTo = (i == 0)
                ? parent
                : created[static_cast<std::size_t>(m_parents[i])];

            if (!(attachTo == kNullEntity))
            {
                const ParentResult result = world.TrySetParent(entity, attachTo, false);
                if (result != ParentResult::Ok)
                {
                    CRU_WARN("Prefab: '{}' could not attach entity {} to its parent ({}) - "
                             "left as a root",
                             m_name, i, ToString(result));
                }
            }

            created.push_back(entity);
        }

        return created.front();
    }

}
