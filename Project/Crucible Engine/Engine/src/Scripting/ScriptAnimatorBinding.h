#pragma once
#include <cstdint>
#include <string>

namespace Crucible {
    enum class ScriptAnimatorOperation { Bool, Float, Integer, Trigger, ResetTrigger };
    struct ScriptAnimatorCommand {
        std::string name;
        ScriptAnimatorOperation operation = ScriptAnimatorOperation::Bool;
        int32_t integer = 0;
        float number = 0;
    };
    // SDK-free, non-owning adapter. The animation system validates owner lifetime
    // and queues parameter writes for its next pose evaluation.
    class IScriptAnimatorBinding {
    public:
        virtual ~IScriptAnimatorBinding() = default;
        virtual bool SetParameter(uint32_t index, uint32_t generation, const ScriptAnimatorCommand& command) = 0;
    };
}
