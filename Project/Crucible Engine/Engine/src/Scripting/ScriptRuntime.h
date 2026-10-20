#pragma once
#include "EngineExport.h"
#include "Scripting/ScriptingConfig.h"
#include "Scripting/ScriptTypes.h"
#include <memory>
#include <string>
#include <vector>

namespace Crucible {
    enum class ScriptRuntimeState { NotStarted, Unavailable, Disabled, Running, Stopped, Failed };

    // One process-lifetime, main-thread Mono host. No ECS, assets, gameplay bindings
    // or Mono SDK types in this interface. Final cleanup cannot be followed by restart.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251) // private Pimpl; allocation and destruction stay inside Engine.dll
#endif
    class ENGINE_API ScriptRuntime final {
    public:
        ScriptRuntime();
        ~ScriptRuntime();
        ScriptRuntime(const ScriptRuntime&) = delete;
        ScriptRuntime& operator=(const ScriptRuntime&) = delete;
        bool Initialize(const ScriptingConfig& config);
        void Shutdown() noexcept;
        static bool IsBuiltWithMono() noexcept;
        ScriptRuntimeState State() const noexcept;
        bool IsRunning() const noexcept;
        bool ManagedExecutionVerified() const noexcept;
        const std::string& Version() const noexcept;
        const std::string& LastError() const noexcept;
        // Main/initialization thread only. Paths are UTF-8 and executable-relative.
        // Assemblies remain loaded until final shutdown; there is no reload/unload API.
        bool LoadAssembly(const std::string& path, ScriptAssemblyHandle& assembly);
        bool DiscoverClasses(ScriptAssemblyHandle assembly, std::vector<ScriptClassInfo>& classes);
        bool CreateObject(ScriptAssemblyHandle assembly, const std::string& nameSpace,
            const std::string& name, ScriptObjectHandle& object);
        bool DestroyObject(ScriptObjectHandle& object);
        bool Invoke(ScriptObjectHandle object, const std::string& method,
            const std::vector<ScriptValue>& arguments, ScriptValue& result);
        bool InvokeStatic(ScriptAssemblyHandle assembly, const std::string& nameSpace,
            const std::string& name, const std::string& method,
            const std::vector<ScriptValue>& arguments, ScriptValue& result);
        // Optional one-shot configured entry; not a frame callback or gameplay lifecycle.
        bool RunStartupEntry(const ScriptingConfig& config);
    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
