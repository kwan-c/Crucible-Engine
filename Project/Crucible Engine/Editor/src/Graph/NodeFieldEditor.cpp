/*************************************************************************
* \file:         NodeFieldEditor.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       2.0
* \brief
*                Field widgets and summaries for the C# nodes.
*
* \history
*    -v1.0       first version (RTTR)
*    -v2.0       driven by NodeCatalog; values as text
*
**************************************************************************/
#include "NodeFieldEditor.h"

// STL
#include <algorithm>    // std::equal, std::find_if
#include <cctype>       // std::tolower
#include <charconv>     // std::from_chars
#include <cstdint>
#include <filesystem>
#include <format>
#include <string_view>
#include <vector>

// ImGui
#include <imgui.h>
#include "misc/cpp/imgui_stdlib.h"   // ImGui::InputText(std::string*)

// Crucible engine
#include "AI/BehaviorNode.h"
#include "AI/Blackboard.h"
#include "AssetPipe/Assetmanager.h"
#include "Core.h"

// Crucible editor
#include "ImGui/EditorWidgets.h"                    // kAssetPayload: an asset dragged from the Asset Browser
#include "ImGui/ImGuiManager.h"                     // EditorManager: the Core and the panels
#include "ImGui/Panels/ImGuiPanel_BehaviorGraph.h"  // Open: shows the chosen graph

namespace Crucible::NodeFieldEditor {

    namespace {

        // Draw's readOnly, for the one widget (the graph picker's Open) that steps
        // out of the read-only block and must step back into it the same way.
        bool s_readOnly = false;

        // --- Text <-> values (the format NodeHost.cs reads) ----------------- //

        float ToFloat(std::string_view text) {

            float value = 0.0f;
            std::from_chars(text.data(), text.data() + text.size(), value);
            return value;

        }   // ToFloat()

        int ToInt(std::string_view text) {

            int value = 0;
            std::from_chars(text.data(), text.data() + text.size(), value);
            return value;

        }   // ToInt()

        Math::vec3 ToVec3(std::string_view text) {

            Math::vec3 value(0.0f);
            for (int i = 0; i < 3; ++i) {
                const std::size_t comma = text.find(',');
                value[i] = ToFloat(text.substr(0, comma));
                if (comma == std::string_view::npos) break;
                text.remove_prefix(comma + 1);
            }
            return value;

        }   // ToVec3()

        std::string FloatText(float value) { return std::format("{}", value); }
        std::string Vec3Text(const Math::vec3& v) { return FloatText(v.x) + "," + FloatText(v.y) + "," + FloatText(v.z); }

        // Which blackboard kind (BlackboardValue's variant index) a field of
        // this type can link to, so the list only offers matching variables.
        std::size_t VariableKindOf(std::string_view type) {

            if (type == "Vector3") return 1;
            if (type == "float")   return 2;
            if (type == "int")     return 3;
            if (type == "bool")    return 4;
            return 0;   // Entity

        }   // VariableKindOf()

        // A .behaviorgraph's Assets path from its id (as text), or "" if there's none.
        std::string GraphPath(const std::string& idText) {

            std::uint64_t id = 0;
            std::from_chars(idText.data(), idText.data() + idText.size(), id);
            Core* core = EditorManager::GetInstance().GetCore();
            const AssetEntry* entry = (core && id != 0) ? core->GetAssets().FindByGuid(xresource::instance_guid{ id }) : nullptr;
            return entry ? entry->virtualPath : std::string();

        }   // GraphPath()

        // A .behaviorgraph field: a list of them, a drop target for one dragged
        // from the Asset Browser, and Open. True if `text` (the asset id) changed.
        bool DrawGraphPicker(const char* label, std::string& text) {

            Core* core = EditorManager::GetInstance().GetCore();
            if (!core) return false;
            bool changed = false;

            const std::string path = GraphPath(text);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
            if (ImGui::BeginCombo(label, path.empty() ? "(choose a .behaviorgraph)" : path.c_str())) {
                for (const AssetEntry& entry : core->GetAssets().GetAll()) {
                    if (entry.type != AssetType::BehaviorGraph) continue;
                    const std::string id = std::to_string(entry.guid.m_Value);
                    if (ImGui::Selectable(entry.virtualPath.c_str(), id == text)) { text = id; changed = true; }
                }
                ImGui::EndCombo();
            }
            if (ImGui::BeginDragDropTarget()) {
                const ImGuiPayload* payload = ImGui::GetDragDropPayload();
                if (payload && payload->IsDataType(EditorWidgets::kAssetPayload)
                    && std::string_view(static_cast<const char*>(payload->Data)).ends_with(".behaviorgraph")) {
                    if (const ImGuiPayload* dropped = ImGui::AcceptDragDropPayload(EditorWidgets::kAssetPayload)) {
                        if (const AssetEntry* entry = core->GetAssets().Find(static_cast<const char*>(dropped->Data))) {
                            text = std::to_string(entry->guid.m_Value);
                            changed = true;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // Open works read-only too (during Play), so it's drawn outside the disabled block.
            ImGui::SameLine();
            ImGui::EndDisabled();
            ImGui::BeginDisabled(path.empty());
            if (ImGui::SmallButton("Open")) {
                if (auto panel = EditorManager::GetInstance().GetPanel<BehaviorGraphPanel>(ImguiPanels::BEHAVIORGRAPH)) {
                    std::uint64_t id = 0;
                    std::from_chars(text.data(), text.data() + text.size(), id);
                    panel->RequestOpen(id);
                }
            }
            ImGui::EndDisabled();
            ImGui::BeginDisabled(s_readOnly);
            return changed;

        }   // DrawGraphPicker()

        // A plain value's widget. True if `text` changed.
        bool DrawValue(const char* label, const NodeFieldInfo& info, std::string& text) {

            if (info.type == "BehaviorGraph") return DrawGraphPicker(label, text);
            if (info.type == "bool") {
                bool v = text == "true";
                if (ImGui::Checkbox(label, &v)) { text = v ? "true" : "false"; return true; }
            }
            else if (info.type == "int") {
                int v = ToInt(text);
                if (ImGui::DragInt(label, &v)) { text = std::to_string(v); return true; }
            }
            else if (info.type == "float") {
                float v = ToFloat(text);
                if (ImGui::DragFloat(label, &v, 0.05f)) { text = FloatText(v); return true; }
            }
            else if (info.type == "string") {
                return ImGui::InputText(label, &text);
            }
            else if (info.type == "Vector3") {
                Math::vec3 v = ToVec3(text);
                if (ImGui::DragFloat3(label, &v.x, 0.1f)) { text = Vec3Text(v); return true; }
            }
            else if (info.type == "enum") {
                bool changed = false;
                if (ImGui::BeginCombo(label, text.c_str())) {
                    for (const std::string& option : info.options) {
                        if (ImGui::Selectable(option.c_str(), option == text)) { text = option; changed = true; }
                    }
                    ImGui::EndCombo();
                }
                return changed;
            }
            else {
                ImGui::TextDisabled("%s: (no editor for this kind of field)", label);
            }
            return false;

        }   // DrawValue()

        /*
            A BlackboardVariable: its own value's widget, or - linked - a list
            of the graph's variables of the matching kind. The "Var" button
            switches between the two. Entity fields are always linked.
        */
        bool DrawLinkable(const char* label, const NodeFieldInfo& info, NodeField& field, const Blackboard& variables) {

            const std::size_t kind    = VariableKindOf(info.type);
            const bool        canType = info.type != "Entity";
            const bool        linked  = !field.link.empty() || !canType;
            bool changed = false;

            std::vector<const std::string*> matching;
            for (const BlackboardVariable& candidate : variables.Variables()) {
                if (candidate.value.index() == kind) matching.push_back(&candidate.name);
            }

            // The link toggle, first on the row so it never moves.
            if (canType) {
                if (ImGui::SmallButton(linked ? "Var" : "Val")) {
                    if (linked) { field.link.clear(); changed = true; }              // back to its own value
                    else if (!matching.empty()) { field.link = *matching.front(); changed = true; }
                }
                ImGui::SetItemTooltip(linked ? "Using a graph variable. Click to give it its own value instead."
                                             : matching.empty() ? "Its own value. Add a variable of this kind to link one."
                                                                : "Its own value. Click to use a graph variable instead.");
                ImGui::SameLine();
            }

            if (linked) {
                const char* shown = field.link.empty() ? "(choose a variable)" : field.link.c_str();
                if (ImGui::BeginCombo(label, shown)) {
                    for (const std::string* name : matching) {
                        if (ImGui::Selectable(name->c_str(), *name == field.link)) { field.link = *name; changed = true; }
                    }
                    if (matching.empty()) ImGui::TextDisabled("No variables of this kind yet");
                    ImGui::EndCombo();
                }
            }
            else if (DrawValue(label, info, field.text)) {
                changed = true;
            }
            return changed;

        }   // DrawLinkable()

        std::string ValueText(std::string_view type, const std::string& text) {

            if (type == "bool")    return text == "true" ? "on" : "off";
            if (type == "string")  return "\"" + text + "\"";
            if (type == "Vector3") { const Math::vec3 v = ToVec3(text); return std::format("({}, {}, {})", v.x, v.y, v.z); }
            if (type == "BehaviorGraph") {
                const std::string path = GraphPath(text);
                return path.empty() ? "(none)" : std::filesystem::path(path).stem().string();   // the file's name is enough on a node box
            }
            return text;

        }   // ValueText()

    }   // ANONYMOUS NAMESPACE

    bool Draw(BehaviorNode& node, const Blackboard& variables, bool readOnly) {

        const NodeTypeInfo* info = node.Info();
        if (!info) {
            // Unknown to C#: show what's stored so nothing seems lost, but don't
            // guess at widgets for it.
            ImGui::TextColored({ 1.0f, 0.7f, 0.4f, 1.0f }, "No C# node \"%s\" - its values are kept as they are.", node.TypeId().c_str());
            for (const NodeField& field : node.Fields()) {
                ImGui::TextDisabled("%s: %s", field.name.c_str(), field.link.empty() ? field.text.c_str() : ("-> " + field.link).c_str());
            }
            return false;
        }

        bool changed = false;
        s_readOnly = readOnly;
        ImGui::BeginDisabled(readOnly);
        for (const NodeFieldInfo& fieldInfo : info->fields) {

            NodeField& field = node.FieldFor(fieldInfo);   // adds the C# default if the graph predates the field
            field.kind     = FieldKindOf(fieldInfo.type);   // the C# type decides how it's saved
            field.linkable = fieldInfo.linkable;

            const std::string& label = fieldInfo.label.empty() ? fieldInfo.name : fieldInfo.label;
            ImGui::PushID(fieldInfo.name.c_str());
            if (fieldInfo.linkable ? DrawLinkable(label.c_str(), fieldInfo, field, variables) : DrawValue(label.c_str(), fieldInfo, field.text)) changed = true;
            ImGui::PopID();

        }
        ImGui::EndDisabled();
        return changed;

    }   // Draw()

    std::string Summary(const BehaviorNode& node) {

        const NodeTypeInfo* info = node.Info();
        if (!info) return {};

        // A field's value as the box shows it: the variable it's linked to, or its own value.
        const auto shownValue = [&](const NodeFieldInfo& fieldInfo) {
            const NodeField* field = node.FindField(fieldInfo.name);
            const std::string& link = field ? field->link : fieldInfo.link;
            const std::string& text = field ? field->text : fieldInfo.value;
            return !link.empty() ? link : fieldInfo.type == "Entity" ? std::string("(no variable)") : ValueText(fieldInfo.type, text);
        };

        // Unity's story: "[Agent] navigates to [Location]" with each [Word] replaced by
        // the field of that name (any case). A word with no such field shows as plain text.
        if (!info->story.empty()) {
            std::string story;
            for (std::size_t at = 0; at < info->story.size();) {
                const std::size_t open  = info->story.find('[', at);
                const std::size_t close = open == std::string::npos ? std::string::npos : info->story.find(']', open);
                if (close == std::string::npos) { story += info->story.substr(at); break; }
                story += info->story.substr(at, open - at);
                const std::string word = info->story.substr(open + 1, close - open - 1);
                const auto same = [&](const std::string& name) {
                    return name.size() == word.size() && std::equal(name.begin(), name.end(), word.begin(),
                        [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
                };
                const auto field = std::find_if(info->fields.begin(), info->fields.end(), [&](const NodeFieldInfo& f) { return same(f.name) || same(f.label); });
                story += field != info->fields.end() ? shownValue(*field) : word;
                at = close + 1;
            }
            return story;
        }

        std::string summary;
        for (const NodeFieldInfo& fieldInfo : info->fields) {
            if (!summary.empty()) summary += ", ";
            summary += (fieldInfo.label.empty() ? fieldInfo.name : fieldInfo.label) + " " + shownValue(fieldInfo);
        }
        return summary;

    }   // Summary()

}   // NAMESPACE CRUCIBLE::NODEFIELDEDITOR
