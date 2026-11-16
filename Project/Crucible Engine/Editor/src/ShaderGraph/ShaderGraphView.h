/*!
* @file			ShaderGraphView.h
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		The shader graph on a canvas: draws the nodes, their pins and the links, and
*				turns the mouse and keyboard into edits.
*
* Built on the team's GraphCanvas (Graph/GraphCanvas.h - grid, pan, zoom, node boxes), so the
* Shader Graph window moves and looks like the State Graph and Behavior Graph windows:
* middle-drag pans, the wheel zooms. What is different here is that a node has several named
* pins down its sides - inputs on the left, outputs on the right - and data runs left to right.
*
*   - Drag from a pin to a pin of the other kind to link them. Dropped on a node's body, the
*     link takes the first input (or output) that fits. Dropped on empty canvas, the add
*     menu opens and the new node is linked.
*   - Drag a link off an input pin to move it elsewhere or, dropped on nothing, remove it.
*   - Click a node to select it; Shift+click adds or removes one; drag on empty canvas to
*     select everything in a box. Dragging a selected node moves them all.
*   - Right-click the canvas for the add menu (type to search), a node or link for its menu.
*   - Delete, Ctrl+C, Ctrl+V, Ctrl+D (duplicate), Ctrl+A.
*
* A pin's colour is the type it carries (as worked out by the generator: ShaderGraph::
* Generated::nodes). A node with a problem has a red outline (amber if the Output does not use
* the node) and says what is wrong when the mouse is over it. A node the Output does not use
* is drawn faded.
*
* The view changes the graph only through ShaderGraphEdit.h, plus node positions. It keeps no
* pointer into the graph between frames: selection is by node id, so it survives undo.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include "Graph/GraphCanvas.h"
#include "ShaderGraph/ShaderGraph.h"
#include "ShaderGraph/ShaderGraphEdit.h"
#include "ShaderGraph/ShaderGraphGenerator.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Crucible {

	class ShaderGraphView {
	public:
		struct Result {
			bool        changed = false;   ///< the graph was edited this frame
			std::string refused;           ///< a link was refused this frame: why
		};

		/*********************************************************************
		 \brief  Draws the graph over the rest of the current window and
		         handles this frame's input.
		 \param  check     What the generator makes of the graph now: pin
		                   types, and which nodes the Output uses.
		 \param  problems  What to show on the nodes: the generator's
		                   problems, and any the caller adds (compiler errors).
		 \param  readOnly  Look, pan and select only.
		 *********************************************************************/
		Result Draw(ShaderGraph::Graph& graph, const ShaderGraph::Generated& check,
		            const std::vector<ShaderGraph::Problem>& problems, bool readOnly);

		const std::vector<uint32_t>& SelectedNodes() const { return m_selected; }
		uint32_t SelectedNode() const { return m_selected.size() == 1 ? m_selected.front() : 0; }   ///< the one selected node, or 0
		void Select(uint32_t node);
		void ClearSelection();
		void KeepSelectionIn(const ShaderGraph::Graph& graph);   ///< after undo or redo: forget what is no longer there

		/// Pans and zooms so the whole graph is in view (from the next frame).
		void FrameAll(const ShaderGraph::Graph& graph);

		/// Mid-gesture (moving nodes, dragging a link, a selection box): wait before an undo step.
		bool IsBusy() const { return m_dragging || m_linking || m_boxSelecting; }

		/// The middle of what the canvas showed last frame, in graph space: where a node added
		/// from outside the canvas should go.
		Math::vec2 ViewCentre() const { return m_viewCentre; }

	private:
		bool IsSelected(uint32_t node) const;
		bool DrawMenus(ShaderGraph::Graph& graph, Result& result);   // true if the graph changed
		bool LinkNewNode(ShaderGraph::Graph& graph, uint32_t node);  // the pending link, onto a node's first pin that fits
		void Duplicate(ShaderGraph::Graph& graph);

		GraphCanvas           m_canvas;
		Math::vec2            m_viewCentre{ 0.0f };

		std::vector<uint32_t> m_selected;            // node ids, the last one clicked at the back
		bool                  m_hasLink = false;     // a link is selected (then no node is)
		ShaderGraph::Link     m_link;

		bool                  m_dragging = false;    // moving the selected nodes
		bool                  m_moved = false;
		bool                  m_boxSelecting = false;
		Math::vec2            m_boxStart{ 0.0f };

		// A link being dragged: one end is fixed on a pin, the other follows the mouse.
		bool                  m_linking = false;
		bool                  m_linkFromOutput = true;   // the fixed end is an output pin
		uint32_t              m_linkNode = 0;
		std::string           m_linkPin;
		bool                  m_linkDetached = false;    // it was pulled off an input: the graph has already changed

		// The menus.
		Math::vec2            m_menuPosition{ 0.0f };    // where the add menu puts a node
		bool                  m_menuLinks = false;       // the add menu was opened by dropping a link: link the new node
		char                  m_filter[64] = {};

		ShaderGraph::Clipboard m_clipboard;
	};

}
