#pragma once
#include "EngineExport.h"
#include "Scripting/ScriptingConfig.h"
#include "Scripting/ScriptTypes.h"
#include <memory>
#include <string>
#include <vector>

namespace Crucible {
    class IScriptAnimatorBinding;
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
        // S3 contract: public instance void OnCreate(), OnUpdate(float), OnDestroy().
        // The host owns the object; callback methods are validated before construction.
        bool CreateInstance(ScriptAssemblyHandle assembly, const std::string& nameSpace,
            const std::string& name, ScriptInstanceHandle& instance, bool enabled = true);
        bool SetInstanceEnabled(ScriptInstanceHandle instance, bool enabled);
        bool GetInstanceInfo(ScriptInstanceHandle instance, ScriptInstanceInfo& info);
        bool ListInstances(std::vector<ScriptInstanceInfo>& instances);
        bool UpdateInstances(float deltaTime);
        bool DestroyInstance(ScriptInstanceHandle& instance);
        bool DestroyAllInstances();
        // Opt-in config batch; a failed creation rolls back only this batch.
        bool CreateConfiguredInstances(const ScriptingConfig& config);
        // Entity adapters own scheduling; UpdateInstances ticks standalone ones only.
        bool DiscoverEntityClasses(ScriptAssemblyHandle assembly, std::vector<ScriptClassInfo>& classes);
        bool CreateEntityInstance(ScriptAssemblyHandle assembly, const std::string& nameSpace,
            const std::string& name, uint32_t index, uint32_t generation, ScriptInstanceHandle& instance);
        bool UpdateEntityInstance(ScriptInstanceHandle instance, float deltaTime);
        // Public mutable instance fields: bool/int/float/double/string, including
        // inherited fields. Default inspection constructs once per class, without callbacks.
        bool GetEntityFieldDefaults(ScriptAssemblyHandle assembly, const std::string& nameSpace,
            const std::string& name, std::vector<ScriptFieldValue>& fields);
        bool CreateEntityInstance(ScriptAssemblyHandle assembly, const std::string& nameSpace,
            const std::string& name, uint32_t index, uint32_t generation, ScriptInstanceHandle& instance,
            const std::vector<ScriptFieldValue>& fields);
        bool ReadEntityFields(ScriptInstanceHandle instance, std::vector<ScriptFieldValue>& fields);
        bool SetEntityFields(ScriptInstanceHandle instance, const std::vector<ScriptFieldValue>& fields);
        // Non-owning; initialization thread only. Clear before destroying the adapter.
        bool SetAnimatorBinding(IScriptAnimatorBinding* binding);
    private:
        bool CreateInstanceImpl(ScriptAssemblyHandle assembly, const std::string& nameSpace,
            const std::string& name, ScriptInstanceHandle& instance, bool enabled,
            const uint32_t* entityIdentity, const std::vector<ScriptFieldValue>* fields = nullptr);
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
