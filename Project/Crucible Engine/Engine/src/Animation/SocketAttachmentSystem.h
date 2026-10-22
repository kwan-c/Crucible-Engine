#pragma once
#include "ECS/Systems/IECSSystems.h"
#include "ECS/Systems/Query.h"
#include "ECS/Components/Components.h"
#include <optional>

namespace Crucible {
    class ResourceManager;
    class SocketAttachmentSystem final : public IECSSystem {
    public:
        explicit SocketAttachmentSystem(ResourceManager& resources) : m_resources(resources) {}
        SystemStage Stage() const override { return SystemStage::PreRender; }
        void Describe(SystemDesc& desc) const override;
        void Update(World& world, SystemContext& ctx) override;
    private:
        ResourceManager& m_resources;
        std::optional<Query<SocketAttachment>> m_query;
    };
}
