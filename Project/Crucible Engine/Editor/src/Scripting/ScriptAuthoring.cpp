#include "Scripting/ScriptAuthoring.h"
#include <fstream>
#include <iterator>
#include <algorithm>
#include <vector>

namespace Crucible::ScriptAuthoring {
    bool IsIdentifier(const std::string& value) {
        const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
        return !value.empty() && letter(value.front()) && std::all_of(value.begin(), value.end(), [&](char c) {
            return letter(c) || (c >= '0' && c <= '9');
        });
    }
    bool Create(const std::filesystem::path& projectRoot, const std::string& name,
                const std::string& nameSpace, std::filesystem::path& output, std::string& error) {
        output.clear(); error.clear();
        if (!IsIdentifier(name)) { error = "Use a class name containing letters, digits and underscores, starting with a letter or underscore."; return false; }
        if (name == "OnCreate" || name == "OnUpdate" || name == "OnDestroy") {
            error = "Choose a class name different from its OnCreate, OnUpdate and OnDestroy callback methods."; return false;
        }
        std::string escapedNamespace;
        for (size_t begin = 0;;) {
            const auto end = nameSpace.find('.', begin);
            const auto part = nameSpace.substr(begin, end == std::string::npos ? end : end - begin);
            if (!IsIdentifier(part)) { error = "Enter a namespace such as Crucible.Scripts."; return false; }
            if (!escapedNamespace.empty()) escapedNamespace += '.';
            escapedNamespace += '@' + part;
            if (end == std::string::npos) break;
            begin = end + 1;
        }
        for (const std::string reserved : {"Crucible.EntityScript", "Crucible.EntityIdentity", "Crucible.Log", "Crucible.Animator"}) {
            if (nameSpace + "." + name == reserved || nameSpace == reserved || nameSpace.starts_with(reserved + ".")) {
                error = "That name is reserved by ScriptCore. Use a namespace such as Crucible.Scripts."; return false;
            }
        }
        try {
            if (!std::filesystem::is_regular_file(projectRoot / "ManagedScripts/CMakeLists.txt")) {
                error = "Project source folder not found. Script creation requires a Crucible source checkout."; return false;
            }
            std::ifstream source(projectRoot / "ManagedScripts/Templates/EntityScript.cs.txt", std::ios::binary);
            if (!source) { error = "Entity script template is missing."; return false; }
            std::string text{std::istreambuf_iterator<char>(source), {}};
            for (const auto& [token, value] : std::vector<std::pair<std::string, std::string>>{
                {"{{NAMESPACE}}", escapedNamespace}, {"{{CLASS}}", '@' + name}, {"{{NAME}}", name}}) {
                for (auto at = text.find(token); at != std::string::npos; at = text.find(token, at + value.size()))
                    text.replace(at, token.size(), value);
            }
            const auto directory = projectRoot / "Assets/Scripts"; // compiled by ManagedScripts/CMakeLists.txt
            std::filesystem::create_directories(directory);
            const auto path = directory / (name + ".cs");
            // C++23 exclusive creation: a duplicate/racing request never overwrites source.
            std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::noreplace);
            if (!file) { error = "Cannot create script: the file already exists or the folder is not writable."; return false; }
            file << text;
            file.close();
            if (!file) { error = "Writing the script failed. Check the source file before retrying."; return false; }
            output = path;
            return true;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
}
