/*************************************************************************
* \file:         InspectScript.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       2.0
* \brief
*                Opening a node type's source file, and copying a built-in's.
*
* \history
*    -v1.0       first version
*    -v2.0       built-ins read-only; MakeEditableCopy
*
**************************************************************************/
#include "InspectScript.h"

// STL
#include <array>
#include <cctype>       // std::tolower, std::isalnum, std::isspace
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <shellapi.h>   // ShellExecuteW
#endif

// Crucible engine
#include "AI/BehaviorNode.h"   // Info(): the node's catalog entry
#include "AssetPipe/Assetmanager.h"
#include "Core.h"
#include "Diagnostics/Logger.h"

// Crucible editor
#include "ImGui/ImGuiManager.h"   // EditorManager: the Core, for the asset root

namespace Crucible::InspectScript {

    namespace {

        // A path tidied and lower-cased, for comparing (Windows paths ignore case).
        std::string Key(const std::filesystem::path& path) {

            std::string text = path.lexically_normal().generic_string();
            for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;

        }   // Key()

        // Assets/Scripts: where designers' scripts live, and where copies go.
        std::filesystem::path ScriptsFolder() {

            Core* core = EditorManager::GetInstance().GetCore();
            return core ? std::filesystem::path(core->GetAssets().GetAssetRoot()) / "Scripts" : std::filesystem::path();

        }   // ScriptsFolder()

        bool ReadText(const std::filesystem::path& path, std::string& text) {

            std::ifstream in(path, std::ios::binary);
            if (!in) return false;
            text.assign(std::istreambuf_iterator<char>(in), {});
            return true;

        }   // ReadText()

        // "ArriveAction" -> "ArriveCustomAction": Custom goes before the kind's suffix.
        std::string CopyClassName(const std::string& original) {

            for (const std::string_view suffix : std::array<std::string_view, 5>{ "Action", "Composite", "Modifier", "Condition", "Node" }) {
                if (original.size() > suffix.size() && original.ends_with(suffix)) {
                    return original.substr(0, original.size() - suffix.size()) + "Custom" + std::string(suffix);
                }
            }
            return original + "Custom";

        }   // CopyClassName()

    }   // ANONYMOUS NAMESPACE

    void OpenFile(const char* path) {

#ifdef _WIN32
        const std::wstring file = std::filesystem::path(path).wstring();
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
            CRU_USER_WARN("Could not open {}", path);
        }
#else
        CRU_USER_WARN("Inspect Script is only implemented on Windows ({})", path);
#endif

    }   // OpenFile()

    bool IsBuiltIn(const NodeTypeInfo& type) {

        const std::filesystem::path scripts = ScriptsFolder();
        if (type.file.empty() || scripts.empty()) return false;
        return !Key(type.file).starts_with(Key(scripts) + "/");

    }   // IsBuiltIn()

    bool OpenTypeSource(const NodeTypeInfo& type) {

        if (type.file.empty()) return false;

        // A built-in opens read-only (Windows' read-only flag), so an edit can't
        // slip into the engine's library by accident. A programmer who means to
        // change it can still clear the flag; designers use Edit a Copy.
        if (IsBuiltIn(type)) {
            std::error_code ignored;
            std::filesystem::permissions(type.file, std::filesystem::perms::owner_write | std::filesystem::perms::group_write | std::filesystem::perms::others_write,
                                         std::filesystem::perm_options::remove, ignored);
            CRU_USER_INFO("'{}' is a built-in node, opened read-only. To change it, right-click the node > Edit a Copy.", type.name);
        }
        OpenFile(type.file.c_str());
        return true;

    }   // OpenTypeSource()

    bool OpenNodeSource(const BehaviorNode& node) {

        const NodeTypeInfo* info = node.Info();
        return info && OpenTypeSource(*info);

    }   // OpenNodeSource()

    bool CanCopy(const NodeTypeInfo& type) {

        if (!IsBuiltIn(type)) return false;

        // Read once per file: the side panels ask every frame. A script using
        // `internal` engine parts (Run Subgraph's hidden child) wouldn't compile
        // outside the engine's library, so it can't be copied.
        static std::unordered_map<std::string, bool> answers;
        const auto found = answers.find(type.file);
        if (found != answers.end()) return found->second;
        std::string text;
        const bool copyable = ReadText(type.file, text) && text.find("internal ") == std::string::npos;
        answers.emplace(type.file, copyable);
        return copyable;

    }   // CanCopy()

    std::string MakeEditableCopy(const NodeTypeInfo& type, std::string& error) {

        std::string text;
        if (!CanCopy(type) || !ReadText(type.file, text)) { error = type.name + " can't be copied."; return {}; }

        // Its new class, file, name and id. The id is new so graphs using the
        // built-in keep the built-in; only the node you copied from switches.
        const std::string oldClass = type.className.substr(type.className.find_last_of('.') + 1);
        const std::string newClass = CopyClassName(oldClass);
        const std::string newName  = type.name + " (Custom)";
        std::string newId = type.id + "_custom";
        for (int n = 2; NodeCatalog::Instance().Find(newId); ++n) newId = type.id + "_custom_" + std::to_string(n);

        const std::filesystem::path folder = ScriptsFolder() / "AI";
        const std::filesystem::path file   = folder / (newClass + ".cs");
        std::error_code exists;
        if (std::filesystem::exists(file, exists)) {
            error = newClass + ".cs already exists in Assets/Scripts/AI - drag it onto the node instead.";
            return {};
        }

        // 1. Into the designers' namespace, with what the built-in got for free
        //    by being inside Crucible.AI. Same line endings as the original.
        const std::string newline = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
        text = std::regex_replace(text, std::regex(R"(namespace\s+Crucible\.AI\b)"), "namespace Crucible.Scripts");
        text = "// Your copy of the built-in " + type.name + " node: change it freely." + newline +
               "using Crucible;" + newline + "using Crucible.AI;" + newline +
               "using Action = Crucible.AI.Action;   // System.Action has the same name" + newline + newline + text;

        // 2. The class renamed everywhere it's mentioned.
        text = std::regex_replace(text, std::regex("\\b" + oldClass + "\\b"), newClass);

        // 3. Its name and id in [NodeDescription] - the id is what graphs store.
        //    The id is replaced first: it comes later in the script, so replacing
        //    it doesn't move the name's position.
        const std::vector<AttributeValue> values = NodeDescriptionValues(text);
        const AttributeValue* idValue   = FindValue(values, "id", 3);
        const AttributeValue* nameValue = FindValue(values, "name", 0);
        if (!idValue || !nameValue || idValue->begin < nameValue->begin) {
            error = "Couldn't find the name and id in " + type.name + "'s [NodeDescription] to rename.";
            return {};
        }
        text.replace(idValue->begin, idValue->end - idValue->begin, newId);
        text.replace(nameValue->begin, nameValue->end - nameValue->begin, newName);

        try {
            std::filesystem::create_directories(folder);
            std::ofstream out(file, std::ios::out | std::ios::binary | std::ios::noreplace);
            if (!out) { error = "Could not create " + file.generic_string(); return {}; }
            out << text;
            out.close();
            if (!out) { error = "Writing " + file.generic_string() + " failed."; return {}; }
        }
        catch (const std::exception& exception) { error = exception.what(); return {}; }

        OpenFile(file.string().c_str());
        return newId;

    }   // MakeEditableCopy()

    std::vector<AttributeValue> NodeDescriptionValues(const std::string& script) {

        std::vector<AttributeValue> values;
        const std::size_t start = script.find("[NodeDescription(");
        if (start == std::string::npos) return values;

        int position = 0;
        bool continuing = false;   // the last thing outside quotes was '+': the next text joins the value before
        std::string word;          // the identifier just read outside quotes (a label, if ':' follows)
        std::string label;         // the label waiting for its value
        for (std::size_t i = start + 17; i < script.size(); ++i) {

            const char c = script[i];
            if (c == '"') {
                // A text value: find its end, stepping over \" escapes.
                std::size_t close = i + 1;
                while (close < script.size() && script[close] != '"') close += script[close] == '\\' ? 2 : 1;
                if (close >= script.size()) break;
                if (!continuing) {
                    AttributeValue value{ i + 1, close, label, label.empty() ? position++ : -1 };
                    values.push_back(value);
                }
                label.clear();
                continuing = false;
                i = close;
                continue;
            }
            if (c == ')' && i + 1 < script.size() && script[i + 1] == ']') break;   // the attribute's end, outside any text
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') { word += c; continue; }
            if (c == ':') label = word;
            if (c == '+') continuing = true;
            if (c == ',') continuing = false;
            if (!std::isspace(static_cast<unsigned char>(c))) word.clear();

        }
        return values;

    }   // NodeDescriptionValues()

    const AttributeValue* FindValue(const std::vector<AttributeValue>& values, const std::string& label, int position) {

        for (const AttributeValue& value : values) if (value.label == label) return &value;
        for (const AttributeValue& value : values) if (value.label.empty() && value.position == position) return &value;
        return nullptr;

    }   // FindValue()

}   // NAMESPACE CRUCIBLE::INSPECTSCRIPT
