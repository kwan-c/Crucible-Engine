/*************************************************************************
  \file       EntityIO.h
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       29 September 2026
  \version    1.0
  \brief
    Moves one entity's components between the World and a JSON document.
  \details
    The single place that knows how to turn "this entity" into JSON and
    back. Scene and Prefab are both just containers around these two
    functions - a scene is many entities, a prefab is one.

    Neither function names a component type. Both expand
    CRUCIBLE_COMPONENT_LIST from ComponentKind.h, the same X-macro
    ComponentRegistry::RegisterAll() uses, so adding a component to that
    list makes it saveable and loadable with no change here.
  \dependencies
    ECS/Core/ECSCore.h, ECS/World.h, ECS/Components/Components.h,
    Json/JsonWrapper.h
  \see
    EntityIO.cpp, Scene.h, Prefab.h, ECS/Components/ComponentKind.h
  \history
    - 1.0 (29 September 2026): Initial version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#pragma once

#include <cstddef>
#include <string>

#include "ECS/Core/ECSCore.h"     // Entity
#include "EngineExport.h"

namespace Crucible {

    class World;
    class JsonWrapper;

    /*************************************************************************
     \brief
        Component-level read and write for a single entity.
     *************************************************************************/
    namespace EntityIO {

        /*********************************************************************
         \brief
            Write every component the entity has into a document.

         \param[in]  world          The world the entity lives in.
         \param[in]  entity         The entity to write. Must be alive.
         \param[out] doc            Receives the components.
         \param[in]  componentsPath Where in the document to put them, e.g.
                                    "entities/0/components".

         \details
            Each component becomes one object under componentsPath, keyed by
            ComponentTraits<T>::name - so a Transform lands at
            "entities/0/components/Transform".

         \note
            Reads through World::Read, which is the const accessor, so
            saving a scene does not bump any component's change tick.
         *********************************************************************/
        ENGINE_API void WriteComponents(World& world,
                                        Entity entity,
                                        JsonWrapper& doc,
                                        const std::string& componentsPath);

        /*********************************************************************
         \brief
            Add to an entity every component found under a path.

         \param[in,out] world          The world to add components in.
         \param[in]     entity         The entity to fill. Must already exist.
         \param[in]     doc            The document to read from.
         \param[in]     componentsPath Where to look, e.g.
                                       "entities/0/components".
         \return
            How many components were applied.

         \details
            Member names under componentsPath are matched against
            ComponentTraits<T>::name. A name that matches nothing is logged
            and skipped, so a scene saved by a branch that has an extra
            component still loads on a branch that does not - it just comes
            back without that component.

         \note
            Every add is a structural change, so the entity moves archetype
            once per component. Fine for loading; do not call this from
            inside a Query.
         *********************************************************************/
        ENGINE_API std::size_t ReadComponents(World& world,
                                              Entity entity,
                                              const JsonWrapper& doc,
                                              const std::string& componentsPath);
    }

}
