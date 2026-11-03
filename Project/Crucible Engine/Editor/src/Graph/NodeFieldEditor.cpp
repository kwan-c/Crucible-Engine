/*************************************************************************
* \file:         NodeFieldEditor.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                RTTR-driven field widgets and summaries.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "NodeFieldEditor.h"

// STL
#include <cctype>       // std::toupper
#include <cstdio>       // std::snprintf
#include <string_view>
#include <vector>

// ImGui
#include <imgui.h>
#include "misc/cpp/imgui_stdlib.h"   // ImGui::InputText(std::string*)

// RTTR
#include <rttr/enumeration.h>
#include <rttr/property.h>
#include <rttr/type.h>

// Crucible engine
#include "AI/Blackboard.h"

namespace Crucible::NodeFieldEditor {

    namespace {

        // "seconds" -> "Seconds", for the row label.
        std::string Label(std::string_view name) {

            std::string label(name);
            if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
            return label;

        }   // Label()

        // Is this RTTR type one of our NodeValue<T>s? They're the classes with a "variable" property.
        bool IsNodeValue(const rttr::type& type) {

            return type.is_class() && type.get_property("variable").is_valid();

        }   // IsNodeValue()

        // Which blackboard kind (BlackboardValue's variant index) a NodeValue
        // holds - so the variable list only offers variables of the right kind.
        // NodeValue<Entity> has no "value" (entities can't be stored), so no
        // value property means Entity.
        std::size_t KindOf(const rttr::type& nodeValueType) {

            const rttr::property value = nodeValueType.get_property("value");
            if (!value.is_valid()) return 0;    // Entity
            const rttr::type inner = value.get_type();
            if (inner == rttr::type::get<Math::vec3>()) return 1;
            if (inner == rttr::type::get<float>())      return 2;
            if (inner == rttr::type::get<int>())        return 3;
            if (inner == rttr::type::get<bool>())       return 4;
            return 0;

        }   // KindOf()

        // A plain value's widget. True if it changed (`value` then holds the new one).
        bool DrawPlain(const char* label, rttr::variant& value) {

            const rttr::type type = value.get_type();

            if (type == rttr::type::get<bool>()) {
                bool v = value.get_value<bool>();
                if (ImGui::Checkbox(label, &v)) { value = v; return true; }
            }
            else if (type == rttr::type::get<int>()) {
                int v = value.get_value<int>();
                if (ImGui::DragInt(label, &v)) { value = v; return true; }
            }
            else if (type == rttr::type::get<float>()) {
                float v = value.get_value<float>();
                if (ImGui::DragFloat(label, &v, 0.05f)) { value = v; return true; }
            }
            else if (type == rttr::type::get<std::string>()) {
                std::string v = value.get_value<std::string>();
                if (ImGui::InputText(label, &v)) { value = v; return true; }
            }
            else if (type == rttr::type::get<Math::vec3>()) {
                Math::vec3 v = value.get_value<Math::vec3>();
                if (ImGui::DragFloat3(label, &v.x, 0.1f)) { value = v; return true; }
            }
            else if (type.is_enumeration()) {
                const rttr::enumeration enumeration = type.get_enumeration();
                const std::string current = enumeration.value_to_name(value).to_string();
                if (ImGui::BeginCombo(label, current.c_str())) {
                    bool changed = false;
                    for (const rttr::string_view name : enumeration.get_names()) {
                        if (ImGui::Selectable(name.data(), name == current)) { value = enumeration.name_to_value(name); changed = true; }
                    }
                    ImGui::EndCombo();
                    return changed;
                }
            }
            else {
                ImGui::TextDisabled("%s: (no editor for this kind of field)", label);
            }
            return false;

        }   // DrawPlain()

        /*
            A NodeValue: the typed value's widget, or - linked - a list of the
            graph's variables of the matching kind. The "Var" button switches
            between the two. Entity fields are always linked (an entity can't be
            typed into a graph asset).
        */
        bool DrawNodeValue(const char* label, rttr::variant& value, const Blackboard& variables) {

            const rttr::type     type         = value.get_type();
            const rttr::property variableProp = type.get_property("variable");
            const rttr::property valueProp    = type.get_property("value");
            rttr::instance       inner(value);

            std::string variable = variableProp.get_value(inner).get_value<std::string>();
            const std::size_t kind = KindOf(type);
            const bool canType = valueProp.is_valid();
            const bool linked  = !variable.empty() || !canType;
            bool changed = false;

            // The variables this field could use.
            std::vector<const std::string*> matching;
            for (const BlackboardVariable& candidate : variables.Variables()) {
                if (candidate.value.index() == kind) matching.push_back(&candidate.name);
            }

            // The link toggle, first on the row so it never moves.
            if (canType) {
                if (ImGui::SmallButton(linked ? "Var" : "Val")) {
                    if (linked) { variable.clear(); changed = true; }              // back to a typed value
                    else if (!matching.empty()) { variable = *matching.front(); changed = true; }
                }
                ImGui::SetItemTooltip(linked ? "Using a blackboard variable. Click to type a value instead."
                                             : matching.empty() ? "Typed value. Add a variable of this kind to link one."
                                                                : "Typed value. Click to use a blackboard variable instead.");
                ImGui::SameLine();
            }

            if (linked) {
                const char* shown = variable.empty() ? "(choose a variable)" : variable.c_str();
                if (ImGui::BeginCombo(label, shown)) {
                    for (const std::string* name : matching) {
                        if (ImGui::Selectable(name->c_str(), *name == variable)) { variable = *name; changed = true; }
                    }
                    if (matching.empty()) ImGui::TextDisabled("No variables of this kind yet");
                    ImGui::EndCombo();
                }
            } else {
                rttr::variant typed = valueProp.get_value(inner);
                if (DrawPlain(label, typed)) { valueProp.set_value(inner, typed); changed = true; }
            }

            if (changed) variableProp.set_value(inner, variable);
            return changed;

        }   // DrawNodeValue()

        std::string PlainText(const rttr::variant& value) {

            const rttr::type type = value.get_type();
            char buffer[64];
            if (type == rttr::type::get<bool>())   return value.get_value<bool>() ? "on" : "off";
            if (type == rttr::type::get<int>())    return std::to_string(value.get_value<int>());
            if (type == rttr::type::get<float>())  { std::snprintf(buffer, sizeof buffer, "%g", value.get_value<float>()); return buffer; }
            if (type == rttr::type::get<std::string>()) return "\"" + value.get_value<std::string>() + "\"";
            if (type == rttr::type::get<Math::vec3>()) {
                const Math::vec3 v = value.get_value<Math::vec3>();
                std::snprintf(buffer, sizeof buffer, "(%g, %g, %g)", v.x, v.y, v.z);
                return buffer;
            }
            if (type.is_enumeration()) return type.get_enumeration().value_to_name(value).to_string();
            return "?";

        }   // PlainText()

    }   // ANONYMOUS NAMESPACE

    bool Draw(rttr::instance object, const Blackboard& variables, bool readOnly) {

        bool changed = false;
        ImGui::BeginDisabled(readOnly);
        for (const rttr::property& property : object.get_type().get_properties()) {

            rttr::variant value = property.get_value(object);
            const std::string label = Label(property.get_name().to_string());

            ImGui::PushID(label.c_str());
            const bool edited = IsNodeValue(value.get_type()) ? DrawNodeValue(label.c_str(), value, variables)
                                                              : DrawPlain(label.c_str(), value);
            if (edited) { property.set_value(object, value); changed = true; }
            ImGui::PopID();

        }
        ImGui::EndDisabled();
        return changed;

    }   // Draw()

    std::string Summary(rttr::instance object) {

        std::string summary;
        for (const rttr::property& property : object.get_type().get_properties()) {

            const rttr::variant value = property.get_value(object);
            std::string text;
            if (IsNodeValue(value.get_type())) {
                rttr::variant  copy = value;
                rttr::instance inner(copy);
                const std::string variable = value.get_type().get_property("variable").get_value(inner).get_value<std::string>();
                const rttr::property typed = value.get_type().get_property("value");
                text = !variable.empty() ? variable : typed.is_valid() ? PlainText(typed.get_value(inner)) : "(no variable)";
            } else {
                text = PlainText(value);
            }

            if (!summary.empty()) summary += ", ";
            summary += property.get_name().to_string() + " " + text;

        }
        return summary;

    }   // Summary()

}   // NAMESPACE CRUCIBLE::NODEFIELDEDITOR
