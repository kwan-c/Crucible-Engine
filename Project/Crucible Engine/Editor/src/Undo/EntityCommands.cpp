/*************************************************************************
  \file       EntityCommands.cpp
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       1 October 2026
  \version    1.0
  \brief
    Entity snapshot capture/restore and the two entity commands.
  \details
    Capture reuses EntityIO::WriteComponents, so a snapshot is byte for byte
    the "components" block a scene file would contain. Apply is its own
    X-macro over CRUCIBLE_COMPONENT_LIST because, unlike loading a scene, it
    also has to REMOVE components the snapshot does not have.
  \dependencies
    EntityCommands.h, Scene/EntityIO.h, Json/JsonWrapper.h, ECS/World.h,
    ECS/Components/Components.h, Diagnostics/Logger.h
  \see
    EntityCommands.h, Scene/EntityIO.cpp
  \history
    - 1.0 (1 October 2026): Initial version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#include "Undo/EntityCommands.h"

#include <type_traits>
#include <utility>

#include "Diagnostics/Logger.h"
#include "ECS/Components/Components.h"
#include "ECS/World.h"
#include "Json/JsonWrapper.h"
#include "Scene/EntityIO.h"

namespace {

    // Where the components sit inside a snapshot document.
    constexpr const char* kComponents = "components";

    /*************************************************************************
     \brief  Components a snapshot never holds - the same rule EntityIO uses.
     \details
        HierarchyNode is World-managed (AddComponent on it does not compile)
        and WorldTransform is recomputed every frame.
     *************************************************************************/
    template <typename T>
    inline constexpr bool kSkipInSnapshot =
        Crucible::WorldManaged<T>::value
        || std::is_same_v<T, Crucible::WorldTransform>;

    /*************************************************************************
     \brief  The JSON path of component T inside a snapshot.
     *************************************************************************/
    template <typename T>
    std::string PathOf()
    {
        return std::string(kComponents) + "/" + std::string(Crucible::ComponentTraits<T>::name);
    }

    /*************************************************************************
     \brief  Pass 1 for one type: remove T if the snapshot does not have it.
     \note
        Must be a template. World::RemoveComponent is constrained on
        RemovableComponent, and a constraint is checked even inside a
        discarded `if constexpr` branch unless that branch depends on a
        template parameter - so written straight into an X-macro in a
        normal function, RemoveComponent<Transform> fails to compile (the
        same C2672 the Inspector's MakeComponentRemover works around).
     *************************************************************************/
    template <typename T>
    void RemoveIfAbsent(Crucible::World& world, Crucible::Entity entity, const Crucible::JsonWrapper& doc)
    {
        if constexpr (!kSkipInSnapshot<T> && Crucible::RemovableComponent<T>)
        {
            if (!doc.Has(PathOf<T>()) && world.HasComponent<T>(entity))
            {
                world.RemoveComponent<T>(entity);
            }
        }
    }

    /*************************************************************************
     \brief  Pass 2 for one type: add or overwrite T from the snapshot.
     \details
        Starts from the current value, so fields the snapshot does not
        carry (runtime handles, DO_NOT_SERIALIZE state) keep their values
        instead of being reset to defaults.
     *************************************************************************/
    template <typename T>
    void RestoreIfPresent(Crucible::World& world, Crucible::Entity entity, const Crucible::JsonWrapper& doc)
    {
        if constexpr (!kSkipInSnapshot<T>)
        {
            const std::string path = PathOf<T>();
            if (doc.Has(path))
            {
                T component{};
                if (const T* current = world.Read<T>(entity))
                {
                    component = *current;
                }
                doc.GetObject(path, component);
                // With the snapshot's on/off state ("$enabled", absent = on): an existing component
                // is switched through SetComponentEnabled, so undo/redo notify once per real change;
                // a re-created one (paste, duplicate, undo delete) simply starts in it.
                if constexpr (Crucible::ToggleableComponent<T>)
                {
                    const bool enabled = doc.Get<bool>(path + "/" + Crucible::kComponentEnabledKey, true);
                    world.AddComponent<T>(entity, std::move(component), enabled);
                }
                else
                {
                    world.AddComponent<T>(entity, std::move(component));
                }
            }
        }
    }
}

namespace Crucible {

    // =======================================================================
    //  Snapshots
    // =======================================================================

    std::string EntityState::Capture(World& world, Entity entity)
    {
        if (!world.IsAlive(entity))
        {
            return std::string{};
        }

        JsonWrapper doc;
        EntityIO::WriteComponents(world, entity, doc, kComponents);
        return doc.ToString(false);   // compact: these are compared, never read by people
    }

    void EntityState::Apply(World& world, Entity entity, const std::string& snapshot)
    {
        if (!world.IsAlive(entity))
        {
            CRU_WARN("Undo: entity {} is not alive - nothing to restore", entity.index);
            return;
        }

        JsonWrapper doc;
        if (!doc.ReadFromString(snapshot))
        {
            return;   // already logged
        }

        // --- Pass 1: remove what the snapshot does not have ---------------
        // Every removal runs before any add, so two components that may not
        // coexist (Collider, CharacterController) are never on the entity
        // at the same time.
        #define CRU_UNDO_REMOVE(T) RemoveIfAbsent<T>(world, entity, doc);
        CRUCIBLE_COMPONENT_LIST(CRU_UNDO_REMOVE)
        #undef CRU_UNDO_REMOVE

        // --- Pass 2: add or overwrite what it does have --------------------
        #define CRU_UNDO_RESTORE(T) RestoreIfPresent<T>(world, entity, doc);
        CRUCIBLE_COMPONENT_LIST(CRU_UNDO_RESTORE)
        #undef CRU_UNDO_RESTORE
    }

    // =======================================================================
    //  EntityStateCommand
    // =======================================================================

    void EntityStateCommand::Undo(UndoHistory& history)
    {
        EntityState::Apply(history.GetWorld(), history.Resolve(m_entity), m_before);
    }

    void EntityStateCommand::Redo(UndoHistory& history)
    {
        EntityState::Apply(history.GetWorld(), history.Resolve(m_entity), m_after);
    }

    // =======================================================================
    //  CreateEntityCommand
    // =======================================================================

    void CreateEntityCommand::Undo(UndoHistory& history)
    {
        // DestroyEntity also destroys descendants. Nothing in the editor can
        // parent to a freshly created entity yet, so there are none.
        history.GetWorld().DestroyEntity(history.Resolve(m_entity));
    }

    void CreateEntityCommand::Redo(UndoHistory& history)
    {
        World& world = history.GetWorld();

        // A brand-new handle. Rebinding makes every other command that
        // refers to this entity - by key - find it here.
        const Entity entity = world.CreateEntity();
        EntityState::Apply(world, entity, m_snapshot);
        history.Rebind(m_entity, entity);
    }


    // =======================================================================
    //  DeleteEntityCommand
    // =======================================================================

    std::unique_ptr<DeleteEntityCommand> DeleteEntityCommand::Capture(UndoHistory& history, Entity entity, std::string label)
    {
        auto command = std::unique_ptr<DeleteEntityCommand>(new DeleteEntityCommand());
        command->m_label = std::move(label);

        World& world = history.GetWorld();
        if (!world.IsAlive(entity))
        {
            return command;
        }

        // Depth-first, parent before children, children first-to-last - the
        // order Undo needs to rebuild the tree with the same sibling order.
        auto visit = [&](auto&& self, Entity node) -> void
        {
            const Entity parent = world.GetParent(node);
            command->m_nodes.push_back(Node{
                history.KeyOf(node),
                parent == kNullEntity ? kNullEntity : history.KeyOf(parent),
                world.GetSiblingIndex(node),
                EntityState::Capture(world, node) });

            world.ForEachChild(node, [&](Entity child) { self(self, child); });
        };
        visit(visit, entity);

        return command;
    }

    void DeleteEntityCommand::Undo(UndoHistory& history)
    {
        World& world = history.GetWorld();

        for (const Node& node : m_nodes)
        {
            const Entity entity = world.CreateEntity();
            EntityState::Apply(world, entity, node.snapshot);
            history.Rebind(node.key, entity);

            if (!(node.parent == kNullEntity))
            {
                const Entity parent = history.Resolve(node.parent);
                // false: the snapshot holds the LOCAL Transform; keep it as-is
                // (same reasoning as Scene loading).
                if (world.IsAlive(parent) && world.TrySetParent(entity, parent, false) == ParentResult::Ok)
                {
                    world.SetSiblingIndex(entity, node.siblingIndex);
                }
                else
                {
                    CRU_WARN("Undo: could not re-parent restored entity {} - left as a root", entity.index);
                }
            }
        }
    }

    void DeleteEntityCommand::Redo(UndoHistory& history)
    {
        if (m_nodes.empty())
        {
            return;
        }
        // Destroys the descendants too, exactly like the original delete.
        history.GetWorld().DestroyEntity(history.Resolve(m_nodes.front().key));
    }

    // =======================================================================
    //  ReparentCommand
    // =======================================================================

    void ReparentCommand::Apply(UndoHistory& history, const Placement& placement) const
    {
        World& world = history.GetWorld();
        const Entity entity = history.Resolve(m_entity);
        if (!world.IsAlive(entity))
        {
            CRU_WARN("Undo: entity {} is not alive - reparent not applied", entity.index);
            return;
        }

        const Entity parent = placement.parent == kNullEntity ? kNullEntity : history.Resolve(placement.parent);

        // false: keep the local Transform as-is; the exact one is written right after anyway.
        const ParentResult result = world.TrySetParent(entity, parent, false);
        if (result != ParentResult::Ok)
        {
            CRU_WARN("Undo: could not reparent entity {} ({})", entity.index, ToString(result));
            return;
        }
        if (!(parent == kNullEntity))
        {
            world.SetSiblingIndex(entity, placement.siblingIndex);
        }
        world.AddComponent<Transform>(entity, placement.local);   // overwrites
    }
}
