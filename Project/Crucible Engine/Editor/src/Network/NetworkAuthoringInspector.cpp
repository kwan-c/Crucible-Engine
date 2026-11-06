/*!
* @file			NetworkAuthoringInspector.cpp
* @author		Abhishek Akilandeswari Patchamuthu
* @par			a.patchamuthu@digipen.edu
* @date			6 November 2026
*
* @brief		The networking policy table (see the header).
*
* All Content copyrighted 2026 DigiPen Institute of Technology Singapore.
* All Rights Reserved
*/

#include "Network/NetworkAuthoringInspector.h"

#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"

#include "ECS/World.h"
#include "ImGui/EditorTheme.h"
#include "Network/NetComponents.h"
#include "Network/NetObjectProfiles.h"
#include "Network/NetPrefabTable.h"
#include "Network/ReplicationRegistry.h"

namespace Crucible
{
	namespace
	{
		constexpr const char* kEnabledMask = "EnabledMask";

		bool Contains(const std::vector<std::string>& list, const std::string& name)
		{
			return std::ranges::find(list, name) != list.end();
		}

		bool Erase(std::vector<std::string>& list, const std::string& name)
		{
			return std::erase(list, name) > 0;
		}

		// One combo whose options can each be greyed out with a reason.
		struct Choice { std::string label; std::string disabledWhy; };

		bool ComboWithReasons(const char* id, int& current, const std::vector<Choice>& choices)
		{
			bool changed = false;
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::BeginCombo(id, choices[static_cast<std::size_t>(current)].label.c_str()))
			{
				for (int i = 0; i < static_cast<int>(choices.size()); ++i)
				{
					const Choice& c = choices[static_cast<std::size_t>(i)];
					ImGui::BeginDisabled(!c.disabledWhy.empty());
					if (ImGui::Selectable(c.label.c_str(), i == current) && i != current) { current = i; changed = true; }
					ImGui::EndDisabled();
					if (!c.disabledWhy.empty()) ImGui::SetItemTooltip("%s", c.disabledWhy.c_str());
				}
				ImGui::EndCombo();
			}
			return changed;
		}

		void HelpMarker(const char* text)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			ImGui::SetItemTooltip("%s", text);
		}
	}

	const char* NetworkAuthoringInspector::AddBlockedReason(const World& world, Entity entity)
	{
		const ComponentId authoring = ComponentTypeId<NetworkObjectAuthoring>();
		for (Entity up = world.GetParent(entity); world.IsAlive(up); up = world.GetParent(up))
			if (world.GetSignature(up).test(authoring))
				return "An entity above this one is already a network object.\nA network object cannot contain another one.";
		std::vector<Entity> open{ entity };
		while (!open.empty())
		{
			const Entity e = open.back();
			open.pop_back();
			bool found = false;
			world.ForEachChild(e, [&](Entity child) {
				if (world.GetSignature(child).test(authoring)) found = true;
				open.push_back(child);
			});
			if (found) return "An entity below this one is already a network object.\nA network object cannot contain another one.";
		}
		return nullptr;
	}

	bool NetworkAuthoringInspector::Draw(World& world, Entity entity, NetworkObjectAuthoring& authoring,
		const ComponentRegistry& components, const ReplicationRegistry& serializers)
	{
		bool changed = false;
		const EditorTheme::Palette& palette = EditorTheme::GetPalette();
		const std::vector<AuthoringComponentOption> options = AuthoringOptions(world, entity, components, serializers);

		ImGui::TextWrapped("Each component keeps its scene or prefab value on every machine (Local) unless it is set here.");

		// ---- the policy table ---------------------------------------------------------------
		constexpr ImGuiTableFlags kTable = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
		if (!options.empty() && ImGui::BeginTable("##network_policies", 4, kTable))
		{
			ImGui::TableSetupColumn("Component", ImGuiTableColumnFlags_WidthStretch, 1.1f);
			ImGui::TableSetupColumn("Network state", ImGuiTableColumnFlags_WidthStretch, 1.4f);
			ImGui::TableSetupColumn("Runs on", ImGuiTableColumnFlags_WidthStretch, 1.0f);
			ImGui::TableSetupColumn("Sync on/off", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();

			for (const AuthoringComponentOption& option : options)
			{
				ImGui::PushID(option.name.c_str());
				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(option.name.c_str());
				if (!option.onRoot) ImGui::SetItemTooltip("On an entity below this one.");

				// Network state: Local, or Replicated with one of its serializers.
				ImGui::TableSetColumnIndex(1);
				std::vector<Choice> states{ { "Local", "" } };
				int state = 0;
				for (std::size_t i = 0; i < option.serializers.size(); ++i)
				{
					states.push_back({ "Replicated: " + option.serializers[i], option.noReplication });
					if (Contains(authoring.replicated, option.serializers[i])) state = static_cast<int>(i + 1);
				}
				if (option.serializers.empty())
				{
					ImGui::TextDisabled("Local");
					ImGui::SetItemTooltip("%s", option.noReplication.c_str());
				}
				else if (ComboWithReasons("##state", state, states))
				{
					// Its old variant goes; the new one keeps the place in record order (the end).
					for (const std::string& s : option.serializers) Erase(authoring.replicated, s);
					if (state > 0) authoring.replicated.push_back(option.serializers[static_cast<std::size_t>(state - 1)]);
					changed = true;
				}

				// Runs on: everywhere, the host only, or the owner only.
				ImGui::TableSetColumnIndex(2);
				int runsOn = Contains(authoring.serverOnly, option.name) ? 1 : (Contains(authoring.ownerLocalOnly, option.name) ? 2 : 0);
				const std::vector<Choice> places{ { "Everywhere", "" }, { "Host only", option.noPolicy }, { "Owner only", option.noPolicy } };
				if (ComboWithReasons("##runs_on", runsOn, places))
				{
					Erase(authoring.serverOnly, option.name);
					Erase(authoring.ownerLocalOnly, option.name);
					if (runsOn == 1) authoring.serverOnly.push_back(option.name);
					if (runsOn == 2) authoring.ownerLocalOnly.push_back(option.name);
					changed = true;
				}

				// Sync on/off: the root component's enabled state is replicated (EnabledMask).
				ImGui::TableSetColumnIndex(3);
				bool synced = Contains(authoring.enabledReplicated, option.name);
				ImGui::BeginDisabled(!option.noEnabledSync.empty() && !synced);
				if (ImGui::Checkbox("##sync", &synced))
				{
					if (synced) authoring.enabledReplicated.push_back(option.name);
					else Erase(authoring.enabledReplicated, option.name);
					const bool wantMask = !authoring.enabledReplicated.empty();
					if (wantMask && !Contains(authoring.replicated, kEnabledMask)) authoring.replicated.push_back(kEnabledMask);
					if (!wantMask) Erase(authoring.replicated, kEnabledMask);
					changed = true;
				}
				ImGui::EndDisabled();
				if (!option.noEnabledSync.empty()) ImGui::SetItemTooltip("%s", option.noEnabledSync.c_str());
				ImGui::PopID();
			}
			ImGui::EndTable();
		}

		// ---- settings that name something this object does not have --------------------------
		const auto offered = [&](const std::string& name) {
			return std::ranges::any_of(options, [&](const AuthoringComponentOption& o) { return o.name == name; });
		};
		const auto isSerializerOfOffered = [&](const std::string& name) {
			if (name == kEnabledMask) return true;
			return std::ranges::any_of(options, [&](const AuthoringComponentOption& o) { return Contains(o.serializers, name); });
		};
		struct Stale { std::vector<std::string>* list; std::string name; const char* field; };
		std::vector<Stale> stale;
		for (const std::string& n : authoring.replicated) if (!isSerializerOfOffered(n)) stale.push_back({ &authoring.replicated, n, "replicated" });
		for (const std::string& n : authoring.serverOnly) if (!offered(n)) stale.push_back({ &authoring.serverOnly, n, "host only" });
		for (const std::string& n : authoring.ownerLocalOnly) if (!offered(n)) stale.push_back({ &authoring.ownerLocalOnly, n, "owner only" });
		for (const std::string& n : authoring.enabledReplicated) if (!offered(n)) stale.push_back({ &authoring.enabledReplicated, n, "sync on/off" });
		for (std::size_t i = 0; i < stale.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));
			ImGui::TextColored(palette.warning, "%s: '%s' is not on this object", stale[i].field, stale[i].name.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove")) { Erase(*stale[i].list, stale[i].name); changed = true; }
			ImGui::PopID();
		}

		// ---- scheduling and spawning ----------------------------------------------------------
		ImGui::Spacing();
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::DragFloat("Send priority", &authoring.sendPriority, 0.05f, 0.01f, kMaxSendPriority, "%.2f", ImGuiSliderFlags_AlwaysClamp)) changed = true;
		HelpMarker("How strongly this object competes for each client's snapshot budget while it waits (default 1). Host only: never sent.");
		if (ImGui::Checkbox("Critical", &authoring.critical)) changed = true;
		HelpMarker("Sent in every snapshot, ahead of the budget (a player, a boss, the ball). Use sparingly.");
		if (ImGui::Checkbox("Allow scene placement", &authoring.allowScenePlacement)) changed = true;
		HelpMarker("In a saved scene this object is a networked scene object: Host and Client keep their own copies and bind them. "
			"A scene entity can have these settings without a prefab, and a prefab instance can change them: it then uses its own.");
		if (ImGui::Checkbox("Spawn per player", &authoring.spawnPerPlayer)) changed = true;
		HelpMarker("The host spawns one for itself and one for each client that joins, owned by that player. One prefab in the project may have this.");

		// ---- what a session start would say -----------------------------------------------------
		ImGui::Spacing();
		const std::vector<std::string> problems = CheckAuthoring(world, entity, authoring, components, serializers);
		if (problems.empty())
		{
			ImGui::TextColored(palette.success, "Network settings are valid");
		}
		else
		{
			for (const std::string& p : problems)
			{
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextColored(palette.error, "%s", p.c_str());
				ImGui::PopTextWrapPos();
			}
		}
		return changed;
	}
}
