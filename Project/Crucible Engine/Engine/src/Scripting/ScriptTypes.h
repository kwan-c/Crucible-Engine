#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

namespace Crucible {
    // Host-local handles, never serialized. Copying does not create another object.
    struct ScriptAssemblyHandle { uint64_t value = 0; explicit operator bool() const { return value != 0; } };
    struct ScriptObjectHandle { uint64_t value = 0; explicit operator bool() const { return value != 0; } };
    struct ScriptClassInfo {
        std::string nameSpace, name;
        bool isAbstract = false;
        bool hasPublicDefaultConstructor = false;
    };
    // monostate = void, nullptr = null String. Arguments require exact managed types.
    using ScriptValue = std::variant<std::monostate, bool, int32_t, float, double, std::string, std::nullptr_t>;
}
