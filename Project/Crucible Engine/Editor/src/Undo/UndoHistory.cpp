/*************************************************************************
  \file       UndoHistory.cpp
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       1 October 2026
  \version    1.1
  \brief
    Undo/redo stack implementation.
  \details
    Two deques of commands and a pair of maps for entity identity. The
    stacks only ever move commands between each other; nothing here knows
    what a command actually does.
  \dependencies
    UndoHistory.h, Scene/SceneManager.h, Diagnostics/Logger.h
  \see
    UndoHistory.h, EntityCommands.cpp
  \history
    - 1.0 (1 October 2026): Initial version.
    - 1.1 (2 October 2026): BeginPlaySession / EndPlaySession for play mode.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#include "Undo/UndoHistory.h"

#include "Diagnostics/Logger.h"
#include "Scene/SceneManager.h"

namespace Crucible {

    namespace {
        const std::string kEmptyLabel;
    }

    void UndoHistory::Init(World& world, SceneManager& scenes)
    {
        m_world  = &world;
        m_scenes = &scenes;

        // New and Load both rebuild the world from scratch, so every handle
        // and key the history holds is meaningless afterwards. SceneManager
        // only notifies on success - a failed load leaves the world, and so
        // the history, intact.
        scenes.OnSceneChanged([this] { Clear(); });
    }

    void UndoHistory::Push(std::unique_ptr<EditorCommand> command)
    {
        if (!command)
        {
            return;
        }

        // A new action makes the old "future" unreachable.
        m_redo.clear();

        m_undo.push_back(std::move(command));
        while (m_undo.size() > kMaxSteps)
        {
            m_undo.pop_front();
        }
    }

    Entity UndoHistory::Undo()
    {
        if (m_undo.empty() || m_world == nullptr)
        {
            return kNullEntity;
        }

        std::unique_ptr<EditorCommand> command = std::move(m_undo.back());
        m_undo.pop_back();

        command->Undo(*this);
        CRU_USER_INFO("Undo: {}", command->Label());

        RememberSelection(*command);
        const Entity select = Resolve(command->Target());
        m_redo.push_back(std::move(command));

        if (m_scenes != nullptr)
        {
            m_scenes->MarkDirty();
        }
        return select;
    }

    Entity UndoHistory::Redo()
    {
        if (m_redo.empty() || m_world == nullptr)
        {
            return kNullEntity;
        }

        std::unique_ptr<EditorCommand> command = std::move(m_redo.back());
        m_redo.pop_back();

        command->Redo(*this);
        CRU_USER_INFO("Redo: {}", command->Label());

        RememberSelection(*command);
        const Entity select = Resolve(command->Target());

        // Not Push(): that would clear the rest of the redo stack.
        m_undo.push_back(std::move(command));

        if (m_scenes != nullptr)
        {
            m_scenes->MarkDirty();
        }
        return select;
    }

    void UndoHistory::RememberSelection(const EditorCommand& command)
    {
        std::vector<EntityKey> keys;
        command.SelectionKeys(keys);

        m_lastSelection.clear();
        for (const EntityKey key : keys)
        {
            if (key.IsValid()) { m_lastSelection.push_back(Resolve(key)); }
        }
    }

    void UndoHistory::Clear()
    {
        m_lastSelection.clear();
        m_undo.clear();
        m_redo.clear();
        m_keyToLive.clear();
        m_liveToKey.clear();
    }

    // =======================================================================
    //  Play mode
    // =======================================================================

    void UndoHistory::BeginPlaySession()
    {
        if (m_stash.active || m_scenes == nullptr)
        {
            return;
        }

        // Stop rebuilds the world, so handles will not survive it; scene guids
        // will. The Play snapshot has just given every live entity one.
        m_stash.guidOfKey.clear();
        const Scene& scene = m_scenes->Current();
        const auto remember = [&](const std::deque<std::unique_ptr<EditorCommand>>& commands)
        {
            std::vector<EntityKey> keys;
            for (const auto& command : commands)
            {
                // Every key the step uses (a group's members, a reparent's
                // parents...), not just its Target().
                keys.clear();
                command->CollectKeys(keys);
                for (const EntityKey key : keys)
                {
                    if (!key.IsValid())
                    {
                        continue;
                    }

                    // An entity that is currently undone (dead) has no guid; its
                    // redo recreates it and rebinds the key anyway.
                    const xresource::instance_guid id = scene.IdOf(Resolve(key));
                    if (id.m_Value != 0)
                    {
                        m_stash.guidOfKey.emplace(key, id.m_Value);
                    }
                }
            }
        };
        remember(m_undo);
        remember(m_redo);

        m_stash.undo      = std::move(m_undo);
        m_stash.redo      = std::move(m_redo);
        m_stash.keyToLive = std::move(m_keyToLive);
        m_stash.liveToKey = std::move(m_liveToKey);
        m_stash.active    = true;
        Clear();   // moved-from containers: make them definitely empty
    }

    void UndoHistory::EndPlaySession(bool worldRestored)
    {
        if (!m_stash.active)
        {
            return;
        }

        // Play-mode steps edited the play world, which is gone.
        m_undo      = std::move(m_stash.undo);
        m_redo      = std::move(m_stash.redo);
        m_keyToLive = std::move(m_stash.keyToLive);
        m_liveToKey = std::move(m_stash.liveToKey);
        const auto guidOfKey = std::move(m_stash.guidOfKey);
        m_stash = PlayStash{};

        if (!worldRestored || m_scenes == nullptr)
        {
            Clear();
            return;
        }

        // Point every kept step at its entity's handle in the restored world.
        const Scene& scene = m_scenes->Current();
        for (const auto& [key, guid] : guidOfKey)
        {
            const Entity live = scene.EntityOf(xresource::instance_guid{ guid });
            if (live.IsValid())
            {
                Rebind(key, live);
            }
        }
    }

    const std::string& UndoHistory::UndoLabel() const
    {
        return m_undo.empty() ? kEmptyLabel : m_undo.back()->Label();
    }

    const std::string& UndoHistory::RedoLabel() const
    {
        return m_redo.empty() ? kEmptyLabel : m_redo.back()->Label();
    }

    // =======================================================================
    //  Entity identity
    // =======================================================================

    EntityKey UndoHistory::KeyOf(Entity live) const
    {
        const auto found = m_liveToKey.find(live);
        return (found == m_liveToKey.end()) ? live : found->second;
    }

    Entity UndoHistory::Resolve(EntityKey key) const
    {
        const auto found = m_keyToLive.find(key);
        return (found == m_keyToLive.end()) ? key : found->second;
    }

    void UndoHistory::Rebind(EntityKey key, Entity live)
    {
        // Drop the reverse entry for wherever this key lived before, so a
        // dead handle can never be mistaken for the key again.
        const auto previous = m_keyToLive.find(key);
        if (previous != m_keyToLive.end())
        {
            m_liveToKey.erase(previous->second);
        }

        m_keyToLive[key]  = live;
        m_liveToKey[live] = key;
    }

}
