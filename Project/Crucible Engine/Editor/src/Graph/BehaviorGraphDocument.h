/*************************************************************************
* \file:         BehaviorGraphDocument.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                BehaviorGraphDocument: the behaviour graph open in the
*                editor, with snapshot undo/redo and an "unsaved" marker.
*
* \details
*                Undo works by snapshots: after an edit, Commit() stores the
*                whole graph as its file text (EncodeBehaviorGraph). Undo
*                rebuilds the graph from the previous text. It catches every
*                kind of edit with no per-edit undo code, at a few KB a step.
*
*                Undo and redo REBUILD the graph, so any BehaviorNode*
*                pointer the editor held is gone afterwards - clear the
*                selection whenever they return true.
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
#include "AI/BehaviorGraph.h"

namespace Crucible {

    class BehaviorGraphDocument {

    public:

        BehaviorGraphDocument();

        // A new graph with just an On Start root. Counts as unsaved.
        void New();

        // Replaces the graph with a file's contents. On failure the old graph stays.
        bool Load(const std::vector<unsigned char>& bytes, std::string& error);

        // The graph as file text, ready to save.
        std::string ToJson() const;

        BehaviorGraph& Graph() { return *m_graph; }

        // Records the graph as it is now as one undo step - if it changed since
        // the last one (so calling it with nothing changed is free of side effects).
        void Commit();

        bool CanUndo() const { return !m_undo.empty(); }
        bool CanRedo() const { return !m_redo.empty(); }
        bool Undo();
        bool Redo();

        // Different from the last save (or never saved).
        bool Dirty() const { return m_current != m_saved; }
        void MarkSaved() { m_saved = m_current; }

    private:

        void Restore(const std::string& json);

        std::unique_ptr<BehaviorGraph> m_graph;
        std::string                    m_current;   // the graph's text as of the last Commit
        std::string                    m_saved;     // its text as of the last save ("" = never saved)
        std::vector<std::string>       m_undo;
        std::vector<std::string>       m_redo;

    };  // BEHAVIORGRAPHDOCUMENT CLASS

}   // NAMESPACE CRUCIBLE
