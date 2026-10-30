/*!
* @file			ImGuiPanel_Resources.cpp
* @author		Chewn Thing Kwan
* @par			kwan.c@digipen.edu
* @date			30 October 2026
*
* @brief		The editor's Resources tab - see ImGuiPanel_Resources.h.
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "ImGui/Panels/ImGuiPanel_Resources.h"
#include "ImGui/EditorTheme.h"
#include "Scene/SceneManager.h"

#include <algorithm>
#include <format>
#include <string_view>
#include <vector>

Crucible::ResourcesPanel::ResourcesPanel(ResourceManager& resources, SceneManager& scenes)
	: m_resources{ resources }, m_scenes{ scenes } {}

void Crucible::ResourcesPanel::Init() {
	m_panelName = "Resources";

	// Bottom area, tabbed with the Asset Browser and Console.
	m_docked = true;
	m_dockArea = DockArea::Bottom;
}

void Crucible::ResourcesPanel::UnloadUnusedNow() {
	SceneManager::Snapshot snapshot;
	if (!m_scenes.CaptureSnapshot(snapshot)) {
		m_lastUnload = "Could not read the open scene - nothing unloaded";
		return;
	}
	const UnloadSummary freed = m_resources.UnloadUnused(snapshot.scene.document);
	m_lastUnload = freed.Total() == 0
		? std::string("Nothing to unload - everything loaded is used")
		: std::format("Unloaded {} models, {} materials, {} textures, {} fonts, {} skyboxes, {} audio clips, {} cached definitions",
		              freed.models, freed.materials, freed.textures, freed.fonts, freed.skyboxes, freed.audioClips, freed.cached);
}

void Crucible::ResourcesPanel::Update(float) {
	if (ImGui::Begin(m_panelName.c_str(), nullptr, m_flags)) {

		const std::vector<LoadedResourceInfo> loaded = m_resources.ListLoaded();

		// Kinds present right now, for the filter combo ("All" first).
		std::vector<std::string_view> kinds{ "All" };
		for (const LoadedResourceInfo& info : loaded) {
			if (std::find(kinds.begin(), kinds.end(), std::string_view{ info.kind }) == kinds.end()) { kinds.emplace_back(info.kind); }
		}
		if (m_kindFilter >= static_cast<int>(kinds.size())) { m_kindFilter = 0; }

		// ---- Toolbar ----
		if (ImGui::Button("Unload unused")) { UnloadUnusedNow(); }
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Free everything the open scene does not use\n(this also happens on every scene change)");
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(160.0f);
		if (ImGui::BeginCombo("##kind", std::string(kinds[m_kindFilter]).c_str())) {
			for (int i = 0; i < static_cast<int>(kinds.size()); ++i) {
				if (ImGui::Selectable(std::string(kinds[i]).c_str(), i == m_kindFilter)) { m_kindFilter = i; }
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::Checkbox("Built-ins", &m_showBuiltins);
		ImGui::SameLine();
		m_filter.Draw("##search", 200.0f);

		std::size_t failed = 0;
		for (const LoadedResourceInfo& info : loaded) { if (info.failed) { ++failed; } }
		ImGui::TextDisabled("%zu in memory%s", loaded.size() - failed,
		                    failed != 0 ? std::format(", {} failed to load", failed).c_str() : "");
		if (!m_lastUnload.empty()) {
			ImGui::SameLine();
			ImGui::TextDisabled("|  %s", m_lastUnload.c_str());
		}

		// ---- Table ----
		constexpr ImGuiTableFlags kTableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
		                                        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
		if (ImGui::BeginTable("##resources", 4, kTableFlags)) {
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 140.0f);
			ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 110.0f);
			ImGui::TableSetupColumn("GUID", ImGuiTableColumnFlags_WidthFixed, 150.0f);
			ImGui::TableHeadersRow();

			for (const LoadedResourceInfo& info : loaded) {
				if (info.builtin && !m_showBuiltins) { continue; }
				if (m_kindFilter != 0 && kinds[m_kindFilter] != info.kind) { continue; }
				if (!m_filter.PassFilter(info.name.c_str())) { continue; }

				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(info.name.c_str());
				if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", info.name.c_str()); }

				ImGui::TableNextColumn();
				ImGui::TextUnformatted(info.kind);

				ImGui::TableNextColumn();
				if (info.failed) {
					ImGui::TextColored(EditorTheme::GetPalette().error, "Failed");
				} else if (info.builtin) {
					ImGui::TextDisabled("Built-in");
				} else if (info.pinned) {
					ImGui::TextUnformatted("Loaded (kept)");
					if (ImGui::IsItemHovered()) { ImGui::SetTooltip("An editor tool is showing it, so it stays loaded"); }
				} else {
					ImGui::TextUnformatted("Loaded");
				}

				ImGui::TableNextColumn();
				ImGui::TextDisabled("%016llX", static_cast<unsigned long long>(info.id));
			}
			ImGui::EndTable();
		}
	}
	ImGui::End();
}
