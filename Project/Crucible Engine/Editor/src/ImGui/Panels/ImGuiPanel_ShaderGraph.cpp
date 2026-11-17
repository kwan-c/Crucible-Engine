/*!
* @file			ImGuiPanel_ShaderGraph.cpp
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		Implementation - see ImGuiPanel_ShaderGraph.h. The window, its toolbar and
*				dialogs follow ImGuiPanel_StateGraph.cpp, so the two behave alike.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/
#include "ImGui/Panels/ImGuiPanel_ShaderGraph.h"

#include "ImGui/Panels/ImGuiPanel_AssetBrowser.h"
#include "ShaderGraph/ShaderGraphAsset.h"
#include "ShaderGraph/ShaderGraphEdit.h"
#include "ShaderGraph/ShaderGraphNodes.h"
#include "ShaderGraph/ShaderGraphSubgraphs.h"
#include "Shaders/ShaderHotReload.h"

#include "AssetPipe/AssetRegistry.h"
#include "AssetPipe/Assetmanager.h"
#include "Core.h"
#include "Resources/ResourceManager.h"
#include "Scene/SceneManager.h"

#include "misc/cpp/imgui_stdlib.h"   // ImGui::InputText(std::string*)

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cmath>
#include <string_view>

namespace Crucible {

	using namespace ShaderGraph;

	namespace {

		constexpr ImVec4 kErrorText{ 1.0f, 0.55f, 0.43f, 1.0f };     // the State Graph window's
		constexpr ImVec4 kFineText{ 0.6f, 0.85f, 0.6f, 1.0f };
		constexpr ImVec4 kWarningText{ 0.95f, 0.75f, 0.4f, 1.0f };

		// Text in a colour, wrapped at the edge of the panel it is in. (ImGui::TextColored and
		// TextDisabled never wrap: a long message ran off the side of a narrow panel.)
		void WrappedColored(const ImVec4& colour, const char* format, ...) IM_FMTARGS(2);
		void WrappedColored(const ImVec4& colour, const char* format, ...) {
			va_list args;
			va_start(args, format);
			ImGui::PushStyleColor(ImGuiCol_Text, colour);
			ImGui::TextWrappedV(format, args);
			ImGui::PopStyleColor();
			va_end(args);
		}

		// The same in the dimmed colour of hints.
		void WrappedDisabled(const char* format, ...) IM_FMTARGS(1);
		void WrappedDisabled(const char* format, ...) {
			va_list args;
			va_start(args, format);
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrappedV(format, args);
			ImGui::PopStyleColor();
			va_end(args);
		}

		// The preview's own shader: never on disk, always the graph that is open.
		constexpr const char* kPreviewShader = "ShaderGraphPreview";
		// Where the preview mesh is parked: straight up, far past any camera's far plane, so no
		// other view ever shows it and nothing of the scene is near it.
		constexpr glm::vec3 kPreviewPlace{ 0.0f, 5000.0f, 0.0f };
		constexpr double kLiveDelay = 0.3;   // seconds after the last change before the graph is compiled again
		constexpr std::array<const char*, 3> kPreviewMeshes{ "Sphere", "Cube", "Plane" };   // the renderer's primitive meshes

		struct TypeChoice { ParameterType type; const char* label; };
		constexpr std::array<TypeChoice, 6> kParameterTypes{ {
			{ ParameterType::Float,   "Number" },
			{ ParameterType::Vec2,    "Vector 2" },
			{ ParameterType::Vec3,    "Vector 3" },
			{ ParameterType::Vec4,    "Vector 4" },
			{ ParameterType::Colour,  "Colour" },
			{ ParameterType::Texture, "Texture" },
		} };

		const char* TypeLabel(ParameterType type) {
			for (const TypeChoice& choice : kParameterTypes) {
				if (choice.type == type) { return choice.label; }
			}
			return "";
		}

		// "SampleTexture" -> "Sample Texture".
		std::string Title(std::string_view name) {
			std::string title;
			for (size_t i = 0; i < name.size(); ++i) {
				const bool upper = name[i] >= 'A' && name[i] <= 'Z';
				if (upper && i > 0 && name[i - 1] >= 'a' && name[i - 1] <= 'z') { title += ' '; }
				title += name[i];
			}
			return title;
		}

		// What an input is when nothing is linked to it and it has no constant: "s.uv" in words.
		const char* DefaultInWords(std::string_view code) {
			if (code == "s.uv")       { return "the mesh's UV"; }
			if (code == "s.time")     { return "time, in seconds"; }
			if (code == "s.normalWS") { return "the mesh's normal"; }
			return "set by the engine";
		}

		// Right-click on the control just drawn: "Reset to default". True when chosen.
		bool ResetMenu() {
			bool reset = false;
			if (ImGui::BeginPopupContextItem("##reset")) {
				reset = ImGui::MenuItem("Reset to default");
				ImGui::EndPopup();
			}
			return reset;
		}

		// A control for one to four numbers. True while it is being changed.
		bool EditNumbers(const char* label, float* values, uint32_t count, float speed = 0.01f) {
			switch (count) {
			case 2:  return ImGui::DragFloat2(label, values, speed);
			case 3:  return ImGui::DragFloat3(label, values, speed);
			case 4:  return ImGui::DragFloat4(label, values, speed);
			default: return ImGui::DragFloat(label, values, speed);
			}
		}

		// Linear, and allowed above 1 (an emissive colour that blooms).
		bool EditColour(const char* label, float* rgb) {
			return ImGui::ColorEdit3(label, rgb, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_NoOptions);
		}

	}   // anonymous namespace

	ShaderGraphPanel::ShaderGraphPanel(Core& core) : m_core(core), m_preview(core.GetRenderer()) {}

	void ShaderGraphPanel::Init() {
		m_panelName     = "Shader Graph";
		m_windowPadding = { 8.0f, 8.0f };
		m_docked        = false;    // floating, like the State Graph window
		m_flags         = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	}

	void ShaderGraphPanel::Update(float) {
		// The preview is only rendered while there is something to look at it.
		m_preview.SetActive(m_visible && m_hasDocument);
		if (!m_visible) { return; }

		ImGui::SetNextWindowSize({ 1280.0f, 720.0f }, ImGuiCond_FirstUseEver);
		if (m_makeFocus) { ImGui::SetNextWindowFocus(); m_makeFocus = false; }

		const bool readOnly = m_core.GetScenes().IsPlaySessionActive();
		const bool unsaved  = m_hasDocument && m_document.Dirty();

		bool open = true;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, m_windowPadding);
		if (ImGui::Begin(m_panelName.c_str(), &open, m_flags | (unsaved ? ImGuiWindowFlags_UnsavedDocument : 0))) {

			const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
			if (focused && !readOnly && m_hasDocument && !ImGui::GetIO().WantTextInput
				&& !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
				if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S)) { Save(); }
				if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z) && m_document.Undo()) { AfterRebuild(); }
				if ((ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) && m_document.Redo()) { AfterRebuild(); }
			}

			DrawToolbar(readOnly);
			if (!m_message.empty()) { WrappedColored(m_messageIsError ? kErrorText : kFineText, "%s", m_message.c_str()); }

			if (m_hasDocument) {
				// A sub-graph changed on disk (or one came or went): what the generator said about this
				// graph is stale, and so are the preview and the live shader.
				if (const uint64_t version = SubgraphsVersion(); version != m_subgraphsSeen) {
					m_subgraphsSeen = version;
					m_document.Touch();
					m_liveDue = true;
					m_liveAt = ImGui::GetTime();
				}
				const float width = ImGui::GetContentRegionAvail().x;
				const float left  = 240.0f;
				const float right = 300.0f;
				const float gap   = ImGui::GetStyle().ItemSpacing.x;

				ImGui::BeginChild("Parameters", { left, 0.0f }, ImGuiChildFlags_Borders);
				if (DrawParameters(readOnly)) { m_pendingCommit = true; }
				ImGui::EndChild();

				ImGui::SameLine();
				ImGui::BeginChild("Canvas", { (std::max)(100.0f, width - left - right - gap * 2.0f), 0.0f }, ImGuiChildFlags_Borders,
				                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
				WrappedDisabled("Right-click to add a node. Drag from pin to pin to link. Drag on empty canvas to select; middle-drag pans, the wheel zooms.");
				{
					const std::vector<Problem> problems = ShownProblems();
					const ShaderGraphView::Result result = m_view.Draw(m_document.GetGraph(), m_document.Check(), problems, readOnly);
					if (result.changed) { m_pendingCommit = true; }
					if (!result.refused.empty()) { SetMessage(result.refused, true); }
				}
				ImGui::EndChild();

				ImGui::SameLine();
				ImGui::BeginChild("Details", { 0.0f, 0.0f }, ImGuiChildFlags_Borders);
				if (DrawDetails(readOnly)) { m_pendingCommit = true; }
				ImGui::EndChild();

				// The graph was changed this frame: what the generator said about it is stale.
				// One undo step per finished gesture, as in the other graph windows.
				if (m_pendingCommit) {
					m_document.Touch();
					m_liveDue = true;                            // and the live shaders are out of date:
					m_liveAt = ImGui::GetTime() + kLiveDelay;    // sent again once the changes stop coming
					if (!ImGui::IsAnyItemActive() && !m_view.IsBusy()) {
						m_document.Commit();
						m_pendingCommit = false;
					}
				}
				UpdateLive();
			} else {
				ImGui::TextWrapped("Click New to start a shader graph, Open to pick one, or double-click a .shadergraph in the Asset Browser.");
			}

			DrawDialogs();
		}
		ImGui::End();
		ImGui::PopStyleVar();

		if (!open) { m_visible = false; }
	}

	void ShaderGraphPanel::DrawToolbar(bool readOnly) {
		ImGui::BeginDisabled(readOnly);
		if (ImGui::Button("New")) { Guard(Pending::New); }
		ImGui::SameLine();
		if (ImGui::Button("Open")) { m_openPicker = true; }
		ImGui::SameLine();
		ImGui::BeginDisabled(!m_hasDocument);
		if (ImGui::Button("Save")) { Save(); }
		ImGui::SetItemTooltip("Writes the file. Materials using this graph update a moment later (Ctrl+S).");
		ImGui::SameLine();
		if (ImGui::Button("Save As")) { m_openSaveAs = true; }
		ImGui::SameLine();
		ImGui::BeginDisabled(!m_document.CanUndo());
		if (ImGui::Button("Undo") && m_document.Undo()) { AfterRebuild(); }
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!m_document.CanRedo());
		if (ImGui::Button("Redo") && m_document.Redo()) { AfterRebuild(); }
		ImGui::EndDisabled();
		ImGui::EndDisabled();   // !m_hasDocument
		ImGui::EndDisabled();   // readOnly

		ImGui::SameLine();
		ImGui::BeginDisabled(!m_hasDocument);
		if (ImGui::Button("Frame All")) { m_view.FrameAll(m_document.GetGraph()); }
		ImGui::EndDisabled();

		if (m_hasDocument) {
			ImGui::SameLine();
			const std::string path = AssetPath(m_asset);
			ImGui::TextDisabled("%s%s", path.empty() ? "(not saved yet)" : path.c_str(), m_document.Dirty() ? " *" : "");

			// What the saved shader is doing, from the hot reload that compiles it.
			const std::string name = ShaderName();
			if (const ShaderHotReload* hotReload = ShaderHotReload::Active(); hotReload && !name.empty()) {
				if (hotReload->IsCompiling(name)) {
					ImGui::SameLine();
					ImGui::TextColored(kWarningText, "- compiling...");
				} else if (hotReload->HasErrors(name)) {
					ImGui::SameLine();
					WrappedColored(kErrorText, "- the saved shader has errors; its last good version is drawing");
				}
			}
		}
		if (readOnly) { ImGui::SameLine(); ImGui::TextDisabled("- read-only during Play"); }
	}

	bool ShaderGraphPanel::DrawParameters(bool readOnly) {
		Graph& graph = m_document.GetGraph();
		bool changed = false;
		if (m_parameter >= static_cast<int>(graph.parameters.size())) { m_parameter = -1; }

		ImGui::TextUnformatted("Parameters");
		ImGui::SameLine();
		ImGui::BeginDisabled(readOnly);
		if (ImGui::SmallButton("+ Add")) { ImGui::OpenPopup("AddParameter"); }
		if (ImGui::BeginPopup("AddParameter")) {
			for (const TypeChoice& choice : kParameterTypes) {
				if (ImGui::MenuItem(choice.label)) {
					m_parameter = static_cast<int>(AddParameter(graph, choice.type));
					changed = true;
				}
			}
			ImGui::EndPopup();
		}
		WrappedDisabled("What each material sets for itself.");
		ImGui::Separator();

		for (int i = 0; i < static_cast<int>(graph.parameters.size()); ++i) {
			const Parameter& parameter = graph.parameters[static_cast<size_t>(i)];
			ImGui::PushID(i);
			if (ImGui::Selectable(parameter.name.c_str(), i == m_parameter, ImGuiSelectableFlags_AllowDoubleClick)) {
				m_parameter = i;
				if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
					// A node that reads it, in the middle of the canvas.
					const Math::vec2 centre = m_view.ViewCentre();
					if (const uint32_t node = AddNode(graph, kParameterNode, centre.x - 95.0f, centre.y - 25.0f, parameter.name)) {
						m_view.Select(node);
						changed = true;
					}
				}
			}
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(TypeLabel(parameter.type)).x);
			ImGui::TextDisabled("%s", TypeLabel(parameter.type));
			ImGui::PopID();
		}
		if (graph.parameters.empty()) { ImGui::TextDisabled("None yet."); }

		if (m_parameter >= 0) {
			const size_t index = static_cast<size_t>(m_parameter);
			Parameter& parameter = graph.parameters[index];
			ImGui::Separator();
			ImGui::PushID("selected");
			ImGui::PushItemWidth(-70.0f);

			// Its name: applied when the box is left, so a half-typed name is never a rename.
			if (!m_nameEditing && m_nameShown != parameter.name) {
				m_nameBuffer = parameter.name;
				m_nameShown = parameter.name;
			}
			ImGui::InputText("Name", &m_nameBuffer);
			m_nameEditing = ImGui::IsItemActive();
			ImGui::SetItemTooltip("Materials keep their values under this name:\nafter a rename they use the default until it is set again.");
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				std::string why;
				if (RenameParameter(graph, index, m_nameBuffer, why)) {
					changed = true;
				} else {
					SetMessage(why, true);
				}
				m_nameShown.clear();   // show what the name is now
			}

			if (ImGui::BeginCombo("Type", TypeLabel(parameter.type))) {
				for (const TypeChoice& choice : kParameterTypes) {
					if (ImGui::Selectable(choice.label, choice.type == parameter.type) && choice.type != parameter.type) {
						parameter.type = choice.type;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}

			switch (parameter.type) {
			case ParameterType::Texture: {
				if (ImGui::Checkbox("Colour data", &parameter.textureIsColour)) { changed = true; }
				ImGui::SetItemTooltip("On for pictures of colour (albedo, emissive).\nOff for data: normal maps, masks, roughness.");
				constexpr std::array<const char*, 3> kDefaults{ "white", "black", "normal" };
				if (ImGui::BeginCombo("When empty", parameter.textureDefault.c_str())) {
					for (const char* choice : kDefaults) {
						if (ImGui::Selectable(choice, parameter.textureDefault == choice) && parameter.textureDefault != choice) {
							parameter.textureDefault = choice;
							changed = true;
						}
					}
					ImGui::EndCombo();
				}
				ImGui::SetItemTooltip("What a material that leaves this texture empty gets:\nwhite, black, or a flat normal map.");
				break;
			}
			case ParameterType::Colour:
				if (EditColour("Default", parameter.defaultValue.data())) { changed = true; }
				break;
			default: {
				const uint32_t count = parameter.type == ParameterType::Vec2 ? 2u : parameter.type == ParameterType::Vec3 ? 3u : parameter.type == ParameterType::Vec4 ? 4u : 1u;
				if (EditNumbers("Default", parameter.defaultValue.data(), count)) { changed = true; }
				if (ImGui::DragFloat("Min", &parameter.minValue, 0.01f)) { changed = true; }
				if (ImGui::DragFloat("Max", &parameter.maxValue, 0.01f)) { changed = true; }
				ImGui::SetItemTooltip("The range of the material's slider in the Inspector.");
				break;
			}
			}
			ImGui::PopItemWidth();

			if (ImGui::SmallButton("Add node")) {
				const Math::vec2 centre = m_view.ViewCentre();
				if (const uint32_t node = AddNode(graph, kParameterNode, centre.x - 95.0f, centre.y - 25.0f, parameter.name)) {
					m_view.Select(node);
					changed = true;
				}
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Up") && MoveParameter(graph, index, -1)) { --m_parameter; changed = true; }
			ImGui::SetItemTooltip("The order here is the order in a material's Inspector.");
			ImGui::SameLine();
			if (ImGui::SmallButton("Down") && MoveParameter(graph, index, +1)) { ++m_parameter; changed = true; }
			ImGui::SameLine();
			if (ImGui::SmallButton("Delete")) {
				DeleteParameter(graph, index);   // and the nodes that read it
				m_view.KeepSelectionIn(graph);
				m_parameter = -1;
				changed = true;
			}
			ImGui::PopID();
		}
		ImGui::EndDisabled();
		return changed;
	}

	bool ShaderGraphPanel::DrawDetails(bool readOnly) {
		Graph& graph = m_document.GetGraph();
		const std::vector<Problem> problems = ShownProblems();
		bool changed = false;

		DrawPreview();
		ImGui::Separator();

		Node* node = m_view.SelectedNode() != 0 ? graph.FindNode(m_view.SelectedNode()) : nullptr;
		if (!node) {
			// Nothing (or several things) selected: the graph as a whole.
			if (m_view.SelectedNodes().size() > 1) {
				ImGui::Text("%zu nodes selected.", m_view.SelectedNodes().size());
				WrappedDisabled("Drag one to move them all. Delete, Ctrl+C and Ctrl+D work on all of them.");
				ImGui::Separator();
			}
			size_t stopping = 0;
			for (const Problem& problem : problems) { stopping += problem.stops ? 1 : 0; }
			if (problems.empty()) {
				ImGui::TextColored(kFineText, "No problems.");
				ImGui::TextWrapped("Save to update the materials that use this graph.");
			} else {
				if (stopping != 0) {
					WrappedColored(kErrorText, "%zu problem%s to fix:", stopping, stopping == 1 ? "" : "s");
				} else {
					WrappedColored(kWarningText, "Nothing stops the shader, but:");
				}
				for (size_t i = 0; i < problems.size(); ++i) {
					const Problem& problem = problems[i];
					ImGui::PushStyleColor(ImGuiCol_Text, problem.stops ? kErrorText : kWarningText);
					ImGui::TextWrapped("%s", Describe(graph, problem).c_str());
					ImGui::PopStyleColor();
					// Click one to select its node.
					if (ImGui::IsItemClicked() && problem.node != 0 && graph.FindNode(problem.node)) { m_view.Select(problem.node); }
				}
			}
			if (m_view.SelectedNodes().empty()) {
				ImGui::Separator();
				WrappedDisabled("Select a node to set its values.");
			}
			return false;
		}

		const NodeType* type = TypeOfNode(graph, *node);
		ImGui::TextUnformatted(Title(node->type).c_str());
		if (!type) {
			if (node->type == kSubgraphNode) {
				// A sub-graph it cannot find, or cannot use: say why, and how to point it at one.
				for (const Problem& problem : problems) {
					if (problem.node == node->id) { WrappedColored(kErrorText, "%s", problem.message.c_str()); }
				}
				WrappedDisabled("Right-click the node and choose Use sub-graph to point it at one.");
			} else {
				WrappedColored(kErrorText, "This Editor has no such node.");
			}
			return false;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(%s)", type->category);
		ImGui::TextWrapped("%s", type->summary);
		for (const Problem& problem : problems) {
			if (problem.node != node->id) { continue; }
			ImGui::PushStyleColor(ImGuiCol_Text, problem.stops ? kErrorText : kWarningText);
			ImGui::TextWrapped("%s%s", problem.pin.empty() ? "" : (problem.pin + ": ").c_str(), problem.message.c_str());
			ImGui::PopStyleColor();
		}
		ImGui::Separator();

		ImGui::BeginDisabled(readOnly);
		ImGui::PushItemWidth(-90.0f);

		if (node->type == kParameterNode) {
			const std::string current = node->settings.count(kParameterSetting) != 0 ? node->settings[kParameterSetting] : std::string();
			if (ImGui::BeginCombo("Parameter", current.empty() ? "(choose)" : current.c_str())) {
				for (const Parameter& parameter : graph.parameters) {
					if (ImGui::Selectable(parameter.name.c_str(), parameter.name == current) && parameter.name != current) {
						node->settings[kParameterSetting] = parameter.name;
						changed = true;
					}
				}
				if (graph.parameters.empty()) { WrappedDisabled("Add one in the list on the left."); }
				ImGui::EndCombo();
			}
		}

		for (const InputPin& pin : type->inputs) {
			ImGui::PushID(pin.name);
			if (const Link* link = graph.FindLinkInto(node->id, pin.name)) {
				const Node* source = graph.FindNode(link->fromNode);
				ImGui::Text("%s", pin.name);
				ImGui::SameLine();
				ImGui::TextDisabled("from %s%s%s", source ? Title(source->type).c_str() : "?", link->fromPin == "out" ? "" : " . ",
				                    link->fromPin == "out" ? "" : link->fromPin.c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton("Unlink")) {
					DeleteLinkInto(graph, node->id, pin.name);
					changed = true;
				}
			} else if (pin.type == ValueType::Texture) {
				ImGui::Text("%s", pin.name);
				ImGui::SameLine();
				ImGui::TextColored(kErrorText, "link a texture parameter");
			} else if (pin.defaultCode) {
				ImGui::Text("%s", pin.name);
				ImGui::SameLine();
				ImGui::TextDisabled("%s", DefaultInWords(pin.defaultCode));
			} else {
				// Its constant: the node's own, or the table's default until one is set.
				const auto constant = node->inputs.find(pin.name);
				std::array<float, 4> values = constant != node->inputs.end() ? constant->second : pin.defaultValue;
				const uint32_t count = pin.type == ValueType::Any ? 1u : ComponentCount(pin.type);
				const bool edited = pin.colour ? EditColour(pin.name, values.data()) : EditNumbers(pin.name, values.data(), count);
				if (edited) {
					node->inputs[pin.name] = values;
					changed = true;
				}
				if (ResetMenu() && node->inputs.erase(pin.name) != 0) { changed = true; }
			}
			ImGui::PopID();
		}
		if (type->inputs.empty() && node->type != kParameterNode) { ImGui::TextDisabled("Nothing to set."); }

		ImGui::PopItemWidth();
		ImGui::EndDisabled();
		return changed;
	}

	std::vector<Problem> ShaderGraphPanel::ShownProblems() {
		std::vector<Problem> problems = m_document.Check().problems;

		// The compiler's errors with the graph as it was last sent to the preview, on the node
		// whose line it is. (The generator's rows are tested to compile, so this is for a row
		// added with a mistake in it - and later for custom code nodes.)
		if (const ShaderHotReload* hotReload = ShaderHotReload::Active()) {
			for (const ShaderCompiler::Diagnostic& error : hotReload->ErrorsOf(kPreviewShader)) {
				Problem problem;
				const size_t line = error.line > 0 ? static_cast<size_t>(error.line) - 1 : m_previewLines.size();
				if (error.file == kPreviewShader && line < m_previewLines.size() && m_document.GetGraph().FindNode(m_previewLines[line])) { problem.node = m_previewLines[line]; }
				problem.message = "the shader compiler says: " + error.message;
				problems.push_back(std::move(problem));
			}
		}
		return problems;
	}

	void ShaderGraphPanel::DrawPreview() {
		Renderer& renderer = m_core.GetRenderer();
		const float side = std::floor((std::min)(ImGui::GetContentRegionAvail().x, 300.0f));
		if (side < 32.0f) { return; }
		m_preview.Resize(static_cast<uint32_t>(side), static_cast<uint32_t>(side));

		// The mesh, parked far above the scene, and the preview's camera turning round it.
		MeshHandle mesh;
		for (const MeshHandle primitive : renderer.GetPrimitiveMeshes()) {
			if (renderer.GetMeshName(primitive) == kPreviewMeshes[static_cast<size_t>(m_previewMesh)]) { mesh = primitive; }
		}
		if (m_preview.IsValid() && mesh.IsValid() && m_previewMaterial.IsValid()) {
			glm::vec3 low(-0.5f), high(0.5f);
			renderer.GetMeshBounds(mesh, low, high);
			const glm::vec3 centre = (low + high) * 0.5f;
			const float radius = (std::max)(glm::length(high - low) * 0.5f, 0.01f);
			renderer.Submit(mesh, m_previewMaterial, glm::translate(glm::mat4(1.0f), kPreviewPlace - centre));

			Camera& camera = m_preview.GetCamera();
			camera.fovY  = 35.0f;
			camera.nearZ = 0.05f;
			camera.farZ  = 200.0f;
			camera.yaw   = m_orbitYaw;
			camera.pitch = m_orbitPitch;
			const float distance = radius / std::sin(glm::radians(camera.fovY * 0.5f)) * 1.1f * m_orbitZoom;
			camera.position = kPreviewPlace - camera.Forward() * distance;
		}

		// The image, with an invisible button over it for the mouse.
		const ImVec2 corner = ImGui::GetCursorScreenPos();
		m_preview.DrawImage({ side, side });
		ImGui::SetCursorScreenPos(corner);
		ImGui::InvisibleButton("##preview", { side, side });
		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
			m_orbitYaw += ImGui::GetIO().MouseDelta.x * 0.5f;
			m_orbitPitch = std::clamp(m_orbitPitch - ImGui::GetIO().MouseDelta.y * 0.5f, -85.0f, 85.0f);
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);   // the wheel zooms here; it does not scroll the list below
			if (ImGui::GetIO().MouseWheel != 0.0f) { m_orbitZoom = std::clamp(m_orbitZoom * std::pow(0.9f, ImGui::GetIO().MouseWheel), 0.4f, 4.0f); }
		}

		ImGui::SetNextItemWidth(110.0f);
		ImGui::Combo("##mesh", &m_previewMesh, kPreviewMeshes.data(), static_cast<int>(kPreviewMeshes.size()));
		ImGui::SetItemTooltip("What the preview shows the graph on. Drag the picture to turn it, the wheel zooms.");

		// What the preview is showing, when it is not simply the graph as it is.
		const ShaderHotReload* hotReload = ShaderHotReload::Active();
		ImGui::SameLine();
		if (!hotReload) {
			WrappedColored(kWarningText, "no shader compiler");
			ImGui::SetItemTooltip("slangc was not found on this machine (the Vulkan SDK), so nothing can be previewed.");
		} else if (!m_document.Check().ok) {
			WrappedColored(kErrorText, "the last graph that worked");
			ImGui::SetItemTooltip("The graph has a problem that stops its shader. The preview keeps the last version that worked.");
		} else if (hotReload->HasErrors(kPreviewShader)) {
			WrappedColored(kErrorText, "compiler errors");
		} else if (m_liveDue || hotReload->IsCompiling(kPreviewShader)) {
			ImGui::TextColored(kWarningText, "updating...");
		} else {
			ImGui::TextDisabled("live");
			ImGui::SetItemTooltip("Parameters show their defaults here, and textures the default each one names.");
		}
	}

	void ShaderGraphPanel::UpdateLive() {
		ShaderHotReload* hotReload = ShaderHotReload::Active();
		if (!hotReload) { return; }   // no compiler on this machine: nothing can be live
		Renderer& renderer = m_core.GetRenderer();

		// The preview's shader and its one material, made the first time they are needed. The
		// shader has no file: the hot reload layer starts watching it a frame later, and from
		// then on it takes the source given below.
		if (!m_previewShader.IsValid()) { m_previewShader = renderer.LoadMaterialShader(kPreviewShader, true); }
		if (!m_previewMaterial.IsValid() && m_previewShader.IsValid()) {
			MaterialData data;
			data.shader = m_previewShader;
			m_previewMaterial = renderer.CreateMaterial(data);
		}
		SyncPreviewValues();

		// The graph again, once the changes have stopped coming and the gesture has ended.
		if (!m_liveDue || m_pendingCommit || ImGui::GetTime() < m_liveAt) { return; }
		const Generated& check = m_document.Check();
		if (!check.ok) {
			m_liveDue = false;   // nothing to send: what worked last keeps drawing
			return;
		}

		// The preview: constants as hidden parameters, so dragging one needs no compile.
		const Generated preview = Generate(m_document.GetGraph(), Options{ true });
		if (!preview.ok || !hotReload->SetLive(kPreviewShader, preview.source, preview.paramsJson)) { return; }   // not watched yet: next frame
		m_previewLines = preview.lineNodes;

		// The scene's materials on this graph: the graph as it is while it differs from its
		// file, the file's own again once it does not (saved, or undone back to it).
		const std::string name = ShaderName();
		if (!name.empty()) {
			if (m_document.Dirty()) {
				if (hotReload->SetLive(name, check.source, check.paramsJson)) { m_liveName = name; }
			} else {
				StopLive();
			}
		}
		m_liveDue = false;
	}

	void ShaderGraphPanel::StopLive() {
		if (m_liveName.empty()) { return; }
		if (ShaderHotReload* hotReload = ShaderHotReload::Active()) { hotReload->ClearLive(m_liveName); }
		m_liveName.clear();
	}

	void ShaderGraphPanel::SyncPreviewValues() {
		if (!m_previewMaterial.IsValid()) { return; }
		Renderer& renderer = m_core.GetRenderer();
		const Graph& graph = m_document.GetGraph();

		// By NAME, against the list the preview shader has right now (a new list arrives with
		// its code, a little after the graph changes): each of the graph's parameters at its
		// default, each hidden one at its node's constant as it is this frame.
		const MaterialShaderParams& list = renderer.GetMaterialShaderParams(m_previewShader);
		MaterialData data = renderer.GetMaterialData(m_previewMaterial);
		bool changed = data.params.size() != list.params.size();
		data.params.resize(list.params.size());
		for (size_t i = 0; i < list.params.size(); ++i) {
			glm::vec4 value = list.params[i].defaultValue;
			uint32_t nodeId = 0;
			std::string pin;
			if (ParseLiveConstantName(list.params[i].name, nodeId, pin)) {
				const Node* node = graph.FindNode(nodeId);
				const NodeType* type = node ? TypeOfNode(graph, *node) : nullptr;
				if (const int index = type ? type->FindInput(pin) : -1; index >= 0) {
					const auto constant = node->inputs.find(pin);
					const std::array<float, 4>& values = constant != node->inputs.end() ? constant->second : type->inputs[static_cast<size_t>(index)].defaultValue;
					value = glm::vec4(values[0], values[1], values[2], values[3]);
				}
			} else if (const Parameter* parameter = graph.FindParameter(list.params[i].name)) {
				value = glm::vec4(parameter->defaultValue[0], parameter->defaultValue[1], parameter->defaultValue[2], parameter->defaultValue[3]);
			}
			if (data.params[i] != value) {
				data.params[i] = value;
				changed = true;
			}
		}
		if (changed) { renderer.UpdateMaterial(m_previewMaterial, data); }
	}

	void ShaderGraphPanel::DrawDialogs() {
		// Open: every .shadergraph in Assets.
		if (m_openPicker) { ImGui::OpenPopup("Open Shader Graph"); m_openPicker = false; }
		if (ImGui::BeginPopup("Open Shader Graph")) {
			const std::vector<ShaderGraphFile> graphs = ListShaderGraphs(m_core.GetAssets());
			for (const ShaderGraphFile& graph : graphs) {
				if (ImGui::Selectable(graph.virtualPath.c_str())) {
					Guard(Pending::Open, AssetRegistry::GuidFromString(graph.shaderName).m_Value);
					ImGui::CloseCurrentPopup();
				}
			}
			if (graphs.empty()) { WrappedDisabled("No .shadergraph files in Assets yet."); }
			ImGui::EndPopup();
		}

		// Save As.
		if (m_openSaveAs) { ImGui::OpenPopup("Save Shader Graph As"); m_openSaveAs = false; }
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
		if (ImGui::BeginPopupModal("Save Shader Graph As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted("Path inside Assets:");
			ImGui::SetNextItemWidth(420.0f);
			ImGui::InputText("##path", m_savePath, sizeof m_savePath);
			if (ImGui::Button("Save") && SaveAs(m_savePath)) { ImGui::CloseCurrentPopup(); }
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
			ImGui::EndPopup();
		}

		// Unsaved changes.
		if (m_openGuard) { ImGui::OpenPopup("Unsaved Shader Graph"); m_openGuard = false; }
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
		if (ImGui::BeginPopupModal("Unsaved Shader Graph", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted("This shader graph has unsaved changes. Discard them?");
			if (ImGui::Button("Discard")) {
				const Pending action = m_pending;
				m_pending = Pending::None;
				if (action == Pending::New) { NewGraph(); }
				else if (action == Pending::Open) { Load(m_pendingAsset); }
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) { m_pending = Pending::None; ImGui::CloseCurrentPopup(); }
			ImGui::EndPopup();
		}
	}

	void ShaderGraphPanel::Guard(Pending action, std::uint64_t asset) {
		if (m_hasDocument && m_document.Dirty()) {
			m_pending      = action;
			m_pendingAsset = asset;
			m_openGuard    = true;
			return;
		}
		if (action == Pending::New) { NewGraph(); }
		else if (action == Pending::Open) { Load(asset); }
	}

	void ShaderGraphPanel::RequestOpen(std::uint64_t asset) {
		Show();
		if (m_hasDocument && asset == m_asset) { return; }
		Guard(Pending::Open, asset);
	}

	void ShaderGraphPanel::AfterRebuild() {
		m_pendingCommit = false;
		m_liveDue = true;               // another graph than a moment ago: show it
		m_liveAt = ImGui::GetTime();
		m_view.KeepSelectionIn(m_document.GetGraph());
		m_nameShown.clear();
		m_nameEditing = false;
	}

	void ShaderGraphPanel::NewGraph() {
		StopLive();   // the graph that was open: its materials go back to its file
		m_document.New();
		m_asset       = 0;
		m_hasDocument = true;
		m_parameter   = -1;
		m_view.ClearSelection();
		AfterRebuild();
		m_view.FrameAll(m_document.GetGraph());
		SetMessage("New shader graph. Right-click to add nodes and link them into the Output; Save As gives it a file.", false);
	}

	bool ShaderGraphPanel::Load(std::uint64_t asset) {
		const AssetEntry* entry = m_core.GetAssets().FindByGuid(xresource::instance_guid{ asset });
		std::string text;
		if (!entry || entry->extension != kShaderGraphExtension || !m_core.GetAssets().ReadText(entry->virtualPath, text)) {
			SetMessage("Could not read that shader graph", true);
			return false;
		}

		std::string error;
		if (!m_document.Load(text, error)) { SetMessage(entry->virtualPath + ": " + error, true); return false; }
		StopLive();   // the graph that was open: its materials go back to its file

		m_asset       = asset;
		m_hasDocument = true;
		m_parameter   = -1;
		m_view.ClearSelection();
		AfterRebuild();
		m_view.FrameAll(m_document.GetGraph());
		SetMessage("Opened " + entry->virtualPath, false);
		return true;
	}

	bool ShaderGraphPanel::Save() {
		if (!m_hasDocument) { return false; }
		if (!m_asset) { m_openSaveAs = true; return false; }

		m_document.Commit();
		m_pendingCommit = false;

		const std::string path = AssetPath(m_asset);
		if (path.empty()) { SetMessage("The file is gone from Assets - use Save As", true); return false; }

		std::string error;
		if (!m_core.GetResources().SaveTextAsset(path, m_document.Text(), false, &error)) { SetMessage(error, true); return false; }

		m_document.MarkSaved();
		StopLive();   // the file now says what was live: from here the shader is the file's, and is kept beside the built ones
		const Generated& check = m_document.Check();
		if (check.ok) {
			SetMessage("Saved " + path + " - materials using it update in a moment.", false);
		} else {
			SetMessage("Saved " + path + ", but it has problems: its materials keep the last good shader until they are fixed.", true);
		}
		if (auto browser = EditorManager::GetInstance().GetPanel<AssetBrowser>(ImguiPanels::ASSETBROWSER)) { browser->Refresh(); }
		return true;
	}

	bool ShaderGraphPanel::SaveAs(std::string path) {
		if (!m_hasDocument) { return false; }
		const std::string_view extension = kShaderGraphExtension;
		if (path.size() < extension.size() || path.compare(path.size() - extension.size(), extension.size(), extension) != 0) { path += extension; }

		m_document.Commit();
		m_pendingCommit = false;

		std::string error;
		const AssetId id = m_core.GetResources().SaveTextAsset(path, m_document.Text(), true, &error);
		if (!id) { SetMessage(error, true); return false; }

		StopLive();   // if this was a saved graph being saved under a new name: the old one goes back to its file
		m_asset = id;
		m_document.MarkSaved();
		SetMessage("Saved " + path + ". Pick it as a material's Shader in the Inspector to use it.", false);
		if (auto browser = EditorManager::GetInstance().GetPanel<AssetBrowser>(ImguiPanels::ASSETBROWSER)) { browser->Refresh(); }
		return true;
	}

	std::string ShaderGraphPanel::ShaderName() const {
		return m_asset != 0 ? AssetRegistry::GuidToString(xresource::instance_guid{ m_asset }) : std::string();
	}

	std::string ShaderGraphPanel::AssetPath(std::uint64_t asset) const {
		if (!asset) { return {}; }
		const AssetEntry* entry = m_core.GetAssets().FindByGuid(xresource::instance_guid{ asset });
		return entry ? entry->virtualPath : std::string();
	}

}
