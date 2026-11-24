/*!
* @file			ShaderGraphDocument.cpp
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		Implementation - see ShaderGraphDocument.h.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "ShaderGraph/ShaderGraphDocument.h"

#include "ShaderGraph/ShaderGraphEdit.h"

#include <utility>

namespace Crucible::ShaderGraph {

	Document::Document() {
		m_graph = NewGraph();
		m_current = Save(m_graph);
		m_saved = m_current;   // nothing to lose yet
	}

	void Document::New(bool subgraph) {
		m_graph = subgraph ? NewSubgraph() : NewGraph();
		m_current = Save(m_graph);
		m_saved.clear();       // not in any file
		m_undo.clear();
		m_redo.clear();
		m_checked = false;
	}

	bool Document::Load(std::string_view json, std::string& error) {
		Graph loaded;
		if (!ShaderGraph::Load(json, loaded, error)) { return false; }
		m_graph = std::move(loaded);
		m_current = Save(m_graph);
		// Dirty() compares with what Save() writes, not with the file's own bytes: a file
		// written by hand, or by an older Editor, is not "changed" just by being opened.
		m_saved = m_current;
		m_undo.clear();
		m_redo.clear();
		m_checked = false;
		return true;
	}

	void Document::Commit() {
		std::string text = Save(m_graph);
		m_checked = false;
		if (text == m_current) { return; }
		m_undo.push_back(std::move(m_current));
		if (m_undo.size() > kMaxUndo) { m_undo.erase(m_undo.begin()); }
		m_redo.clear();
		m_current = std::move(text);
	}

	void Document::Restore(const std::string& text) {
		Graph graph;
		std::string error;
		if (ShaderGraph::Load(text, graph, error)) { m_graph = std::move(graph); }   // its own Save(): always loads
		m_current = text;
		m_checked = false;
	}

	bool Document::Undo() {
		if (m_undo.empty()) { return false; }
		// Anything not committed yet (half a drag) is dropped: the step before the last
		// committed one is what comes back.
		m_redo.push_back(std::move(m_current));
		const std::string text = std::move(m_undo.back());
		m_undo.pop_back();
		Restore(text);
		return true;
	}

	bool Document::Redo() {
		if (m_redo.empty()) { return false; }
		m_undo.push_back(std::move(m_current));
		const std::string text = std::move(m_redo.back());
		m_redo.pop_back();
		Restore(text);
		return true;
	}

	const Generated& Document::Check() {
		if (!m_checked) {
			m_check = Generate(m_graph);
			m_checked = true;
		}
		return m_check;
	}

}
