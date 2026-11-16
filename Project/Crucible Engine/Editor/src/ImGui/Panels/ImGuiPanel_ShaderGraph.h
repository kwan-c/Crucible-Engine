/*!
* @file			ImGuiPanel_ShaderGraph.h
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		The Shader Graph window: build a mesh shader out of nodes instead of code.
*
* Laid out like the State Graph window, which it shares its canvas with:
*   - left, the graph's PARAMETERS - the values each material sets for itself (a colour, a
*     speed, a texture). Add, rename, retype, reorder and delete them here; double-click one
*     to put a node that reads it on the canvas;
*   - middle, the GRAPH (ShaderGraph/ShaderGraphView.h): nodes, pins and links, ending in the
*     Output node - the surface;
*   - right, the selected NODE: the value of each input that has nothing linked to it, and
*     what is wrong with the node, if anything. With nothing selected: the graph's problems.
*
* A floating window: View > Shader Graph, double-click a .shadergraph in the Asset Browser, or
* Edit beside a material's Shader in the Inspector. Read-only during Play. Closing only hides it.
*
* SAVE IS WHAT UPDATES THE SCENE. The window edits the graph in memory; Save writes the
* .shadergraph, and the Editor's shader hot reload (Shaders/ShaderHotReload.h) sees the file
* change, generates the shader, compiles it and swaps it in on every material using it. A
* graph saved with a problem in it keeps drawing with its last good shader.
*
* Undo and redo cover everything done in the window (ShaderGraph/ShaderGraphDocument.h).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#pragma once

#include "ImGui/ImGuiManager.h"
#include "ShaderGraph/ShaderGraphDocument.h"
#include "ShaderGraph/ShaderGraphView.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Crucible {

	class Core;

	class ShaderGraphPanel final : public EditorPanel {
	public:
		explicit ShaderGraphPanel(Core& core) : m_core(core) {}

		void Init() override;
		void Update(float) override;

		void Show()             { m_visible = true; m_makeFocus = true; }
		bool IsOpen() const     { return m_visible; }
		void SetOpen(bool open) { m_visible = open; }

		/// Opens a .shadergraph by asset id (the Asset Browser's double-click, the Inspector's Edit).
		void RequestOpen(std::uint64_t asset);

	private:
		enum class Pending { None, New, Open };

		void NewGraph();
		bool Load(std::uint64_t asset);
		bool Save();
		bool SaveAs(std::string path);
		void Guard(Pending action, std::uint64_t asset = 0);   // asks first if there are unsaved changes
		void AfterRebuild();                                   // after undo, redo or load: the graph is a new one

		void DrawToolbar(bool readOnly);
		bool DrawParameters(bool readOnly);   // the left list; true if the graph changed
		bool DrawDetails(bool readOnly);      // the right side; true if the graph changed
		void DrawDialogs();

		/// What the canvas shows on the nodes: the generator's problems with the graph as it is
		/// now, and the compiler's errors with the shader that was last saved.
		std::vector<ShaderGraph::Problem> ShownProblems();

		std::string ShaderName() const;                        // the name materials use for the open graph ("" if not saved yet)
		std::string AssetPath(std::uint64_t asset) const;      // "" if unknown
		void SetMessage(std::string text, bool error) { m_message = std::move(text); m_messageIsError = error; }

		Core&                  m_core;
		ShaderGraph::Document  m_document;
		ShaderGraphView        m_view;

		std::uint64_t          m_asset = 0;             // the open graph's asset id (0 = not saved yet)
		bool                   m_hasDocument = false;
		bool                   m_visible = false;
		bool                   m_pendingCommit = false; // something changed: an undo step is due when the gesture ends
		std::string            m_message;
		bool                   m_messageIsError = false;
		std::vector<uint32_t>  m_savedLines;            // the saved shader's lines -> nodes, for the compiler's errors

		int                    m_parameter = -1;        // the parameter selected in the left list
		std::string            m_nameBuffer;            // its name, as being typed
		std::string            m_nameShown;             // the parameter name m_nameBuffer was last filled from
		bool                   m_nameEditing = false;

		Pending                m_pending = Pending::None;
		std::uint64_t          m_pendingAsset = 0;
		bool                   m_openPicker = false;
		bool                   m_openSaveAs = false;
		bool                   m_openGuard = false;
		char                   m_savePath[512] = "ShaderGraphs/New Shader Graph.shadergraph";
	};

}
