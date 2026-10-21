#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

namespace Crucible {
    // Host-local handles, never serialized. Copying does not create another object.
    struct ScriptAssemblyHandle { uint64_t value = 0; explicit operator bool() const { return value != 0; } };
    struct ScriptObjectHandle { uint64_t value = 0; explicit operator bool() const { return value != 0; } };
    struct ScriptInstanceHandle { uint64_t value = 0; explicit operator bool() const { return value != 0; } };
    enum class ScriptInstanceState { Running, Disabled, Faulted };
    struct ScriptInstanceInfo {
        ScriptInstanceHandle handle;
        ScriptAssemblyHandle assembly;
        std::string nameSpace, name;
        ScriptInstanceState state = ScriptInstanceState::Disabled;
        uint64_t updateCount = 0;
        double elapsedSeconds = 0;
        std::string lastError;
    };
    struct ScriptClassInfo {
        std::string nameSpace, name;
        bool isAbstract = false;
        bool hasPublicDefaultConstructor = false;
    };
    // monostate = void, nullptr = null String. Arguments require exact managed types.
    using ScriptValue = std::variant<std::monostate, bool, int32_t, float, double, std::string, std::nullptr_t>;
}
