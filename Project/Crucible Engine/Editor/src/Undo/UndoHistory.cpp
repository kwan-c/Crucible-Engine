/*************************************************************************
  \file       UndoHistory.cpp
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       1 October 2026
  \version    1.0
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

        const Entity select = Resolve(command->Target());

        // Not Push(): that would clear the rest of the redo stack.
        m_undo.push_back(std::move(command));

        if (m_scenes != nullptr)
        {
            m_scenes->MarkDirty();
        }
        return select;
    }

    void UndoHistory::Clear()
    {
        m_undo.clear();
        m_redo.clear();
        m_keyToLive.clear();
        m_liveToKey.clear();
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
