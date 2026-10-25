#include "Animation/ModelSockets.h"
#include "ECS/World.h"
#include "Resources/ResourceManager.h"
#include <cmath>

namespace Crucible {
    const char* ToString(SocketResult result) {
        switch (result) {
        case SocketResult::Ready: return "Ready";
        case SocketResult::Disabled: return "Disabled: ordinary scene parenting";
        case SocketResult::Unbound: return "Choose a parent model node";
        case SocketResult::MissingParent: return "Needs a live scene parent with a model";
        case SocketResult::ModelMismatch: return "Parent model changed; rebind the node";
        case SocketResult::MissingModel: return "Waiting for the compiled model";
        case SocketResult::InvalidNode: return "Node index is outside the model";
        case SocketResult::OutsidePart: return "Node is outside the parent's selected model part";
        default: return "Pose is unavailable or its root transform is singular";
        }
    }

    SocketResult ModelSocketFrame(const World& world, Entity owner,
                                 const SocketAttachment& socket, glm::mat4& frame) {
        frame = glm::mat4(1);
        if (!socket.enabled) return SocketResult::Disabled;
        if (!socket.modelAsset || socket.node == InvalidModelIndex) return SocketResult::Unbound;
        const auto* renderer = world.Read<MeshRenderer>(owner);
        if (!renderer || !renderer->modelAsset) return SocketResult::MissingParent;
        if (renderer->modelAsset != socket.modelAsset) return SocketResult::ModelMismatch;
        if (socket.cachedAsset != socket.modelAsset || !socket.model) return SocketResult::MissingModel;
        if (socket.node >= socket.model->nodes.size()) return SocketResult::InvalidNode;
        if (!IsModelNodeInSubtree(*socket.model, socket.node, renderer->modelRoot)) return SocketResult::OutsidePart;
        const ModelPose* pose = socket.restPose.get();
        // A switched-off AnimatedModel isn't evaluated: follow the rest pose, like the renderer.
        const auto* animation = world.IsComponentActive<AnimatedModel>(owner) ? world.Read<AnimatedModel>(owner) : nullptr;
        if (animation) {
            if (!animation->poseValid || animation->evaluatedAsset != socket.modelAsset) return SocketResult::InvalidPose;
            pose = &animation->pose;
        }
        return pose && ModelNodeTransform(*socket.model, *pose, renderer->modelRoot, socket.node, frame)
            ? SocketResult::Ready : SocketResult::InvalidPose;
    }

    bool ModelSocketWorld(const World& world, Entity child, glm::mat4& matrix) {
        return world.TryGetWorldMatrix(child, matrix);
    }

    bool AttachModelSocket(World& world, ResourceManager& resources, Entity child,
                           Entity owner, uint32_t node, const Transform& alignment, const std::string& name) {
        if (!world.IsAlive(child) || !world.IsAlive(owner)) return false;
        const auto offset = Detail::ComposeTRS(alignment.positions, alignment.rotation, alignment.scale);
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
            if (!std::isfinite(offset[c][r])) return false;
        const auto* renderer = world.Read<MeshRenderer>(owner);
        if (!renderer || !renderer->modelAsset) return false;
        SocketAttachment socket;
        socket.modelAsset = renderer->modelAsset;
        socket.cachedAsset = socket.modelAsset;
        socket.model = resources.GetModel(socket.modelAsset);
        socket.restPose = resources.GetModelRestPose(socket.modelAsset);
        socket.node = node; socket.socketName = name;
        if (!socket.model || !IsModelNodeInSubtree(*socket.model, node, renderer->modelRoot)) return false;
        // Validate links before adding a component. No structural changes occur on validation failure.
        if (child == owner || world.IsChildOf(owner, child)) return false;
        if (!world.HasComponent<SocketAttachment>(child)) world.AddComponent<SocketAttachment>(child);
        world.SetParent(child, owner, false);
        *world.Write<SocketAttachment>(child) = std::move(socket);
        *world.Write<Transform>(child) = alignment;
        return true;
    }

    bool DetachModelSocket(World& world, Entity child, bool keepWorld) {
        if (!world.IsAlive(child) || !world.HasComponent<SocketAttachment>(child)) return false;
        Transform transform = *world.Read<Transform>(child);
        if (keepWorld) {
            glm::mat4 matrix;
            if (!world.TryGetWorldMatrix(child, matrix) ||
                !Detail::DecomposeTRS(matrix, transform.positions, transform.rotation, transform.scale)) return false;
            // A sheared world transform cannot be represented by the engine's local TRS.
            const auto reconstructed = Detail::ComposeTRS(transform.positions, transform.rotation, transform.scale);
            for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
                if (std::abs(matrix[c][r] - reconstructed[c][r]) > 1e-4f * (1 + std::abs(matrix[c][r]))) return false;
        }
        world.SetParent(child, kNullEntity, false);
        world.RemoveComponent<SocketAttachment>(child);
        *world.Write<Transform>(child) = transform;
        return true;
    }

    Entity FindModelSocket(const World& world, Entity owner, const std::string& name) {
        Entity found{};
        if (name.empty()) return found;
        for (uint32_t i = 0; i < world.ChildCount(owner); ++i) {
            const auto child = world.GetChild(owner, i);
            const auto* socket = world.Read<SocketAttachment>(child);
            if (!socket || socket->socketName != name) continue;
            if (found.IsValid()) return kNullEntity;
            found = child;
        }
        return found;
    }
}
