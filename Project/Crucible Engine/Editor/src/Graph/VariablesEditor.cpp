/*************************************************************************
* \file:         VariablesEditor.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The shared blackboard variables list.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "VariablesEditor.h"

// STL
#include <array>
#include <vector>

// ImGui
#include <imgui.h>
#include "misc/cpp/imgui_stdlib.h"   // ImGui::InputText(std::string*)

// Crucible engine
#include "AI/Blackboard.h"

namespace Crucible {

    namespace {

        // The blackboard kinds by name, in BlackboardValue's order.
        constexpr std::array<const char*, 6> kKindNames{ "Entity", "Vector3", "Float", "Int", "Bool", "Entity List" };

        // A fresh value of kind `index`, for when a variable's kind is changed.
        BlackboardValue DefaultOfKind(std::size_t index) {

            switch (index) {
                case 1:  return Math::vec3(0.0f);
                case 2:  return 0.0f;
                case 3:  return 0;
                case 4:  return false;
                case 5:  return std::vector<Entity>{};
                default: return Entity{};
            }

        }   // DefaultOfKind()

    }   // ANONYMOUS NAMESPACE

    bool VariablesEditor::Draw(Blackboard& variables, bool readOnly, std::string& message) {

        ImGui::TextUnformatted("Variables");
        ImGui::SetItemTooltip("This graph's memory. Each enemy gets its own copy.\n\"Self\" is always there: the enemy itself.\nEntities can't be stored in the file - they're set per enemy.");
        ImGui::Separator();

        bool changed = false;
        auto& list = variables.Variables();
        int removeAt = -1;

        ImGui::BeginDisabled(readOnly);
        for (int i = 0; i < static_cast<int>(list.size()); ++i) {

            BlackboardVariable& variable = list[static_cast<std::size_t>(i)];
            ImGui::PushID(i);

            ImGui::SetNextItemWidth(-28.0f);
            if (ImGui::InputText("##name", &variable.name)) changed = true;
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) removeAt = i;
            ImGui::SetItemTooltip("Delete this variable");

            // Kind: changing it resets the value to that kind's default.
            int kind = static_cast<int>(variable.value.index());
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##kind", &kind, kKindNames.data(), static_cast<int>(kKindNames.size()))) {
                variable.value = DefaultOfKind(static_cast<std::size_t>(kind));
                changed = true;
            }

            // Its default value (entities can't be stored in a graph asset).
            ImGui::SetNextItemWidth(-1.0f);
            if      (Math::vec3* v = std::get_if<Math::vec3>(&variable.value)) changed |= ImGui::DragFloat3("##value", &v->x, 0.1f);
            else if (float* f = std::get_if<float>(&variable.value))           changed |= ImGui::DragFloat("##value", f, 0.05f);
            else if (int* n = std::get_if<int>(&variable.value))               changed |= ImGui::DragInt("##value", n);
            else if (bool* b = std::get_if<bool>(&variable.value))             changed |= ImGui::Checkbox("Default", b);
            else ImGui::TextDisabled("set per enemy");

            if (variable.name == "Self") ImGui::TextColored({ 1.0f, 0.75f, 0.4f, 1.0f }, "\"Self\" is set to the enemy at Play.");

            ImGui::Separator();
            ImGui::PopID();

        }
        if (removeAt >= 0) { list.erase(list.begin() + removeAt); changed = true; }

        // Add: a name, then a Float to start with (change its kind above).
        ImGui::SetNextItemWidth(-48.0f);
        ImGui::InputTextWithHint("##new", "new variable", m_newName, sizeof m_newName);
        ImGui::SameLine();
        if (ImGui::Button("Add") && m_newName[0] != '\0') {
            if (variables.Has(m_newName)) message = std::string("There is already a variable called ") + m_newName;
            else { variables.Set(m_newName, 0.0f); m_newName[0] = '\0'; changed = true; }
        }
        ImGui::EndDisabled();

        return changed;

    }   // VariablesEditor::Draw()

}   // NAMESPACE CRUCIBLE
