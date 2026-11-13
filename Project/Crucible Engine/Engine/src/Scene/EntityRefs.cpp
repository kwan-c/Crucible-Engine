/*************************************************************************
  \file       EntityRefs.cpp
  \author     Tan Chun Kwang Xavier
  \par        Email: chunkwangxavier.tan@digipen.edu
  \date       2 November 2026
  \version    1.0
  \brief      Implementation - see EntityRefs.h.
*************************************************************************/
#include "Scene/EntityRefs.h"

#include "ECS/Components/Components.h"
#include "ECS/World.h"
#include "Json/JsonWrapper.h"
#include "Physics/PhysicsComponents.h"

namespace Crucible::EntityRefs {

    namespace {

        // Applies `map` to one stored id; true if it changed.
        bool Apply(std::uint64_t& id, const Map& map)
        {
            if (id == 0) return false;
            const std::uint64_t mapped = map(id);
            if (mapped == id) return false;
            id = mapped;
            return true;
        }

        // One component of a live entity: copy, remap, write back only if changed.
        template <typename T>
        bool RemapLive(World& world, Entity entity, const Map& map)
        {
            const T* current = world.Read<T>(entity);
            if (current == nullptr) return false;
            T copy = *current;
            if (!Remap(copy, map)) return false;
            if (T* target = world.Write<T>(entity)) *target = std::move(copy);
            return true;
        }

        // One component in a JSON document: read, remap, write back (keeping "$enabled").
        template <typename T>
        bool RemapJson(JsonWrapper& doc, const std::string& componentsPath, const Map& map)
        {
            const std::string path = componentsPath + "/" + std::string(ComponentTraits<T>::name);
            if (!doc.Has(path)) return false;
            T component{};
            doc.GetObject(path, component);
            if (!Remap(component, map)) return false;

            const std::string enabledPath = path + "/" + kComponentEnabledKey;
            const bool hasEnabled = doc.Has(enabledPath);
            const bool enabled = doc.Get<bool>(enabledPath, true);
            doc.SetObject(path, component);   // replaces the object...
            if (hasEnabled) doc.Set(enabledPath, enabled);   // ...so put the on/off state back
            return true;
        }

    }   // namespace

    bool Remap(ScriptComponent& component, const Map& map)
    {
        bool changed = false;
        for (ScriptEntry& script : component.scripts)
        {
            for (ScriptFieldValue& field : script.fields)
            {
                if (field.type == ScriptFieldType::Entity) changed |= Apply(field.entityId, map);
            }
        }
        return changed;
    }

    bool Remap(PhysicsJoint& component, const Map& map)
    {
        return Apply(component.m_connectedBody, map);
    }

    bool RemapEntity(World& world, Entity entity, const Map& map)
    {
        if (!world.IsAlive(entity)) return false;
        bool changed = RemapLive<ScriptComponent>(world, entity, map);
        changed |= RemapLive<PhysicsJoint>(world, entity, map);
        return changed;
    }

    bool RemapDocument(JsonWrapper& doc, const std::string& componentsPath, const Map& map)
    {
        bool changed = RemapJson<ScriptComponent>(doc, componentsPath, map);
        changed |= RemapJson<PhysicsJoint>(doc, componentsPath, map);
        return changed;
    }

}   // NAMESPACE CRUCIBLE::ENTITYREFS
