/*************************************************************************
  \file       UndoHistory.h
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       1 October 2026
  \version    1.1
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
      - Play mode keeps the edit-mode history aside and brings it back at
        Stop, pointed at the restored entities; play-mode steps are dropped.

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
    - 1.1 (2 October 2026): BeginPlaySession / EndPlaySession for play mode.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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

        /*********************************************************************
         \brief  Every entity key this command refers to: its own, plus any
                 parents or group members.
         \details
            Play mode uses this to point each key at its entity again after
            Stop rebuilds the world. Default: just Target().
         *********************************************************************/
        virtual void CollectKeys(std::vector<EntityKey>& out) const { out.push_back(Target()); }

        /*********************************************************************
         \brief  The entities to select once this has run (several for a
                 multi-selection step). Default: just Target().
         *********************************************************************/
        virtual void SelectionKeys(std::vector<EntityKey>& out) const { out.push_back(Target()); }
    };

    /*************************************************************************
     \brief
        Several commands recorded as ONE undo step - an action on a
        multi-selection, e.g. "Delete 3 Entities" or a gizmo drag that moved
        several objects.
     \details
        Undo runs the parts newest-first, Redo oldest-first, so parts that
        depend on each other (sibling positions) replay in the same order
        they happened. Use Make() rather than the constructor: it returns
        nullptr for no parts and the part itself for exactly one.
     *************************************************************************/
    class GroupCommand final : public EditorCommand
    {
    public:
        GroupCommand(std::vector<std::unique_ptr<EditorCommand>> parts, std::string label)
            : m_parts(std::move(parts))
            , m_label(std::move(label))
        {
        }

        static std::unique_ptr<EditorCommand> Make(std::vector<std::unique_ptr<EditorCommand>> parts, std::string label)
        {
            if (parts.empty())     { return nullptr; }
            if (parts.size() == 1) { return std::move(parts.front()); }
            return std::make_unique<GroupCommand>(std::move(parts), std::move(label));
        }

        void Undo(UndoHistory& history) override
        {
            for (auto it = m_parts.rbegin(); it != m_parts.rend(); ++it) { (*it)->Undo(history); }
        }
        void Redo(UndoHistory& history) override
        {
            for (auto& part : m_parts) { part->Redo(history); }
        }

        //! The last part's entity: the "active" one of the selection.
        EntityKey          Target() const override { return m_parts.empty() ? kNullEntity : m_parts.back()->Target(); }
        const std::string& Label()  const override { return m_label; }

        void CollectKeys(std::vector<EntityKey>& out) const override
        {
            for (const auto& part : m_parts) { part->CollectKeys(out); }
        }
        void SelectionKeys(std::vector<EntityKey>& out) const override
        {
            for (const auto& part : m_parts) { part->SelectionKeys(out); }
        }

    private:
        std::vector<std::unique_ptr<EditorCommand>> m_parts;
        std::string                                 m_label;
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

        // ===================================================================
        //  Play mode
        // ===================================================================

        /*********************************************************************
         \brief
            Play was pressed: put the edit-mode history aside.
         \details
            Call after the Play snapshot was taken (it gives every live
            entity a scene guid). Each kept step's entity is remembered by
            that guid. While playing, the history starts empty and records
            only play-mode edits, which Stop throws away with the play world.
            Commands are matched to their entity through Target(), which is
            the one entity every current command edits.
         *********************************************************************/
        void BeginPlaySession();

        /*********************************************************************
         \brief
            Stop was pressed: drop the play-mode steps, bring the edit-mode
            history back.
         \param[in] worldRestored
            true if Stop rebuilt the world from the Play snapshot: every
            handle changed, so each kept step is pointed at its entity's new
            handle (found by guid). false: the world was not restored, so
            the old steps cannot be trusted and the history is cleared.
         *********************************************************************/
        void EndPlaySession(bool worldRestored);

        //! True between BeginPlaySession and EndPlaySession.
        bool InPlaySession() const { return m_stash.active; }

        /*********************************************************************
         \brief  Every entity the last Undo()/Redo() touched, resolved to
                 live handles (the whole group for a multi-selection step).
                 Undo()'s return value is the last of these. May hold dead
                 handles (undoing a Create destroys its entity).
         *********************************************************************/
        const std::vector<Entity>& LastSelection() const { return m_lastSelection; }

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

        std::vector<Entity> m_lastSelection;   // see LastSelection()
        void RememberSelection(const EditorCommand& command);

        std::deque<std::unique_ptr<EditorCommand>> m_undo;   // back() is newest
        std::deque<std::unique_ptr<EditorCommand>> m_redo;   // back() is next to redo

        // key -> current handle, and current handle -> key. Only entities that
        // have been recreated by a redo appear here; every other entity's key
        // is simply its own handle.
        std::unordered_map<Entity, Entity, EntityHash> m_keyToLive;
        std::unordered_map<Entity, Entity, EntityHash> m_liveToKey;

        // The edit-mode history while a play session runs, and the scene guid
        // of each kept step's entity at Play.
        struct PlayStash
        {
            bool active = false;
            std::deque<std::unique_ptr<EditorCommand>>     undo;
            std::deque<std::unique_ptr<EditorCommand>>     redo;
            std::unordered_map<Entity, Entity, EntityHash> keyToLive;
            std::unordered_map<Entity, Entity, EntityHash> liveToKey;
            std::unordered_map<Entity, std::uint64_t, EntityHash> guidOfKey;
        };
        PlayStash m_stash;
    };

}
