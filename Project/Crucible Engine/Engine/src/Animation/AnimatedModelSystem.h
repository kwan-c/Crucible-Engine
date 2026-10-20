#pragma once
#include <optional>
#include <unordered_map>
#include "EngineExport.h"
#include "Animation/ModelPose.h"
#include "Resources/RenderedModel.h"
#include "Graphics/RenderTypes.h"
#include "Spatial/Bounds.h"
#include "ECS/Components/Components.h"
#include "ECS/Systems/IECSSystems.h"
#include "ECS/Systems/Query.h"
#include "Systems/PlayState.h"

namespace Crucible {
    class Renderer;
    class ResourceManager;
    class PlayMode;
    class AnimatedModelSystem final : public IECSSystem {
    public:
        AnimatedModelSystem(Renderer& renderer, ResourceManager& resources, PlayMode& playMode)
            : m_renderer(renderer), m_resources(resources), m_playMode(playMode) {}
        SystemStage Stage() const override { return SystemStage::PreRender; }
        void Describe(SystemDesc& desc) const override;
        void Update(World& world, SystemContext& ctx) override;
    private:
        using ModelQuery = Query<const WorldTransform, AnimatedModel>;
        struct Part {
            uint32_t node = 0, primitive = 0;
            RenderProxyHandle proxy;
            std::vector<AABB> jointBounds;
            AABB unweightedBounds;
        };
        struct Instance {
            uint32_t generation = 0;
            uint32_t clip = InvalidModelIndex;
            uint64_t asset = 0, seen = 0;
            std::shared_ptr<const RenderedModel> resource;
            ModelPose pose;
            std::vector<Part> parts;
        };
        void Release(Instance& instance);
        void CreateParts(Instance& instance, Entity entity);
        Renderer& m_renderer;
        ResourceManager& m_resources;
        PlayMode& m_playMode;
        PlayState m_previousState = PlayState::Edit;
        uint64_t m_frame = 0;
        std::optional<ModelQuery> m_query;
        std::unordered_map<uint32_t, Instance> m_instances;
    };
}
