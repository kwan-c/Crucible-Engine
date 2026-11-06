#include "Animation/SocketAttachmentSystem.h"
#include "Animation/AnimatedModelSystem.h"
#include "Resources/ResourceManager.h"
#include "ECS/World.h"

namespace Crucible {
    void SocketAttachmentSystem::Describe(SystemDesc& desc) const {
        desc.Uses<Query<SocketAttachment>>()
            .RunAfterIfPresent<AnimatedModelSystem>()
            .Affinity(SystemAffinity::MainThread);
    }
    void SocketAttachmentSystem::Update(World& world, SystemContext&) {
        if (!m_query) m_query.emplace(world.CreateQuery<SocketAttachment>());
        m_query->ForEach([this](Entity, SocketAttachment& socket) {
            // Asked every frame (a map lookup) so a hot-reloaded model is picked up too.
            auto model = m_resources.GetModel(socket.modelAsset);
            if (socket.cachedAsset == socket.modelAsset && socket.model == model) return;
            socket.cachedAsset = socket.modelAsset;
            socket.model = std::move(model);
            socket.restPose = m_resources.GetModelRestPose(socket.modelAsset);
        });
    }
}
