/*************************************************************************
* \file:         NodeScriptDialog.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          5 November 2026
* \version       1.0
* \brief
*                Writing a new node script from its template.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "NodeScriptDialog.h"

// STL
#include <array>
#include <cctype>       // std::isalnum, std::toupper, std::tolower
#include <cstdio>       // std::snprintf
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>
#include <vector>

// ImGui
#include <imgui.h>

// Crucible engine
#include "AI/NodeCatalog.h"
#include "AssetPipe/Assetmanager.h"
#include "Core.h"

// Crucible editor
#include "Graph/InspectScript.h"
#include "ImGui/ImGuiManager.h"   // EditorManager: the Core, for the project folder

namespace Crucible {

    namespace {

        struct KindInfo {

            const char* label;      // "Action"
            const char* suffix;     // added to the class name: AttackPlayerAction
            const char* category;   // the Add Node submenu it starts in
            const char* templateFile;

        };  // KINDINFO STRUCT

        // Default categories in Unity's layout ('/' nests: "Action/Navigation" is a submenu of Action).
        constexpr std::array<KindInfo, 4> kKinds{ {
            { "Action",    "Action",    "Action",     "Action.cs.txt" },
            { "Modifier",  "Modifier",  "Flow",       "Modifier.cs.txt" },
            { "Composite", "Composite", "Flow",       "Composite.cs.txt" },
            { "Condition", "Condition", "Conditions", "Condition.cs.txt" },
        } };

        const KindInfo& InfoOf(NodeScriptDialog::Kind kind) { return kKinds[static_cast<std::size_t>(kind)]; }

        // The name split into words of letters and digits: "attack player!" -> attack, player.
        std::vector<std::string> Words(const std::string& name) {

            std::vector<std::string> words(1);
            for (const char c : name) {
                if (std::isalnum(static_cast<unsigned char>(c))) words.back() += c;
                else if (!words.back().empty()) words.emplace_back();
            }
            if (words.back().empty()) words.pop_back();
            return words;

        }   // Words()

        // A C# string literal's contents: quotes and backslashes escaped, line breaks dropped.
        std::string Escape(const std::string& text) {

            std::string out;
            for (const char c : text) {
                if (c == '"' || c == '\\') out += '\\';
                if (c != '\n' && c != '\r') out += c;
            }
            return out;

        }   // Escape()

    }   // ANONYMOUS NAMESPACE

    void NodeScriptDialog::Open(Kind kind) {

        m_kind          = kind;
        m_openRequested = true;
        m_name[0]       = '\0';
        m_description[0] = '\0';
        m_story[0]      = '\0';
        std::snprintf(m_category, sizeof m_category, "%s", InfoOf(kind).category);   // snprintf always ends the text, unlike strncpy
        m_error.clear();

    }   // NodeScriptDialog::Open()

    bool NodeScriptDialog::Create(std::string& id) {

        const KindInfo& info = InfoOf(m_kind);
        const std::vector<std::string> words = Words(m_name);
        if (words.empty() || std::isdigit(static_cast<unsigned char>(words.front()[0]))) {
            m_error = "Give it a name that starts with a letter, like \"Attack Player\".";
            return false;
        }

        // "attack player" -> class AttackPlayerAction, id "attack_player".
        std::string className;
        id.clear();
        for (const std::string& word : words) {
            className += static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
            className += word.substr(1);
            if (!id.empty()) id += '_';
            for (const char c : word) id += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (!className.ends_with(info.suffix)) className += info.suffix;
        if (NodeCatalog::Instance().Find(id)) { m_error = "There's already a node with the id \"" + id + "\". Pick another name."; return false; }

        Core* core = EditorManager::GetInstance().GetCore();
        if (!core) { m_error = "The Editor isn't ready."; return false; }
        const std::filesystem::path project  = std::filesystem::path(core->GetAssets().GetAssetRoot()).parent_path();
        const std::filesystem::path scripts  = project / "Assets" / "Scripts";
        const std::filesystem::path folder   = scripts / "AI";
        const std::filesystem::path file     = folder / (className + ".cs");

        try {

            std::ifstream source(project / "ManagedScripts" / "Templates" / info.templateFile, std::ios::binary);
            if (!source) { m_error = std::string("The template ManagedScripts/Templates/") + info.templateFile + " is missing."; return false; }
            std::string text{ std::istreambuf_iterator<char>(source), {} };

            const std::vector<std::pair<std::string, std::string>> tokens{
                { "{{CLASS}}", className }, { "{{NAME}}", Escape(m_name) }, { "{{DESCRIPTION}}", Escape(m_description) },
                { "{{STORY}}", Escape(m_story) }, { "{{CATEGORY}}", Escape(m_category) }, { "{{ID}}", id } };
            for (const auto& [token, value] : tokens) {
                for (auto at = text.find(token); at != std::string::npos; at = text.find(token, at + value.size())) text.replace(at, token.size(), value);
            }

            // All of Assets/Scripts compiles into one DLL: the same class name in
            // two folders would break the build, so refuse it anywhere in there.
            std::error_code walk;
            for (auto it = std::filesystem::recursive_directory_iterator(scripts, walk); !walk && it != std::filesystem::recursive_directory_iterator(); it.increment(walk)) {
                if (it->is_regular_file(walk) && it->path().filename() == file.filename()) {
                    m_error = "A script called " + className + ".cs already exists in Assets/Scripts.";
                    return false;
                }
            }

            std::filesystem::create_directories(folder);
            std::ofstream out(file, std::ios::out | std::ios::binary | std::ios::noreplace);   // never overwrite a script
            if (!out) { m_error = "Could not create " + file.generic_string(); return false; }
            out << text;
            out.close();
            if (!out) { m_error = "Writing " + file.generic_string() + " failed."; return false; }

        }
        catch (const std::exception& exception) { m_error = exception.what(); return false; }

        InspectScript::OpenFile(file.string().c_str());   // straight into the code, like Unity
        return true;

    }   // NodeScriptDialog::Create()

    std::string NodeScriptDialog::Draw() {

        const KindInfo& info  = InfoOf(m_kind);
        const std::string title = std::string("Create new ") + info.label + "###CreateNodeScript";
        if (m_openRequested) { ImGui::OpenPopup(title.c_str()); m_openRequested = false; }

        std::string created;
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
        if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {

            ImGui::TextWrapped("Writes a C# script for a new %s in Assets/Scripts/AI and opens it. "
                               "It appears in the graph once the scripts have compiled.", info.label);
            ImGui::Separator();

            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(320.0f);
            ImGui::InputTextWithHint("Name", "Attack Player", m_name, sizeof m_name);
            ImGui::SetNextItemWidth(320.0f);
            ImGui::InputTextWithHint("Story", "Self attacks [Target]", m_story, sizeof m_story);
            ImGui::SetItemTooltip("The sentence on the node, as in Unity. A [Word] shows the field of that name - add the field to the script.");
            ImGui::SetNextItemWidth(320.0f);
            ImGui::InputText("Category", m_category, sizeof m_category);
            ImGui::SetItemTooltip("The Add Node submenu it goes in. A '/' nests, as in Unity: Action/Combat.");
            ImGui::SetNextItemWidth(320.0f);
            ImGui::InputTextWithHint("Description", "What it does, shown as its tooltip", m_description, sizeof m_description);

            if (!m_error.empty()) ImGui::TextColored({ 1.0f, 0.55f, 0.43f, 1.0f }, "%s", m_error.c_str());

            if (ImGui::Button("Create")) {
                std::string id;
                if (Create(id)) { created = id; ImGui::CloseCurrentPopup(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();

        }
        return created;

    }   // NodeScriptDialog::Draw()

}   // NAMESPACE CRUCIBLE
