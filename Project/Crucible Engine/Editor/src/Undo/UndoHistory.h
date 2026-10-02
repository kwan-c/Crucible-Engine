/*************************************************************************
  \file       UndoHistory.h
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       1 October 2026
  \version    1.0
  \brief
    The editor's undo/redo stack.
  \details
    Every undoable editor action is an EditorCommand pushed here AFTER it
    has already been applied. Undo() reverts the newest one, Redo()
    re-applies the newest undone one.

    Rules, the same as every editor people already know:
      - At most kMaxSteps (20) undo steps. The oldest is dropped.
      - Doing anything new throws away everything that could be redone.
      - Opening or creating a scene clears the history: the old commands
        refer to entities that no longer exist.

    Entity identity. Undoing "Create Cube" destroys the cube; redoing it
    creates a NEW entity with a different handle. Any later command that
    edited that cube still holds the old handle. To keep those working,
    commands never store a live handle - they store a key (the handle the
    entity had when the history first saw it), and the history keeps the
    key -> current-handle mapping up to date. Commands go through KeyOf()
    when recording and Resolve() when applying, and never touch a raw
    handle across steps.
  \dependencies
    ECS/Core/ECSCore.h, <deque>, <memory>, <string>, <unordered_map>
  \see
    UndoHistory.cpp, EntityCommands.h, ImGui/ImGuiManager.h
  \history
    - 1.0 (1 October 2026): Initial version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>

#include "ECS/Core/ECSCore.h"   // Entity, kNullEntity

namespace Crucible {

    class World;
    class SceneManager;
    class UndoHistory;

    /*************************************************************************
     \brief
        A stable name for an entity across undo/redo.
     \details
        The handle the entity had when the history first recorded it. Never
        dereference one directly - always go through UndoHistory::Resolve().
     *************************************************************************/
    using EntityKey = Entity;

    /*************************************************************************
     \brief
        One undoable editor action.

     \details
        Implementations capture whatever they need to go both ways at the
        moment the action happens, then do nothing until asked. A command
        is pushed AFTER its action has been applied, so the first call it
        ever receives is Undo().
     *************************************************************************/
    class EditorCommand
    {
    public:
        virtual ~EditorCommand() = default;

        /*********************************************************************
         \brief  Revert the action.
         *********************************************************************/
        virtual void Undo(UndoHistory& history) = 0;

        /*********************************************************************
         \brief  Re-apply the action after an Undo.
         *********************************************************************/
        virtual void Redo(UndoHistory& history) = 0;

        /*********************************************************************
         \brief  The entity the editor should select once this has run.
         \return A key; the history resolves it. kNullEntity for none.
         *********************************************************************/
        virtual EntityKey Target() const = 0;

        /*********************************************************************
         \brief  Short description for the Edit menu: "Edit Transform".
         *********************************************************************/
        virtual const std::string& Label() const = 0;
    };

    /*************************************************************************
     \brief
        The undo and redo stacks, plus entity-key bookkeeping.

     \par Usage
     \code
        // after the action has happened:
        history.Push(std::make_unique<EntityStateCommand>(
            history.KeyOf(entity), before, after, "Edit Transform"));

        // Ctrl+Z
        const Entity select = history.Undo();
     \endcode
     *************************************************************************/
    class UndoHistory
    {
    public:
        //! Undo steps kept. Older ones are discarded.
        static constexpr std::size_t kMaxSteps = 20;

        /*********************************************************************
         \brief
            Bind to the world being edited.

         \details
            Also subscribes to SceneManager::OnSceneChanged, so loading or
            creating a scene clears the history automatically. Call once.
         *********************************************************************/
        void Init(World& world, SceneManager& scenes);

        /*********************************************************************
         \brief
            Record an action that has ALREADY been applied.

         \details
            Clears the redo stack, and drops the oldest undo step once there
            are more than kMaxSteps.
         *********************************************************************/
        void Push(std::unique_ptr<EditorCommand> command);

        /*********************************************************************
         \brief  Revert the newest action.
         \return The entity to select afterwards. May be kNullEntity, or no
                 longer alive (undoing a Create destroys its entity).
         *********************************************************************/
        Entity Undo();

        /*********************************************************************
         \brief  Re-apply the newest undone action.
         \return The entity to select afterwards; see Undo().
         *********************************************************************/
        Entity Redo();

        /*********************************************************************
         \brief  Forget everything. Called automatically on scene change.
         *********************************************************************/
        void Clear();

        bool CanUndo() const { return !m_undo.empty(); }
        bool CanRedo() const { return !m_redo.empty(); }

        std::size_t UndoCount() const { return m_undo.size(); }
        std::size_t RedoCount() const { return m_redo.size(); }

        /*********************************************************************
         \brief  Label of the action Undo() would revert, or "" if none.
         *********************************************************************/
        const std::string& UndoLabel() const;

        /*********************************************************************
         \brief  Label of the action Redo() would re-apply, or "" if none.
         *********************************************************************/
        const std::string& RedoLabel() const;

        // ===================================================================
        //  Entity identity - for commands
        // ===================================================================

        /*********************************************************************
         \brief  The stable key for a live entity. Use when recording.
         *********************************************************************/
        EntityKey KeyOf(Entity live) const;

        /*********************************************************************
         \brief  The current handle for a key. Use when applying.
         \return The handle; may be dead if the entity is currently undone.
         *********************************************************************/
        Entity Resolve(EntityKey key) const;

        /*********************************************************************
         \brief
            Tell the history that a key now lives at a new handle.
         \details
            For commands that recreate an entity (redo of Create).
         *********************************************************************/
        void Rebind(EntityKey key, Entity live);

        /*********************************************************************
         \brief  The world commands operate on. Valid after Init().
         *********************************************************************/
        World& GetWorld() const { return *m_world; }

    private:
        struct EntityHash
        {
            std::size_t operator()(Entity entity) const noexcept
            {
                return (static_cast<std::size_t>(entity.generation) << 32)
                     ^  static_cast<std::size_t>(entity.index);
            }
        };

        World*        m_world  = nullptr;
        SceneManager* m_scenes = nullptr;

        std::deque<std::unique_ptr<EditorCommand>> m_undo;   // back() is newest
        std::deque<std::unique_ptr<EditorCommand>> m_redo;   // back() is next to redo

        // key -> current handle, and current handle -> key. Only entities that
        // have been recreated by a redo appear here; every other entity's key
        // is simply its own handle.
        std::unordered_map<Entity, Entity, EntityHash> m_keyToLive;
        std::unordered_map<Entity, Entity, EntityHash> m_liveToKey;
    };

}
