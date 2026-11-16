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
#include "Shaders/ShaderHotReload.h"

#include "AssetPipe/AssetRegistry.h"
#include "AssetPipe/Assetmanager.h"
#include "Core.h"
#include "Resources/ResourceManager.h"
#include "Scene/SceneManager.h"

#include "misc/cpp/imgui_stdlib.h"   // ImGui::InputText(std::string*)

#include <algorithm>
#include <array>
#include <string_view>

namespace Crucible {

	using namespace ShaderGraph;

	namespace {

		constexpr ImVec4 kErrorText{ 1.0f, 0.55f, 0.43f, 1.0f };     // the State Graph window's
		constexpr ImVec4 kFineText{ 0.6f, 0.85f, 0.6f, 1.0f };
		constexpr ImVec4 kWarningText{ 0.95f, 0.75f, 0.4f, 1.0f };

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

	void ShaderGraphPanel::Init() {
		m_panelName     = "Shader Graph";
		m_windowPadding = { 8.0f, 8.0f };
		m_docked        = false;    // floating, like the State Graph window
		m_flags         = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	}

	void ShaderGraphPanel::Update(float) {
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
			if (!m_message.empty()) { ImGui::TextColored(m_messageIsError ? kErrorText : kFineText, "%s", m_message.c_str()); }

			if (m_hasDocument) {
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
				ImGui::TextDisabled("Right-click to add a node. Drag from pin to pin to link. Drag on empty canvas to select; middle-drag pans, the wheel zooms.");
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
					if (!ImGui::IsAnyItemActive() && !m_view.IsBusy()) {
						m_document.Commit();
						m_pendingCommit = false;
					}
				}
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
					ImGui::TextColored(kErrorText, "- the saved shader has errors; its last good version is drawing");
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
		ImGui::TextDisabled("What each material sets for itself.");
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

		Node* node = m_view.SelectedNode() != 0 ? graph.FindNode(m_view.SelectedNode()) : nullptr;
		if (!node) {
			// Nothing (or several things) selected: the graph as a whole.
			if (m_view.SelectedNodes().size() > 1) {
				ImGui::Text("%zu nodes selected.", m_view.SelectedNodes().size());
				ImGui::TextDisabled("Drag one to move them all. Delete, Ctrl+C and Ctrl+D work on all of them.");
				ImGui::Separator();
			}
			size_t stopping = 0;
			for (const Problem& problem : problems) { stopping += problem.stops ? 1 : 0; }
			if (problems.empty()) {
				ImGui::TextColored(kFineText, "No problems.");
				ImGui::TextWrapped("Save to update the materials that use this graph.");
			} else {
				if (stopping != 0) {
					ImGui::TextColored(kErrorText, "%zu problem%s to fix:", stopping, stopping == 1 ? "" : "s");
				} else {
					ImGui::TextColored(kWarningText, "Nothing stops the shader, but:");
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
				ImGui::TextDisabled("Select a node to set its values.");
			}
			return false;
		}

		const NodeType* type = FindNodeType(node->type);
		ImGui::TextUnformatted(Title(node->type).c_str());
		if (!type) {
			ImGui::TextColored(kErrorText, "This Editor has no such node.");
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
				if (graph.parameters.empty()) { ImGui::TextDisabled("Add one in the list on the left."); }
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

		// The compiler's errors with the shader as it was last saved, on the node whose line it is.
		const std::string name = ShaderName();
		if (const ShaderHotReload* hotReload = ShaderHotReload::Active(); hotReload && !name.empty()) {
			for (const ShaderCompiler::Diagnostic& error : hotReload->ErrorsOf(name)) {
				Problem problem;
				const size_t line = error.line > 0 ? static_cast<size_t>(error.line) - 1 : m_savedLines.size();
				if (error.file == name && line < m_savedLines.size() && m_document.GetGraph().FindNode(m_savedLines[line])) { problem.node = m_savedLines[line]; }
				problem.message = "the shader compiler says: " + error.message;
				problems.push_back(std::move(problem));
			}
		}
		return problems;
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
			if (graphs.empty()) { ImGui::TextDisabled("No .shadergraph files in Assets yet."); }
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
		m_view.KeepSelectionIn(m_document.GetGraph());
		m_nameShown.clear();
		m_nameEditing = false;
	}

	void ShaderGraphPanel::NewGraph() {
		m_document.New();
		m_asset       = 0;
		m_hasDocument = true;
		m_parameter   = -1;
		m_savedLines.clear();
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

		m_asset       = asset;
		m_hasDocument = true;
		m_parameter   = -1;
		m_savedLines  = m_document.Check().lineNodes;
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
		const Generated& check = m_document.Check();
		m_savedLines = check.lineNodes;
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

		m_asset = id;
		m_document.MarkSaved();
		m_savedLines = m_document.Check().lineNodes;
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
