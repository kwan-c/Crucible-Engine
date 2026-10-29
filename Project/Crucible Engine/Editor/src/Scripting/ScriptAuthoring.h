#pragma once
#include <filesystem>
#include <string>

namespace Crucible::ScriptAuthoring {
    bool IsIdentifier(const std::string& value);
    bool Create(const std::filesystem::path& projectRoot, const std::string& name,
        const std::string& nameSpace, std::filesystem::path& output, std::string& error);
}
