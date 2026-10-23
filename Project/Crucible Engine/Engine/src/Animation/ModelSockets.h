#pragma once
#include "ECS/Components/Components.h"

namespace Crucible {
    class World;
    class ResourceManager;
    enum class SocketResult { Ready, Disabled, Unbound, MissingParent, ModelMismatch, MissingModel,
                              InvalidNode, OutsidePart, InvalidPose };
    ENGINE_API const char* ToString(SocketResult result);
    // Pure read: the preparation system populates the shared CPU caches first.
    ENGINE_API SocketResult ModelSocketFrame(const World& world, Entity owner,
        const SocketAttachment& socket, glm::mat4& frame);
    // Fresh local hierarchy + current evaluated pose; false for an unresolved binding.
    ENGINE_API bool ModelSocketWorld(const World& world, Entity attached, glm::mat4& matrix);
    // Structural operations: call outside ECS queries, like World::SetParent.
    ENGINE_API bool AttachModelSocket(World& world, ResourceManager& resources, Entity child,
        Entity owner, uint32_t node, const Transform& alignment, const std::string& name = {});
    ENGINE_API bool DetachModelSocket(World& world, Entity child, bool keepWorld = true);
    // A unique direct child alias, or null (missing or ambiguous).
    ENGINE_API Entity FindModelSocket(const World& world, Entity owner, const std::string& name);
}
