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

#include <type_traits>
#include <utility>
#include <vector>

#include "Diagnostics/Logger.h"
#include "ECS/Components/Components.h"
#include "ECS/World.h"
#include "Json/JsonWrapper.h"

namespace {

    /*************************************************************************
     \brief
        Components in CRUCIBLE_COMPONENT_LIST that must never be written to,
        or read from, a scene file.

     \details
        HierarchyNode   Raw Entity handles (parent, siblings). Handles are
                        slot indices that mean nothing after a reload. It is
                        also WorldManaged, so AddComponent<HierarchyNode> is
                        a compile error. The tree is saved by Scene instead,
                        as guid references resolved through World::SetParent.

        WorldTransform  Derived: HierarchyTransformSystem recomputes it from
                        Transform every frame. Saving it would only store a
                        stale copy.

     \note
        Checked with if constexpr, not a runtime if. A runtime branch still
        instantiates AddComponent<HierarchyNode>, and its static_assert fires
        even though the branch could never run.
     *************************************************************************/
    template <typename T>
    inline constexpr bool kSkipInSceneFile =
        Crucible::WorldManaged<T>::value
        || std::is_same_v<T, Crucible::WorldTransform>;
}

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
            if constexpr (!kSkipInSceneFile<T>)                             \
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
            bool recognised = false;

            // The reverse of the write macro: compare the runtime name
            // against each compile-time name until one hits.
            //
            // Transform is a mandatory component, so the entity already has
            // one from CreateEntity(). AddComponent on an existing component
            // overwrites it, which is exactly what loading wants.
            #define CRU_READ_ONE_COMPONENT(T)                               \
                if (!recognised && name == ComponentTraits<T>::name)        \
                {                                                           \
                    recognised = true;                                      \
                    if constexpr (!kSkipInSceneFile<T>)                     \
                    {                                                       \
                        T component{};                                      \
                        doc.GetObject(path, component);                     \
                        world.AddComponent<T>(entity, std::move(component));\
                        ++applied;                                          \
                    }                                                       \
                }

            CRUCIBLE_COMPONENT_LIST(CRU_READ_ONE_COMPONENT)

            #undef CRU_READ_ONE_COMPONENT

            if (!recognised)
            {
                // Not fatal. Most likely a component that exists on another
                // branch, or one that has since been removed from
                // CRUCIBLE_COMPONENT_LIST.
                CRU_WARN("EntityIO: unknown component '{}' at '{}' - skipped",
                         name, componentsPath);
            }
        }

        // Upgrade the early preview format at the common scene/prefab/snapshot boundary.
        // Renderer ownership is authoritative in new files; never overwrite its settings.
        const std::string animationPath = componentsPath + "/AnimatedModel";
        if (world.HasComponent<AnimatedModel>(entity)) {
            if (!doc.Has(animationPath + "/playOnStart") && doc.Has(animationPath + "/playing"))
                world.Write<AnimatedModel>(entity)->playOnStart = doc.Get<bool>(animationPath + "/playing", true);
            if (doc.Has(animationPath + "/modelAsset")) {
                const auto* existing = world.Read<MeshRenderer>(entity);
                MeshRenderer renderer = existing ? *existing : MeshRenderer{};
                if (renderer.modelAsset == 0) renderer.modelAsset = doc.Get<uint64_t>(animationPath + "/modelAsset", 0);
                if (!doc.Has(componentsPath + "/MeshRenderer/visible"))
                    renderer.visible = doc.Get<bool>(animationPath + "/visible", true);
                if (existing) *world.Write<MeshRenderer>(entity) = std::move(renderer);
                else { world.AddComponent<MeshRenderer>(entity, std::move(renderer)); ++applied; }
            }
        }
        return applied;
    }

}
