/*************************************************************************
  \file       EntityIO.cpp
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       29 September 2026
  \version    1.0
  \brief
    Component-level entity read/write implementation.
  \details
    Both functions are one X-macro expansion over CRUCIBLE_COMPONENT_LIST.
    The generated code is a straight-line chain of compile-time-typed
    branches, which is how a runtime string like "Transform" ends up
    calling the template AddComponent<Transform>.
  \dependencies
    EntityIO.h, ECS/World.h, ECS/Components/Components.h,
    Json/JsonWrapper.h, Diagnostics/Logger.h
  \see
    EntityIO.h, Scene.cpp, Prefab.cpp
  \history
    - 1.0 (29 September 2026): Initial version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#include "Scene/EntityIO.h"

#include <utility>
#include <vector>

#include "Diagnostics/Logger.h"
#include "ECS/Components/Components.h"
#include "ECS/World.h"
#include "Json/JsonWrapper.h"

namespace Crucible::EntityIO {

    void WriteComponents(World& world,
                         Entity entity,
                         JsonWrapper& doc,
                         const std::string& componentsPath)
    {
        // An entity with no components still gets an empty object, so the
        // file shape is the same whether or not anything was written.
        doc.EnsureObject(componentsPath);

        // Expanded once per entry in CRUCIBLE_COMPONENT_LIST. T is a real
        // type here, so Read<T> and ComponentTraits<T> both resolve at
        // compile time.
        #define CRU_WRITE_ONE_COMPONENT(T)                                  \
            {                                                               \
                if (world.HasComponent<T>(entity))                          \
                {                                                           \
                    if (const T* component = world.Read<T>(entity))         \
                    {                                                       \
                        doc.SetObject(componentsPath + "/"                   \
                                      + std::string(ComponentTraits<T>::name),\
                                      *component);                           \
                    }                                                        \
                }                                                            \
            }

        CRUCIBLE_COMPONENT_LIST(CRU_WRITE_ONE_COMPONENT)

        #undef CRU_WRITE_ONE_COMPONENT
    }

    std::size_t ReadComponents(World& world,
                               Entity entity,
                               const JsonWrapper& doc,
                               const std::string& componentsPath)
    {
        std::size_t applied = 0;

        // GetMemberNames is what makes this generic: the file tells us which
        // components the entity had, we do not ask the entity.
        for (const std::string& name : doc.GetMemberNames(componentsPath))
        {
            const std::string path = componentsPath + "/" + name;
            bool matched = false;

            // The reverse of the write macro: compare the runtime name
            // against each compile-time name until one hits.
            #define CRU_READ_ONE_COMPONENT(T)                               \
                if (!matched && name == ComponentTraits<T>::name)           \
                {                                                           \
                    T component{};                                          \
                    doc.GetObject(path, component);                         \
                    world.AddComponent<T>(entity, std::move(component));    \
                    matched = true;                                         \
                }

            CRUCIBLE_COMPONENT_LIST(CRU_READ_ONE_COMPONENT)

            #undef CRU_READ_ONE_COMPONENT

            if (matched)
            {
                ++applied;
            }
            else
            {
                // Not fatal. Most likely a component that exists on another
                // branch, or one that has since been removed from
                // CRUCIBLE_COMPONENT_LIST.
                CRU_WARN("EntityIO: unknown component '{}' at '{}' - skipped",
                         name, componentsPath);
            }
        }

        return applied;
    }

}
