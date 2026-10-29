#pragma once
#include <optional>
#include "EngineExport.h"
#include "Animation/ModelPose.h"
#include "ECS/Components/Components.h"
#include "ECS/Systems/IECSSystems.h"
#include "ECS/Systems/Query.h"
#include "Systems/PlayState.h"
#include "Scripting/ScriptAnimatorBinding.h"
#include <map>

namespace Crucible {
    class ResourceManager;
    class PlayMode;
    class AnimatedModelSystem final : public IECSSystem, public IScriptAnimatorBinding {
    public:
        AnimatedModelSystem(ResourceManager& resources, PlayMode& playMode, World* world = nullptr)
            : m_resources(resources), m_playMode(playMode), m_world(world) {}
        SystemStage Stage() const override { return SystemStage::PreRender; }
        void Describe(SystemDesc& desc) const override;
        void Update(World& world, SystemContext& ctx) override;
        void Shutdown() noexcept override { m_pending.clear(); m_world = nullptr; }
        bool SetParameter(uint32_t index, uint32_t generation, const ScriptAnimatorCommand& command) override;
    private:
        using ModelQuery = Query<const MeshRenderer, AnimatedModel>;
        ResourceManager& m_resources;
        PlayMode& m_playMode;
        uint64_t m_sessionRevision = 0;
        std::optional<ModelQuery> m_query;
        World* m_world = nullptr;
        struct PendingParameters {
            uint64_t model = 0, controller = 0, session = 0;
            std::shared_ptr<const AnimatorController> definition;
            std::vector<ScriptAnimatorCommand> commands;
        };
        std::map<uint64_t, PendingParameters> m_pending;
    };
}
