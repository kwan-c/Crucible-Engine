/*!
* @file			ShaderGraphDocument.h
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		The shader graph being edited: the graph, its undo history, whether it has
*				unsaved changes, and what the generator makes of it right now.
*
* Undo works the way the State Graph and Behavior Graph documents do it (Graph/
* StateGraphDocument.h): a step is the whole graph as its file text. Commit() records one
* when the text has changed since the last; Undo() and Redo() rebuild the graph from a
* recorded text. A graph's text is small and Save() always writes the same graph the same
* way, so this is cheap and cannot go out of step with the graph - which a list of "do" and
* "undo" actions per kind of edit could.
*
* Undo and redo REPLACE the graph: nodes keep their ids, but anything pointing into the old
* graph is gone. Selections are by id for that reason.
*
* No ImGui and no engine here either, so it is tested with the rest (Tools/ShaderGraphTests).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include "ShaderGraph/ShaderGraph.h"
#include "ShaderGraph/ShaderGraphGenerator.h"

#include <string>
#include <string_view>
#include <vector>

namespace Crucible::ShaderGraph {

	class Document {
	public:
		Document();   ///< a new graph (one Output node), with nothing to undo and nothing to save

		/// Starts over with a new graph. Counts as unsaved.
		void New();

		/// Replaces the graph with a file's contents. On failure (false, and why) the old graph stays.
		bool Load(std::string_view json, std::string& error);

		Graph&       GetGraph() { return m_graph; }
		const Graph& GetGraph() const { return m_graph; }

		/// The graph as the text of its file, as of the last Commit(), Undo(), Redo() or Load().
		const std::string& Text() const { return m_current; }

		/// Records the graph as one undo step, if it has changed since the last. Call when an
		/// edit is finished (a drag released, a value typed), not on every frame of it.
		void Commit();

		bool CanUndo() const { return !m_undo.empty(); }
		bool CanRedo() const { return !m_redo.empty(); }
		bool Undo();   ///< false if there was nothing to undo
		bool Redo();

		bool Dirty() const { return m_current != m_saved; }   ///< changed since it was loaded or saved
		void MarkSaved() { m_saved = m_current; }

		/// Say that the graph was changed by hand (through GetGraph()), so Check() looks again.
		void Touch() { m_checked = false; }

		/// What the generator makes of the graph as it is now: its problems, each pin's type,
		/// and the shader if nothing stops it. Worked out again only after Touch(), Commit(),
		/// Undo(), Redo(), New() or Load().
		const Generated& Check();

	private:
		static constexpr size_t kMaxUndo = 200;   // steps kept; the oldest go first

		void Restore(const std::string& text);

		Graph                    m_graph;
		std::string              m_current;   // Save(m_graph) as of the last step
		std::string              m_saved;     // the text that is in the file ("" = never saved)
		std::vector<std::string> m_undo;      // older texts, the newest at the back
		std::vector<std::string> m_redo;
		Generated                m_check;
		bool                     m_checked = false;
	};

}
