#include "Scripting/ScriptAuthoring.h"
#include <fstream>
#include <iterator>
#include <algorithm>
#include <cctype>
#include <system_error>
#include <vector>

namespace Crucible::ScriptAuthoring {
    namespace {
        std::string ToLower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }
    }
    bool IsIdentifier(const std::string& value) {
        const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
        return !value.empty() && letter(value.front()) && std::all_of(value.begin(), value.end(), [&](char c) {
            return letter(c) || (c >= '0' && c <= '9');
        });
    }
    bool IsInScriptsFolder(const std::filesystem::path& projectRoot, const std::filesystem::path& directory) {
        std::error_code ec;
        const auto scripts = std::filesystem::weakly_canonical(projectRoot / "Assets/Scripts", ec);
        if (ec) return false;
        const auto folder = std::filesystem::weakly_canonical(directory, ec);
        if (ec) return false;
        const auto relative = folder.lexically_relative(scripts);   // "." for Assets/Scripts itself
        return !relative.empty() && *relative.begin() != "..";
    }
    bool Create(const std::filesystem::path& projectRoot, const std::string& name,
                const std::string& nameSpace, std::filesystem::path& output, std::string& error) {
        return CreateIn(projectRoot, projectRoot / "Assets/Scripts", name, nameSpace, output, error);
    }
    bool CreateIn(const std::filesystem::path& projectRoot, const std::filesystem::path& directory,
                  const std::string& name, const std::string& nameSpace, std::filesystem::path& output, std::string& error) {
        output.clear(); error.clear();
        if (!IsInScriptsFolder(projectRoot, directory)) {
            error = "Scripts must be inside Assets/Scripts (or a folder in it) to be compiled."; return false;
        }
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
        for (const std::string reserved : {"Crucible.EntityScript", "Crucible.EntityIdentity", "Crucible.Entity", "Crucible.Log", "Crucible.Animator",
                                           "Crucible.Transform", "Crucible.RigidBody", "Crucible.Input", "Crucible.KeyCode", "Crucible.Prefab",
                                           "Crucible.AudioSource", "Crucible.AudioClip", "Crucible.Audio", "Crucible.AudioBus",
                                           "Crucible.Physics", "Crucible.RaycastHit", "Crucible.CharacterController", "Crucible.Time", "Crucible.Scene",
                                           "Crucible.Camera", "Crucible.CameraOrbit", "Crucible.Ray", "Crucible.Light", "Crucible.LightType", "Crucible.Color"}) {
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
            // All of Assets/Scripts compiles into one assembly, so the same class name in two
            // subfolders would be a build error. Refuse a file name that already exists anywhere.
            const auto scripts = projectRoot / "Assets/Scripts";
            std::error_code walk;
            if (std::filesystem::is_directory(scripts, walk)) {
                const auto wanted = ToLower(name + ".cs");
                for (auto it = std::filesystem::recursive_directory_iterator(scripts, walk);
                     !walk && it != std::filesystem::recursive_directory_iterator(); it.increment(walk)) {
                    if (it->is_regular_file(walk) && ToLower(it->path().filename().string()) == wanted) {
                        error = "A script named " + name + ".cs already exists in " +
                                std::filesystem::relative(it->path(), projectRoot, walk).generic_string() + ".";
                        return false;
                    }
                }
            }
            std::filesystem::create_directories(directory); // compiled by ManagedScripts/CMakeLists.txt
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
