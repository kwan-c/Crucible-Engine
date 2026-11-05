/*************************************************************************
* \file:         BehaviorGraphDocument.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                Snapshot undo for the behaviour graph editor.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "BehaviorGraphDocument.h"

// STL
#include <span>

// Crucible engine
#include "AI/BehaviorGraphIO.h"

namespace Crucible {

    namespace {

        // Enough to undo a long session of edits; each step is only a few KB.
        constexpr std::size_t kMaxUndoSteps = 200;

    }   // ANONYMOUS NAMESPACE

    BehaviorGraphDocument::BehaviorGraphDocument() : m_graph(std::make_unique<BehaviorGraph>()) {}

    void BehaviorGraphDocument::New() {

        m_graph = std::make_unique<BehaviorGraph>();
        m_graph->SetRoot(MakeNode("start"));   // On Start: every graph begins with one

        m_current = EncodeBehaviorGraph(*m_graph);
        m_saved.clear();    // never saved: Dirty until the first Save As
        m_undo.clear();
        m_redo.clear();

    }   // BehaviorGraphDocument::New()

    bool BehaviorGraphDocument::Load(const std::vector<unsigned char>& bytes, std::string& error) {

        BehaviorGraphResult result = DecodeBehaviorGraph(std::span<const unsigned char>(bytes.data(), bytes.size()));
        if (!result.ok) { error = result.error; return false; }

        m_graph = std::move(result.graph);
        // Re-encoded rather than kept as read, so the "unsaved" comparison isn't
        // fooled by formatting differences between the file and our writer.
        m_current = EncodeBehaviorGraph(*m_graph);
        m_saved   = m_current;
        m_undo.clear();
        m_redo.clear();
        return true;

    }   // BehaviorGraphDocument::Load()

    std::string BehaviorGraphDocument::ToJson() const {

        return EncodeBehaviorGraph(*m_graph);

    }   // BehaviorGraphDocument::ToJson()

    void BehaviorGraphDocument::Commit() {

        std::string now = EncodeBehaviorGraph(*m_graph);
        if (now == m_current) return;

        m_undo.push_back(std::move(m_current));
        if (m_undo.size() > kMaxUndoSteps) m_undo.erase(m_undo.begin());
        m_current = std::move(now);
        m_redo.clear();     // a new edit after undoing starts a new history branch

    }   // BehaviorGraphDocument::Commit()

    bool BehaviorGraphDocument::Undo() {

        if (m_undo.empty()) return false;
        m_redo.push_back(m_current);
        Restore(m_undo.back());
        m_undo.pop_back();
        return true;

    }   // BehaviorGraphDocument::Undo()

    bool BehaviorGraphDocument::Redo() {

        if (m_redo.empty()) return false;
        m_undo.push_back(m_current);
        Restore(m_redo.back());
        m_redo.pop_back();
        return true;

    }   // BehaviorGraphDocument::Redo()

    void BehaviorGraphDocument::Restore(const std::string& json) {

        // Our own writer produced this text, so it always reads back; the check
        // is only so a surprise can never leave the editor with no graph.
        BehaviorGraphResult result = DecodeBehaviorGraph(std::span<const unsigned char>(reinterpret_cast<const unsigned char*>(json.data()), json.size()));
        if (!result.ok) return;
        m_graph   = std::move(result.graph);
        m_current = json;

    }   // BehaviorGraphDocument::Restore()

}   // NAMESPACE CRUCIBLE
