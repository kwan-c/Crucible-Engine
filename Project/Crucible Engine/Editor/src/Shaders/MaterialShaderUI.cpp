/*!
* @file			MaterialShaderUI.cpp
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			16 November 2026
*
* @brief		Implementation - see MaterialShaderUI.h.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "Shaders/MaterialShaderUI.h"

#include "ShaderGraph/ShaderGraphAsset.h"
#include "ImGui/Panels/ImGuiPanel_ShaderGraph.h"
#include "AssetPipe/AssetRegistry.h"

#include "ImGui/EditorWidgets.h"   // kAssetPayload: what the Asset Browser drags
#include "ImGui/ImGuiManager.h"    // EditorManager::GetResources
#include "AssetPipe/AssetPaths.h"
#include "Diagnostics/Logger.h"
#include "Graphics/MaterialShaderParams.h"
#include "Graphics/RenderSystem.h"
#include "Resources/ResourceManager.h"
#include <Core.h>

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace Crucible {

	namespace {

		constexpr const char* kBuiltInLabel = "Default";
		constexpr const char* kBuiltInName  = "DefaultShader";   // MaterialShaders' handle 0
		constexpr double      kRescanEvery  = 2.0;               // seconds between looks at Assets/Shaders

		// The mesh shaders in Assets/Shaders: every .slang that ends with the engine's mesh shader
		// (#include "SurfaceTemplate.slang"), by name, apart from the built-in one. Looked up again
		// every couple of seconds, so a shader written while the Editor runs appears by itself.
		const std::vector<std::string>& MeshShaderNames() {
			static std::vector<std::string> names;
			static double nextScan = 0.0;
			if (ImGui::GetTime() < nextScan) { return names; }
			nextScan = ImGui::GetTime() + kRescanEvery;

			names.clear();
			std::error_code ec;
			for (const fs::directory_entry& entry : fs::directory_iterator(AssetPath("Shaders"), ec)) {
				if (!entry.is_regular_file(ec) || entry.path().extension() != ".slang") { continue; }
				std::ifstream in(entry.path(), std::ios::binary);
				const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				if (text.find("#include \"SurfaceTemplate.slang\"") == std::string::npos) { continue; }
				std::string name = PathToUtf8(entry.path().stem());
				if (name != kBuiltInName) { names.push_back(std::move(name)); }
			}
			std::sort(names.begin(), names.end());
			return names;
		}

		// The shader graphs in Assets (any folder), looked up again every couple of seconds like
		// the list above.
		const std::vector<ShaderGraphFile>& ShaderGraphs() {
			static std::vector<ShaderGraphFile> graphs;
			static double nextScan = 0.0;
			if (ImGui::GetTime() < nextScan) { return graphs; }
			nextScan = ImGui::GetTime() + kRescanEvery;

			const Core* core = EditorManager::GetInstance().GetCore();
			graphs = core ? ListShaderGraphs(core->GetAssets()) : std::vector<ShaderGraphFile>();
			return graphs;
		}

		// "Ember.shadergraph" -> "Ember (graph)": how the picker shows a shader graph.
		std::string GraphLabel(const ShaderGraphFile& graph) {
			return graph.label.substr(0, graph.label.rfind('.')) + " (graph)";
		}

		// "swirlSpeed" / "swirl_speed" -> "Swirl Speed": a parameter's name as a label.
		std::string Label(const std::string& name) {
			std::string label;
			for (size_t i = 0; i < name.size(); ++i) {
				const char c = name[i];
				const bool upper = c >= 'A' && c <= 'Z';
				if (c == '_') {
					if (!label.empty() && label.back() != ' ') { label += ' '; }
					continue;
				}
				if (upper && i > 0 && name[i - 1] >= 'a' && name[i - 1] <= 'z') { label += ' '; }
				const bool first = label.empty() || label.back() == ' ';
				label += (first && c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
			}
			return label.empty() ? name : label;
		}

		// Right-click on the control just drawn: "Reset to default". True when chosen. (An id of
		// its own, inside the caller's PushID: multi-part controls - a colour, a vector - have no
		// id for the popup to borrow.)
		bool ResetMenu() {
			bool reset = false;
			if (ImGui::BeginPopupContextItem("##reset")) {
				reset = ImGui::MenuItem("Reset to default");
				ImGui::EndPopup();
			}
			return reset;
		}

		bool DrawShaderPicker(Renderer& renderer, MaterialData& data) {
			const std::string current = renderer.GetMaterialShaderName(data.shader);
			const bool builtIn = current.empty() || current == kBuiltInName;

			// A shader graph goes by its GUID: show its file's name instead.
			const std::vector<ShaderGraphFile>& graphs = ShaderGraphs();
			std::string shown = current;
			bool isGraph = false;
			for (const ShaderGraphFile& graph : graphs) {
				if (graph.shaderName == current) {
					shown = GraphLabel(graph);
					isGraph = true;
				}
			}

			bool changed = false;
			const auto choose = [&](MaterialShaderHandle shader) {
				if (shader == data.shader) { return; }
				data.shader = shader;
				// Another shader, another list: its parameters start from its defaults.
				data.params.clear();
				data.shaderTextures.clear();
				changed = true;
			};

			if (ImGui::BeginCombo("Shader", builtIn ? kBuiltInLabel : shown.c_str())) {
				if (ImGui::Selectable(kBuiltInLabel, builtIn) && !builtIn) { choose(MaterialShaderHandle{}); }
				for (const std::string& name : MeshShaderNames()) {
					if (ImGui::Selectable(name.c_str(), name == current) && name != current) {
						choose(renderer.LoadMaterialShader(name));
					}
				}
				for (const ShaderGraphFile& graph : graphs) {
					const std::string label = GraphLabel(graph) + "##" + graph.shaderName;   // two graphs can share a file name
					if (ImGui::Selectable(label.c_str(), graph.shaderName == current) && graph.shaderName != current) {
						choose(renderer.LoadMaterialShader(graph.shaderName));
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SetItemTooltip("What draws this material: the built-in shader (the factors and five textures here),\n"
			                      "or a mesh shader from Assets/Shaders or a shader graph, which add parameters of their own below.");
			if (isGraph) {
				// Straight to the graph, in the Shader Graph window.
				ImGui::SameLine();
				if (ImGui::SmallButton("Edit")) {
					if (auto panel = EditorManager::GetInstance().GetPanel<ShaderGraphPanel>(ImguiPanels::SHADERGRAPH)) {
						panel->RequestOpen(AssetRegistry::GuidFromString(current).m_Value);
					}
				}
				ImGui::SetItemTooltip("Open this shader graph in the Shader Graph window.");
			}
			return changed;
		}

		bool DrawParam(const MaterialParamDesc& desc, glm::vec4& value) {
			const std::string label = Label(desc.name);
			const bool ranged = desc.maxValue > desc.minValue;
			const float low = ranged ? desc.minValue : 0.0f, high = ranged ? desc.maxValue : 0.0f;   // 0, 0 = no limits

			bool changed = false;
			ImGui::PushID(desc.name.c_str());
			switch (desc.type) {
			case MaterialParamType::Float:
				changed = ranged ? ImGui::SliderFloat(label.c_str(), &value.x, low, high)
				                 : ImGui::DragFloat(label.c_str(), &value.x, 0.01f);
				break;
			case MaterialParamType::Vec2:   changed = ImGui::DragFloat2(label.c_str(), &value.x, 0.01f, low, high); break;
			case MaterialParamType::Vec3:   changed = ImGui::DragFloat3(label.c_str(), &value.x, 0.01f, low, high); break;
			case MaterialParamType::Vec4:   changed = ImGui::DragFloat4(label.c_str(), &value.x, 0.01f, low, high); break;
			case MaterialParamType::Colour:
				// Linear, and allowed above 1 (a glow that blooms). NoOptions: right-click is the
				// reset menu here, as on the other controls.
				changed = ImGui::ColorEdit3(label.c_str(), &value.x,
				                            ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_NoOptions);
				break;
			}
			if (ResetMenu() && value != desc.defaultValue) {
				value = desc.defaultValue;
				changed = true;
			}
			ImGui::PopID();
			return changed;
		}

		// One texture slot: a box that says whether it holds a texture and takes one dragged from the
		// Asset Browser, then the slot's name. (The same shape as the Inspector's five built-in slots.)
		bool DrawTexture(const MaterialTextureDesc& desc, TextureHandle& handle) {
			const std::string label = Label(desc.name);
			bool changed = false;

			ImGui::PushID(desc.name.c_str());
			ImGui::Selectable(handle.IsValid() ? "(assigned)" : "(none)", false, ImGuiSelectableFlags_None, ImVec2(120.0f, 0.0f));
			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorWidgets::kAssetPayload)) {
					const std::string path(static_cast<const char*>(payload->Data));
					// Through the ResourceManager: only textures it loaded have an asset id, and the
					// id is what the .mat file stores.
					ResourceManager* resources = EditorManager::GetInstance().GetResources();
					const TextureHandle loaded = resources ? resources->GetTextureByPath(path, desc.srgb) : TextureHandle{};
					if (loaded.IsValid()) {
						handle  = loaded;
						changed = true;
					} else {
						CRU_USER_WARN("[Inspector] '{}' is not a texture that could be loaded - {} unchanged", path, label);
					}
				}
				ImGui::EndDragDropTarget();
			}
			if (ImGui::BeginPopupContextItem("##clear")) {
				if (ImGui::MenuItem("Clear", nullptr, false, handle.IsValid())) {
					handle  = TextureHandle{};
					changed = true;
				}
				ImGui::EndPopup();
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(label.c_str());
			ImGui::PopID();
			return changed;
		}

	}   // anonymous namespace

	MaterialBuiltIns MaterialBuiltInsUsed(Renderer& renderer, const MaterialData& data) {
		MaterialBuiltIns used;
		const std::string name = renderer.GetMaterialShaderName(data.shader);
		if (name.empty() || name == kBuiltInName) { return used; }

		for (const ShaderGraphFile& graph : ShaderGraphs()) {
			if (graph.shaderName == name) {
				used.factors = false;
				used.textures = false;
				return used;
			}
		}
		used.textures = renderer.GetMaterialShaderParams(data.shader).textures.empty();
		return used;
	}

	bool DrawMaterialShaderParams(Renderer& renderer, MaterialData& data) {
		bool changed = DrawShaderPicker(renderer, data);

		const MaterialShaderParams& list = renderer.GetMaterialShaderParams(data.shader);
		if (list.Empty()) { return changed; }

		// A divider carrying the shader's name: what follows is that shader's own, set apart
		// from the built-in controls above it.
		std::string title = renderer.GetMaterialShaderName(data.shader);
		for (const ShaderGraphFile& graph : ShaderGraphs()) {
			if (graph.shaderName == title) { title = graph.label.substr(0, graph.label.rfind('.')); }   // a graph: its file's name, not its GUID
		}
		ImGui::SeparatorText((title + " parameters").c_str());

		ImGui::PushID("ShaderParams");

		// Values are kept by slot. A material that has fewer than the list (it was saved before a
		// parameter was added, or never set any) gets the defaults for the rest - what the
		// renderer draws it with anyway, so this alone is not a change.
		if (data.params.size() != list.params.size()) {
			const size_t had = (std::min)(data.params.size(), list.params.size());
			data.params.resize(list.params.size());
			for (size_t i = had; i < list.params.size(); ++i) { data.params[i] = list.params[i].defaultValue; }
		}
		for (size_t i = 0; i < list.params.size(); ++i) {
			changed |= DrawParam(list.params[i], data.params[i]);
		}

		if (!list.textures.empty()) {
			data.shaderTextures.resize(list.textures.size());   // new slots start empty
			for (size_t i = 0; i < list.textures.size(); ++i) {
				changed |= DrawTexture(list.textures[i], data.shaderTextures[i]);
			}
		}

		ImGui::PopID();
		return changed;
	}

}
