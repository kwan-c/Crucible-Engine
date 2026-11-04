#pragma once
#include <filesystem>
#include <string>

namespace Crucible::ScriptAuthoring {
    bool IsIdentifier(const std::string& value);
    // Creates Assets/Scripts/<name>.cs from the EntityScript template (Inspector > Add Component).
    bool Create(const std::filesystem::path& projectRoot, const std::string& name,
        const std::string& nameSpace, std::filesystem::path& output, std::string& error);
    // Same, but in `directory`, which must be Assets/Scripts or a folder inside it - the only
    // place ManagedScripts/CMakeLists.txt compiles scripts from (Asset Browser > Create C# Script).
    bool CreateIn(const std::filesystem::path& projectRoot, const std::filesystem::path& directory,
        const std::string& name, const std::string& nameSpace, std::filesystem::path& output, std::string& error);
    // True if `directory` is projectRoot/Assets/Scripts or inside it.
    bool IsInScriptsFolder(const std::filesystem::path& projectRoot, const std::filesystem::path& directory);
}
