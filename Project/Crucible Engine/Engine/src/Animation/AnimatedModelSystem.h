#pragma once
#include <optional>
#include "EngineExport.h"
#include "Animation/ModelPose.h"
#include "ECS/Components/Components.h"
#include "ECS/Systems/IECSSystems.h"
#include "ECS/Systems/Query.h"
#include "Systems/PlayState.h"

namespace Crucible {
    class ResourceManager;
    class PlayMode;
    class AnimatedModelSystem final : public IECSSystem {
    public:
        AnimatedModelSystem(ResourceManager& resources, PlayMode& playMode)
            : m_resources(resources), m_playMode(playMode) {}
        SystemStage Stage() const override { return SystemStage::PreRender; }
        void Describe(SystemDesc& desc) const override;
        void Update(World& world, SystemContext& ctx) override;
    private:
        using ModelQuery = Query<const MeshRenderer, AnimatedModel>;
        ResourceManager& m_resources;
        PlayMode& m_playMode;
        uint64_t m_sessionRevision = 0;
        std::optional<ModelQuery> m_query;
    };
}
