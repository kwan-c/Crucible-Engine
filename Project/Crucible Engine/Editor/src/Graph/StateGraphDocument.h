/*************************************************************************
* \file:         StateGraphDocument.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                StateGraphDocument: the state graph open in the editor,
*                with snapshot undo/redo and an "unsaved" marker -
*                BehaviorGraphDocument's twin.
*
* \details
*                Commit() stores the whole graph as its file text; undo
*                rebuilds it from the previous text. That covers the states'
*                embedded trees too, so editing a tree inside a state is
*                undone the same way as moving a state.
*
*                Undo and redo REBUILD the graph: drop any pointer into it
*                (a tree view's selection) whenever they return true. State
*                and arrow numbers survive, unless the undo removed them.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <memory>
#include <string>
#include <vector>

// Crucible engine
#include "AI/StateGraph.h"

namespace Crucible {

    class StateGraphDocument {

    public:

        StateGraphDocument();

        // A new graph with one empty "Idle" state. Counts as unsaved.
        void New();

        // Replaces the graph with a file's contents. On failure the old graph stays.
        bool Load(const std::vector<unsigned char>& bytes, std::string& error);

        std::string ToJson() const;
        StateGraph& Graph() { return *m_graph; }

        // Records the graph as one undo step, if it changed since the last.
        void Commit();

        bool CanUndo() const { return !m_undo.empty(); }
        bool CanRedo() const { return !m_redo.empty(); }
        bool Undo();
        bool Redo();

        bool Dirty() const { return m_current != m_saved; }
        void MarkSaved() { m_saved = m_current; }

    private:

        bool Restore(const std::string& json);

        std::unique_ptr<StateGraph> m_graph;
        std::string                 m_current;
        std::string                 m_saved;
        std::vector<std::string>    m_undo;
        std::vector<std::string>    m_redo;

    };  // STATEGRAPHDOCUMENT CLASS

}   // NAMESPACE CRUCIBLE
