#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

namespace Crucible {
    enum class ScriptFieldType { Boolean, Int32, Single, Double, String };
    // Scene-safe field override. Identity includes the declaring type so hidden
    // inherited fields cannot overwrite one another. No Mono pointers/GC handles.
    struct ScriptFieldValue {
        std::string declaringType, name;
        ScriptFieldType type = ScriptFieldType::Boolean;
        bool boolean = false;
        int32_t integer = 0;
        float single = 0;
        double real = 0;
        std::string text;
        bool stringIsNull = false;
        bool operator==(const ScriptFieldValue&) const = default;
    };
    inline bool SameScriptField(const ScriptFieldValue& a, const ScriptFieldValue& b) {
        return a.declaringType == b.declaringType && a.name == b.name && a.type == b.type;
    }
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
        bool entityOwned = false;
        uint32_t entityIndex = 0, entityGeneration = 0;
    };
    struct ScriptClassInfo {
        std::string nameSpace, name;
        bool isAbstract = false;
        bool hasPublicDefaultConstructor = false;
    };
    // monostate = void, nullptr = null String. Arguments require exact managed types.
    using ScriptValue = std::variant<std::monostate, bool, int32_t, float, double, std::string, std::nullptr_t>;
}
