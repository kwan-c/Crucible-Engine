#pragma once
#include "EngineExport.h"
#include "ECS/Systems/IECSSystems.h"
#include "ECS/Components/Components.h"
#include "Scripting/ScriptTypes.h"
#include <map>
#include <set>
#include <memory>

namespace Crucible {
    class ScriptRuntime;
    class PlayMode;
    class SceneManager;
    struct EntityScriptStatus {
        ScriptInstanceHandle instance;
        std::string error;
    };
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251 4275) // private STL; IECSSystem is a shared header-only native interface
#endif
    class ENGINE_API EntityScriptSystem final : public IECSSystem {
    public:
        EntityScriptSystem(ScriptRuntime& runtime, World& world, PlayMode& play, SceneManager& scenes);
        void Init() override;
        void Describe(SystemDesc& desc) const override;
        void Update(World& world, SystemContext& ctx) override;
        void Shutdown() noexcept override;
        void StopAll();
        EntityScriptStatus Status(Entity entity) const;
    private:
        struct Record { ScriptComponent definition; ScriptInstanceHandle instance; std::string error; };
        static uint64_t Key(Entity entity) { return (uint64_t(entity.generation) << 32) | entity.index; }
        void Release(Record& record);
        ScriptRuntime& m_runtime;
        World& m_world;
        PlayMode& m_play;
        SceneManager& m_scenes;
        std::map<uint64_t, Record> m_records;
        std::set<uint64_t> m_removed;
        std::shared_ptr<int> m_lifetime = std::make_shared<int>(0);
        uint64_t m_session = 0;
        bool m_sceneChanged = false;
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
