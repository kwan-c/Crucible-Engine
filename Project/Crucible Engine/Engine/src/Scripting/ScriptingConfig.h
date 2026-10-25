#pragma once
#include <string>
#include <vector>

namespace Crucible {
    struct ScriptInstanceConfig {
        std::string assembly, nameSpace, className;
        bool enabled = true;
    };
    struct ScriptingConfig {
        // CMake controls availability; this flag controls startup in enabled builds.
        bool enabled = true;
        // Relative paths resolve against the executable, never the working directory.
        std::string runtimeDirectory = "Mono";
        bool verifyManagedExecution = true;
        // Empty = no user code at startup. Nonempty entries are invoked once with no arguments.
        std::string startupAssembly, startupNamespace, startupClass, startupMethod;
        bool startupStatic = false;
        // Optional standalone Core-lifetime instances. No ECS or gameplay bindings.
        std::vector<ScriptInstanceConfig> instances;
    };
}
