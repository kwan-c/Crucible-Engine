/*************************************************************************
  \file       EntityRefs.h
  \author     Tan Chun Kwang Xavier
  \par        Email: chunkwangxavier.tan@digipen.edu
  \date       2 November 2026
  \version    1.0
  \brief
    Finds and rewrites the references one entity's components hold to OTHER
    entities, so prefabs can translate them.
  \details
    Saved entity references are 64-bit ids: a scene id (Scene::IdOf) in a
    scene, a prefab entry id (the per-entry "id" Prefab::SaveFrom writes)
    inside a .cprefab. Today two things hold them:
      - ScriptComponent fields of type Entity (ScriptFieldValue::entityId)
      - PhysicsJoint::m_connectedBody (the only ENTITY_REF property)
    A new component with an entity reference must be added to Remap() here,
    or prefabs will keep pointing at the entity they were made from.

    `map` is called once per non-zero reference and returns the id to store
    (return the same id to leave it alone, 0 to clear it).
  \see
    Prefab.h (SaveFrom / Instantiate), Editor Prefabs/PrefabAuthoring.cpp
*************************************************************************/
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "ECS/Core/ECSCore.h"   // Entity
#include "EngineExport.h"

namespace Crucible {

    class World;
    class JsonWrapper;
    struct ScriptComponent;
    struct PhysicsJoint;

    namespace EntityRefs {

        /// old id -> new id (called only for non-zero ids).
        using Map = std::function<std::uint64_t(std::uint64_t)>;

        /*********************************************************************
         \brief  Rewrites one component's entity references in place.
         \return true if anything changed.
         *********************************************************************/
        ENGINE_API bool Remap(ScriptComponent& component, const Map& map);
        ENGINE_API bool Remap(PhysicsJoint& component, const Map& map);

        /// Every other component holds no entity references.
        template <typename T>
        bool Remap(T&, const Map&) { return false; }

        /*********************************************************************
         \brief  Rewrites every reference held by a live entity's components.
                 Components are only written (marked changed) when a value
                 actually changes.
         \return true if anything changed.
         *********************************************************************/
        ENGINE_API bool RemapEntity(World& world, Entity entity, const Map& map);

        /*********************************************************************
         \brief  Rewrites the references in one saved entity's components, as
                 JSON (e.g. "entities/3/components" in a .cprefab). A
                 component's "$enabled" (switched off) state is kept.
         \return true if anything changed.
         *********************************************************************/
        ENGINE_API bool RemapDocument(JsonWrapper& doc, const std::string& componentsPath, const Map& map);

    }   // NAMESPACE ENTITYREFS

}   // NAMESPACE CRUCIBLE
