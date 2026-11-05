/*************************************************************************
* \file:         StateGraphDocument.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                Snapshot undo for the state graph editor.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "StateGraphDocument.h"

// STL
#include <span>

// Crucible engine
#include "AI/StateGraphIO.h"

// Crucible editor
#include "Graph/GraphAssets.h"

namespace Crucible {

    namespace {

        constexpr std::size_t kMaxUndoSteps = 200;

        std::span<const unsigned char> BytesOf(const std::string& text) {

            return { reinterpret_cast<const unsigned char*>(text.data()), text.size() };

        }   // BytesOf()

    }   // ANONYMOUS NAMESPACE

    StateGraphDocument::StateGraphDocument() : m_graph(std::make_unique<StateGraph>()) {}

    void StateGraphDocument::New() {

        // Exactly what Asset Browser > Create State Graph writes, so both start the same.
        Restore(GraphAssets::NewStateGraphJson());
        m_saved.clear();
        m_undo.clear();
        m_redo.clear();

    }   // StateGraphDocument::New()

    bool StateGraphDocument::Load(const std::vector<unsigned char>& bytes, std::string& error) {

        StateGraphResult result = DecodeStateGraph(std::span<const unsigned char>(bytes.data(), bytes.size()));
        if (!result.ok) { error = result.error; return false; }

        m_graph   = std::move(result.graph);
        m_current = EncodeStateGraph(*m_graph);   // re-encoded, so formatting can't fake "unsaved"
        m_saved   = m_current;
        m_undo.clear();
        m_redo.clear();
        return true;

    }   // StateGraphDocument::Load()

    std::string StateGraphDocument::ToJson() const {

        return EncodeStateGraph(*m_graph);

    }   // StateGraphDocument::ToJson()

    void StateGraphDocument::Commit() {

        std::string now = EncodeStateGraph(*m_graph);
        if (now == m_current) return;

        m_undo.push_back(std::move(m_current));
        if (m_undo.size() > kMaxUndoSteps) m_undo.erase(m_undo.begin());
        m_current = std::move(now);
        m_redo.clear();

    }   // StateGraphDocument::Commit()

    bool StateGraphDocument::Undo() {

        if (m_undo.empty()) return false;
        m_redo.push_back(m_current);
        Restore(m_undo.back());
        m_undo.pop_back();
        return true;

    }   // StateGraphDocument::Undo()

    bool StateGraphDocument::Redo() {

        if (m_redo.empty()) return false;
        m_undo.push_back(m_current);
        Restore(m_redo.back());
        m_redo.pop_back();
        return true;

    }   // StateGraphDocument::Redo()

    bool StateGraphDocument::Restore(const std::string& json) {

        StateGraphResult result = DecodeStateGraph(BytesOf(json));
        if (!result.ok) return false;
        m_graph   = std::move(result.graph);
        m_current = EncodeStateGraph(*m_graph);
        return true;

    }   // StateGraphDocument::Restore()

}   // NAMESPACE CRUCIBLE
