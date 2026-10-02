/*************************************************************************
  \file       EntityCommands.h
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       1 October 2026
  \version    1.0
  \brief
    The undoable entity actions: change an entity, and create one.
  \details
    Both commands work from snapshots: the entity's serialisable components
    written to compact JSON through the same EntityIO code scenes use. That
    is what lets one command type cover every Inspector action - editing a
    field, adding a component and removing one are all just "the entity
    looked like this before and like that after".

    Snapshots cover exactly what a scene file covers. Anything a scene does
    not save is not touched by undo either:
      - DO_NOT_SERIALIZE properties (CharacterController runtime state)
      - runtime handles (MeshRenderer::mesh, Skybox::cubemap); the asset
        ids beside them ARE restored, and ResourceManager resolves those
      - HierarchyNode and WorldTransform
  \dependencies
    UndoHistory.h, ECS/Core/ECSCore.h, <string>
  \see
    EntityCommands.cpp, UndoHistory.h, Scene/EntityIO.h
  \history
    - 1.0 (1 October 2026): Initial version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#pragma once

#include <string>

#include "ECS/Core/ECSCore.h"
#include "Undo/UndoHistory.h"

namespace Crucible {

    class World;

    /*************************************************************************
     \brief  Capture and restore one entity's serialisable components.
     *************************************************************************/
    namespace EntityState {

        /*********************************************************************
         \brief  Snapshot an entity as compact JSON.
         \return "" if the entity is not alive.
         \note   Two snapshots of an unchanged entity compare equal as
                 strings, which is how the Inspector tells a real edit from
                 a click that changed nothing.
         *********************************************************************/
        std::string Capture(World& world, Entity entity);

        /*********************************************************************
         \brief
            Make an entity match a snapshot exactly.

         \details
            Components in the snapshot are added or overwritten; removable
            components NOT in the snapshot are removed. Every removal runs
            before any add, so the entity is never briefly holding two
            components that may not coexist (Collider and
            CharacterController).

            Overwrites start from the component's current value, so fields
            the snapshot does not carry - runtime handles, runtime state -
            keep their values instead of being reset.
         *********************************************************************/
        void Apply(World& world, Entity entity, const std::string& snapshot);
    }

    /*************************************************************************
     \brief
        "This entity changed": any Inspector edit, Add Component or Remove
        Component.
     *************************************************************************/
    class EntityStateCommand final : public EditorCommand
    {
    public:
        EntityStateCommand(EntityKey entity, std::string before, std::string after, std::string label)
            : m_entity(entity)
            , m_before(std::move(before))
            , m_after(std::move(after))
            , m_label(std::move(label))
        {
        }

        void Undo(UndoHistory& history) override;
        void Redo(UndoHistory& history) override;

        EntityKey          Target() const override { return m_entity; }
        const std::string& Label()  const override { return m_label; }

    private:
        EntityKey   m_entity;
        std::string m_before;
        std::string m_after;
        std::string m_label;
    };

    /*************************************************************************
     \brief
        "This entity was created". Undo destroys it, Redo creates a new
        entity from the snapshot and rebinds the key to it.
     \note
        Created entities are roots. When the editor gains reparenting, a
        created-then-parented entity will need the parent recorded too.
     *************************************************************************/
    class CreateEntityCommand final : public EditorCommand
    {
    public:
        CreateEntityCommand(EntityKey entity, std::string snapshot, std::string label)
            : m_entity(entity)
            , m_snapshot(std::move(snapshot))
            , m_label(std::move(label))
        {
        }

        void Undo(UndoHistory& history) override;
        void Redo(UndoHistory& history) override;

        EntityKey          Target() const override { return m_entity; }
        const std::string& Label()  const override { return m_label; }

    private:
        EntityKey   m_entity;
        std::string m_snapshot;
        std::string m_label;
    };

}
