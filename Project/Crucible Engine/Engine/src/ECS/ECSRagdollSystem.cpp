/*************************************************************************
* \file:         ECSRagdollSystem.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          20 November 2026
* \version       0.9
* \brief         See ECSRagdollSystem.h.
*
**************************************************************************/
// STL
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

// Crucible engine
#include "ECS/ECSRagdollSystem.h"
#include "Animation/AnimatedModelSystem.h"
#include "Animation/ModelPose.h"
#include "Animation/SocketAttachmentSystem.h"
#include "Diagnostics/Logger.h"
#include "ECS/World.h"
#include "Resources/ModelResource.h"
#include "Resources/ResourceManager.h"
#include "Systems/PlayMode.h"

namespace Crucible {

    namespace {

        // A node's local transform as a matrix, composed exactly as BuildModelPose does
        // (ModelPose.cpp): a node stored as a matrix keeps it, any other is T * R * S.
        Math::mat4 LocalMatrix(const ModelLocalTransform& local) {
            return local.hasMatrix ? local.matrix
                                   : Detail::ComposeTRS(local.translation, local.rotation, local.scale);
        }

        float Volume(const RagdollPiece& piece) { return piece.size.x * piece.size.y * piece.size.z; }

        // How many nodes above this one: the core is the piece nearest the root.
        int Depth(const ModelResource& model, std::uint32_t node) {
            int depth = 0;
            for (std::uint32_t at = model.nodes[node].parent; at < model.nodes.size(); at = model.nodes[at].parent)
                if (++depth > 256) break;   // a broken file with a parent loop must not hang
            return depth;
        }

    }   // ANONYMOUS NAMESPACE

    void ECSRagdollSystem::Describe(SystemDesc& desc) const {
        // Exclusive: it creates the pieces directly so the joints can name them the same frame
        // (see the header). After the animation builds the pose, before sockets read it (a weapon
        // on a socket must follow the ragdoll's bone, not the animation's). Not SimulationOnly:
        // while Paused, AnimatedModelSystem still rebuilds the pose every frame, so the copy-back
        // (step 4) has to run then too, or a paused corpse would snap back to its animation.
        desc.Exclusive()
            .RunAfterIfPresent<AnimatedModelSystem>()
            .RunBeforeIfPresent<SocketAttachmentSystem>()
            .Affinity(SystemAffinity::MainThread);
    }   // ECSRagdollSystem::Describe()

    void ECSRagdollSystem::Update(World& world, [[maybe_unused]] SystemContext& ctx) {
        // A new play session (the Editor's Play after Stop): the old pieces went with the old scene.
        if (m_sessionRevision != m_playMode.SessionRevision()) {
            m_sessionRevision = m_playMode.SessionRevision();
            m_ragdolls.clear();
            m_requests.clear();
        }
        // Ragdolls only exist in Play. Stopping restores the scene, which takes the pieces with
        // it, so the list goes too: else Ragdoll.Count would report corpses that are gone.
        if (m_playMode.State() == PlayState::Edit) { m_requests.clear(); m_ragdolls.clear(); return; }

        // Moved out first: Spawn may log and fail, and nothing should be handled twice.
        std::vector<Request> requests;
        requests.swap(m_requests);
        // No per-frame limit (step 7): 24 kills in one frame cost about +10 ms here and +19 ms
        // in the next frame's physics (Debug build), and the deaths themselves cost more. If
        // playtests stutter, build only the first few here and leave the rest in m_requests.
        for (const Request& request : requests) Spawn(world, request);

        // Corpses that have rested long enough, or lived too long, go first: no point posing them.
        UpdateLifetimes(world, world.DeltaTime(), m_playMode.State() == PlayState::Playing);

        // Every frame, including the one they spawned on and while Paused: the animation has just
        // rebuilt each pose, so put the ragdolled bones back where their bodies are.
        ApplyPoses(world);
    }   // ECSRagdollSystem::Update()

    void ECSRagdollSystem::ApplyPoses(World& world) {
        for (std::size_t i = 0; i < m_ragdolls.size();) {
            if (world.IsAlive(m_ragdolls[i].model)) { ApplyPose(world, m_ragdolls[i]); ++i; continue; }
            // Its model is gone (the enemy was destroyed some other way): the pieces go with it,
            // or invisible boxes would lie around the level for the rest of the session.
            Remove(world, i);
        }
    }   // ECSRagdollSystem::ApplyPoses()

    /*! \brief  Unity's CombatRagdoll.TickLifetime. A ragdoll is removed (with its owner) once
                every part has been at rest for kRestSeconds, or kMaxLifetime after it spawned.
                As in Unity, the rest clock runs only while Playing, but the lifetime counts real
                time, paused or not.
                "At rest" is every part under kRestSpeed and kRestAngularSpeed. It is judged over
                kRestSampleSeconds, not per frame: the angular speed comes from how far each part
                turned since the last sample, and over one frame at a few hundred fps that turn
                is smaller than float rounding in the rotation, so a still body could read as
                spinning and never be removed.
    */
    void ECSRagdollSystem::UpdateLifetimes(World& world, float deltaTime, bool playing) {
        for (std::size_t i = 0; i < m_ragdolls.size();) {
            RagdollInstance& ragdoll = m_ragdolls[i];
            ragdoll.life += deltaTime;
            if (playing) {
                ragdoll.sinceSample += deltaTime;
                if (ragdoll.sinceSample >= kRestSampleSeconds) {
                    bool resting = true;
                    for (RagdollInstance::Part& part : ragdoll.parts) {
                        const Transform* transform = world.Read<Transform>(part.body);
                        const RigidBody* body = world.Read<RigidBody>(part.body);
                        if (!transform || !body) continue;   // a part gone: judge the rest
                        const float dot = std::min(1.0f, std::abs(glm::dot(part.sampledRotation, transform->rotation)));
                        const float turned = 2.0f * std::acos(dot);   // radians since the last sample
                        if (glm::length(body->velocity) > kRestSpeed || turned / ragdoll.sinceSample > kRestAngularSpeed)
                            resting = false;
                        part.sampledRotation = transform->rotation;
                    }
                    ragdoll.restAge = resting ? ragdoll.restAge + ragdoll.sinceSample : 0.0f;
                    ragdoll.sinceSample = 0.0f;
                }
            }
            if (ragdoll.restAge >= kRestSeconds || ragdoll.life >= kMaxLifetime) Remove(world, i);
            else ++i;
        }
    }   // ECSRagdollSystem::UpdateLifetimes()

    /*! \brief  Takes a ragdoll out of the world: its pieces, then its owner (the enemy, whose
                children - the model, the head, the weapon - go with it), then its entry.
    */
    void ECSRagdollSystem::Remove(World& world, std::size_t index) {
        const RagdollInstance& ragdoll = m_ragdolls[index];
        for (const RagdollInstance::Part& part : ragdoll.parts)
            if (world.IsAlive(part.body)) world.DestroyEntity(part.body);
        if (ragdoll.owner != kNullEntity && world.IsAlive(ragdoll.owner)) world.DestroyEntity(ragdoll.owner);
        m_ragdolls.erase(m_ragdolls.begin() + static_cast<std::ptrdiff_t>(index));
    }   // ECSRagdollSystem::Remove()

    /*! \brief  Puts every ragdolled bone where its body is and rebuilds the pose from there.
                Walks the nodes parent first. A ragdolled bone gets the model-space matrix that
                lands it on its body (the body's world position and rotation, with the bone's own
                scale put back on) and the local transform that gives that under its parent as
                posed this frame. Every other node keeps its local transform, so the meshes hung
                on a bone, its sockets (the head, the weapon) and anything between bones follow.
                BuildModelPose then recomputes the globals and skin palettes from the locals, as
                AnimatedModelSystem did a moment ago.
        \return False if this ragdoll could not be posed this frame (its model or pose is gone).
    */
    bool ECSRagdollSystem::ApplyPose(World& world, const RagdollInstance& ragdoll) {
        if (!world.IsAlive(ragdoll.model)) return false;
        const MeshRenderer* renderer  = world.Read<MeshRenderer>(ragdoll.model);
        AnimatedModel*      animation = world.GetComponent<AnimatedModel>(ragdoll.model);
        if (!renderer || !animation || renderer->modelAsset != ragdoll.modelAsset) return false;
        const auto model = m_resources.GetModel(ragdoll.modelAsset);
        if (!model) return false;
        const std::size_t nodeCount = model->nodes.size();

        ModelPose& pose = animation->pose;
        if (pose.local.size() != nodeCount || pose.global.size() != nodeCount) {
            // No animated pose this frame: start from the rest pose.
            const auto rest = m_resources.GetModelRestPose(ragdoll.modelAsset);
            if (!rest || rest->local.size() != nodeCount || rest->global.size() != nodeCount) return false;
            pose = *rest;
        }

        // World -> model space. The renderer draws node n at entity * inverse(global[root]) *
        // global[n] (root = the subtree a MeshRenderer may draw; none = identity), so going back
        // is global[root] * inverse(entity).
        Math::mat4 entityWorld(1.0f);
        if (!world.TryGetWorldMatrix(ragdoll.model, entityWorld)) return false;
        if (std::abs(glm::determinant(entityWorld)) < 1e-12f) return false;   // a zero scale: nothing to draw anyway
        const Math::mat4 rootGlobal = renderer->modelRoot < nodeCount ? pose.global[renderer->modelRoot] : Math::mat4(1.0f);
        const Math::mat4 worldToModel = rootGlobal * glm::inverse(entityWorld);

        m_partOfNode.assign(nodeCount, -1);
        for (std::size_t i = 0; i < ragdoll.parts.size(); ++i)
            if (ragdoll.parts[i].bone < nodeCount) m_partOfNode[ragdoll.parts[i].bone] = static_cast<int>(i);
        m_globals.resize(nodeCount);

        for (std::uint32_t index : model->nodeOrder) {
            const ModelNode& node = model->nodes[index];
            const Math::mat4 parentGlobal = node.parent < nodeCount ? m_globals[node.parent] : Math::mat4(1.0f);
            const int part = m_partOfNode[index];
            const Transform* body = part >= 0 ? world.Read<Transform>(ragdoll.parts[static_cast<std::size_t>(part)].body) : nullptr;
            if (!body) {   // not ragdolled (or its body is gone): keeps its own local transform
                m_globals[index] = parentGlobal * LocalMatrix(pose.local[index]);
                continue;
            }
            const Math::vec3& boneScale = ragdoll.parts[static_cast<std::size_t>(part)].boneScale;
            const Math::mat4 desired = worldToModel * Detail::ComposeTRS(body->positions, body->rotation, boneScale);
            m_globals[index] = desired;

            // The local transform that puts it there under its parent as posed this frame. TRS
            // when it splits cleanly (the usual case); else the exact matrix.
            const Math::mat4 local = glm::inverse(parentGlobal) * desired;
            ModelLocalTransform& out = pose.local[index];
            if (Detail::DecomposeTRS(local, out.translation, out.rotation, out.scale)) out.hasMatrix = false;
            else { out.matrix = local; out.hasMatrix = true; }
        }

        animation->poseValid = BuildModelPose(*model, pose);
        return animation->poseValid;
    }   // ECSRagdollSystem::ApplyPose()

    bool ECSRagdollSystem::RequestRagdoll(const World& world, Entity model, const Math::vec3& velocity, Entity owner) {
        if (model == kNullEntity || !world.IsAlive(model)) return false;
        for (const Request& request : m_requests) if (request.model == model) return false;
        for (const RagdollInstance& ragdoll : m_ragdolls) if (ragdoll.model == model) return false;

        // Can it be built? Asked now because the build runs later this frame: a "no" from there
        // would come after the caller had already frozen its enemy for a ragdoll, so the golem
        // would stand still until removed instead of toppling. Spawn checks again (cheap), in
        // case something changed in between. The rig is measured here on first use (cached).
        const MeshRenderer* renderer = world.Read<MeshRenderer>(model);
        if (!renderer || renderer->modelAsset == 0 || !world.Read<AnimatedModel>(model)) {
            CRU_WARN("[Ragdoll] entity {} has no MeshRenderer with a model and AnimatedModel - no ragdoll", model.index);
            return false;
        }
        const auto resource = m_resources.GetModel(renderer->modelAsset);
        if (!resource) { CRU_WARN("[Ragdoll] model {:016X} is not loaded - no ragdoll", renderer->modelAsset); return false; }
        if (RigFor(renderer->modelAsset, resource).pieces.empty()) {
            CRU_WARN("[Ragdoll] model {:016X} has no rigid pieces - no ragdoll", renderer->modelAsset);
            return false;
        }

        m_requests.push_back(Request{model, velocity, owner == kNullEntity ? model : owner});
        return true;
    }   // ECSRagdollSystem::RequestRagdoll()

    std::size_t ECSRagdollSystem::RagdollCount() const noexcept { return m_ragdolls.size(); }

    /*! \brief  Builds one ragdoll, Unity's CombatRagdoll.Build: a box body per piece placed on
                its bone where the animation has it this frame, then a Character joint from every
                piece but the core to its nearest ancestor piece (else the core).
                Every body is a root entity at the bone's world position and rotation with scale
                1 (Unity: "uniform world-space proxies avoid imported bone scales"); the bone's
                scale goes into the box instead. So step 4 can copy a body straight back onto
                its bone.
    */
    void ECSRagdollSystem::Spawn(World& world, const Request& request) {
        const Entity entity = request.model;
        if (!world.IsAlive(entity)) return;   // destroyed since it was asked for: nothing to do
        const MeshRenderer*  renderer  = world.GetComponent<MeshRenderer>(entity);
        const AnimatedModel* animation = world.GetComponent<AnimatedModel>(entity);
        if (!renderer || !animation || renderer->modelAsset == 0) {
            CRU_WARN("[Ragdoll] entity {} has no MeshRenderer with a model and AnimatedModel - no ragdoll", entity.index);
            return;
        }
        const auto model = m_resources.GetModel(renderer->modelAsset);
        if (!model) { CRU_WARN("[Ragdoll] model {:016X} is not loaded - no ragdoll", renderer->modelAsset); return; }
        const RagdollRig& rig = RigFor(renderer->modelAsset, model);
        if (rig.pieces.empty()) { CRU_WARN("[Ragdoll] model {:016X} has no rigid pieces - no ragdoll", renderer->modelAsset); return; }

        // Where the bones are now: the entity's world matrix times this frame's animated pose
        // (model space), or the model's rest pose if the animation has none.
        Math::mat4 entityWorld(1.0f);
        if (!world.TryGetWorldMatrix(entity, entityWorld)) return;
        std::shared_ptr<const ModelPose> restPose;
        const ModelPose* pose = &animation->pose;
        if (!animation->poseValid || pose->global.size() != model->nodes.size()) {
            restPose = m_resources.GetModelRestPose(renderer->modelAsset);
            pose = restPose.get();
        }
        if (!pose || pose->global.size() != model->nodes.size()) {
            CRU_WARN("[Ragdoll] model {:016X} has no usable pose - no ragdoll", renderer->modelAsset);
            return;
        }

        const EntityName* ownerName = world.Read<EntityName>(entity);   // World guards EntityName: read-only access
        const std::string prefix = (ownerName ? ownerName->name : std::string("Entity")) + " Ragdoll ";

        RagdollInstance ragdoll;
        ragdoll.model = entity;
        ragdoll.owner = request.owner;
        ragdoll.modelAsset = renderer->modelAsset;
        std::unordered_map<std::uint32_t, std::size_t> partOfBone;   // bone node -> index in ragdoll.parts

        // --- the bodies ---------------------------------------------------- //
        for (std::size_t i = 0; i < rig.pieces.size(); ++i) {
            const RagdollPiece& piece = rig.pieces[i];
            // The bone in the entity's frame exactly as the renderer will draw it (a MeshRenderer
            // may draw a subtree of the model: ModelNodeTransform takes its root out), then world.
            Math::mat4 inEntity(1.0f);
            if (!ModelNodeTransform(*model, *pose, renderer->modelRoot, piece.bone, inEntity)) continue;
            Math::vec3 position(0.0f), scale(1.0f);
            Math::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
            if (!Detail::DecomposeTRS(entityWorld * inEntity, position, rotation, scale)) {
                // A mirrored (negative scale) or flattened bone has no plain rotation for a body,
                // so it gets none and stays stiff on its parent. Our models have none: say so.
                CRU_WARN("[Ragdoll] {}: bone {} is mirrored or has no size - no body for it",
                         ownerName ? ownerName->name : std::string("entity"), model->nodes[piece.bone].name);
                continue;
            }

            const Entity body = world.CreateNamedEntity(prefix + model->nodes[piece.bone].name);
            if (Transform* transform = world.GetComponent<Transform>(body)) {
                transform->positions = position;
                transform->rotation  = rotation;
                transform->scale     = Math::vec3(1.0f);
            }

            Collider box;
            box.m_type     = ShapeType::Box;
            box.m_size     = glm::max(piece.size * scale * kBoxScale, Math::vec3(kMinBox));
            box.m_center   = piece.center * scale;
            box.m_layer    = PhysicsLayer::Corpse;   // the level only: not the mech, enemies or other corpses
            box.m_friction = kFriction;
            world.AddComponent(body, box);

            RigidBody rigidbody;
            rigidbody.mass        = i == 0 ? kCoreMass : kPartMass;
            rigidbody.drag        = kDrag;
            rigidbody.angularDrag = kAngularDrag;
            rigidbody.velocity    = request.velocity;   // the body is created moving (ECSPhysicsSystem reads it)
            world.AddComponent(body, rigidbody);

            partOfBone[piece.bone] = ragdoll.parts.size();
            ragdoll.parts.push_back(RagdollInstance::Part{body, kNullEntity, piece.bone, scale, rotation});
        }
        if (ragdoll.parts.empty()) return;

        // --- the joints: each part holds on to the nearest ancestor that is a part, else the core
        for (std::size_t i = 1; i < ragdoll.parts.size(); ++i) {
            RagdollInstance::Part& part = ragdoll.parts[i];
            std::size_t parentIndex = 0;
            for (std::uint32_t at = model->nodes[part.bone].parent; at < model->nodes.size(); at = model->nodes[at].parent) {
                const auto found = partOfBone.find(at);
                if (found != partOfBone.end()) { parentIndex = found->second; break; }
            }
            part.parent = ragdoll.parts[parentIndex].body;

            // Unity's ConfigurableJoint on its default axis: X limited +-35, Y and Z +-30, the joint
            // point at this part's own origin (its bone's pivot). Limits count from this pose.
            PhysicsJoint joint;
            joint.m_type              = JointType::Character;
            joint.m_connectedEntity   = part.parent;
            joint.m_anchor            = Math::vec3(0.0f);
            joint.m_axis              = Math::vec3(1.0f, 0.0f, 0.0f);
            joint.m_swingAxis         = Math::vec3(0.0f, 1.0f, 0.0f);
            joint.m_lowTwistLimit     = -kTwistLimit;
            joint.m_highTwistLimit    =  kTwistLimit;
            joint.m_swing1Limit       =  kSwingLimit;
            joint.m_swing2Limit       =  kSwingLimit;
            joint.m_enableCollision   = false;
            joint.m_solverIterations  = kJointIterations;
            world.AddComponent(part.body, joint);
        }

        // Debug, not Info: one line per death is developer detail (about 0.1 ms each in a Debug
        // build, measured in step 7), and CRU_DEBUG is compiled out of the shipping build.
        CRU_DEBUG("[Ragdoll] {}: {} pieces spawned, core {}, starting at ({:.2f}, {:.2f}, {:.2f}) m/s",
                  ownerName ? ownerName->name : std::string("entity"), ragdoll.parts.size(),
                  model->nodes[ragdoll.parts[0].bone].name, request.velocity.x, request.velocity.y, request.velocity.z);

        // The cap (Unity has 12; ours is kMaxRagdolls): make room by removing the oldest corpse,
        // one already at rest if there is one (usually behind the fight), else the oldest of all.
        // Done after building, so a ragdoll that fails to build never costs an old one.
        if (m_ragdolls.size() >= kMaxRagdolls) {
            std::size_t victim = 0;
            for (std::size_t i = 0; i < m_ragdolls.size(); ++i)
                if (m_ragdolls[i].restAge > 0.0f) { victim = i; break; }   // oldest first: the first at rest
            Remove(world, victim);
        }
        m_ragdolls.push_back(std::move(ragdoll));
    }   // ECSRagdollSystem::Spawn()

    /*! \brief  The cached rig for this model, measured now if it is new or its file was
                reloaded (the cached copy no longer points at the model the manager hands out).
    */
    const RagdollRig& ECSRagdollSystem::RigFor(std::uint64_t modelAsset, const std::shared_ptr<const ModelResource>& model) {
        CachedRig& cached = m_rigs[modelAsset];
        if (cached.source.lock() != model) {
            std::size_t skippedSkinned = 0;
            cached.rig = BuildRig(*model, skippedSkinned);
            cached.source = model;
            LogRig(modelAsset, *model, cached.rig, skippedSkinned);
        }
        return cached.rig;
    }   // ECSRagdollSystem::RigFor()

    /*! \brief  Boxes every rigid mesh in the frame of the bone that moves it: the mesh node's
                parent (or the mesh node itself if it is a root). Each vertex is taken from the
                mesh's own space into the bone's through the mesh node's local transform, so the
                box is exact in that frame; Unity boxed the world bounds' corners, which is looser
                for a turned piece. Meshes sharing a bone (a knee sphere and a thigh) make one box,
                like Unity's bounds per anchor. Then largest first, at most kMaxPieces, and the
                piece nearest the root moved to the front as the core.
                Skinned primitives are skipped and counted: their vertices sit in bind space,
                spread over several bones, and none of our models use them yet.
    */
    RagdollRig ECSRagdollSystem::BuildRig(const ModelResource& model, std::size_t& skippedSkinned) {
        // Empty until a vertex is added: min starts above and max below any real point.
        struct Bounds {
            Math::vec3 min{ std::numeric_limits<float>::infinity()};
            Math::vec3 max{-std::numeric_limits<float>::infinity()};
        };

        RagdollRig rig;
        std::vector<Bounds> bounds;                                 // parallel to rig.pieces
        std::unordered_map<std::uint32_t, std::size_t> pieceOfBone;  // bone node -> its piece

        for (std::uint32_t index = 0; index < model.nodes.size(); ++index) {
            const ModelNode& node = model.nodes[index];
            if (node.mesh >= model.meshes.size()) continue;   // not a mesh node

            const bool hasParent = node.parent < model.nodes.size();
            const std::uint32_t bone = hasParent ? node.parent : index;
            // Mesh space -> bone space. A root mesh is its own bone: no change.
            const Math::mat4 toBone = hasParent ? LocalMatrix(node.local) : Math::mat4(1.0f);

            const auto [slot, isNew] = pieceOfBone.try_emplace(bone, rig.pieces.size());
            if (isNew) { rig.pieces.push_back(RagdollPiece{bone}); bounds.emplace_back(); }
            Bounds& box = bounds[slot->second];

            for (const ModelPrimitive& primitive : model.meshes[node.mesh].primitives) {
                if (primitive.hasSkinning) { ++skippedSkinned; continue; }
                for (const ModelVertex& vertex : primitive.vertices) {
                    const Math::vec3 point = Math::vec3(toBone * Math::vec4(vertex.position, 1.0f));
                    box.min = glm::min(box.min, point);
                    box.max = glm::max(box.max, point);
                }
            }
        }

        // Min/max into centre and size. A bone whose meshes gave no vertices has no box: dropped.
        std::vector<RagdollPiece> measured;
        for (std::size_t i = 0; i < rig.pieces.size(); ++i) {
            if (!(bounds[i].min.x <= bounds[i].max.x)) continue;   // still +inf/-inf: nothing measured
            RagdollPiece piece = rig.pieces[i];
            piece.center = (bounds[i].min + bounds[i].max) * 0.5f;
            piece.size   = bounds[i].max - bounds[i].min;
            measured.push_back(piece);
        }

        // Largest first: over the limit the smallest go (Unity's sort).
        std::stable_sort(measured.begin(), measured.end(),
                         [](const RagdollPiece& a, const RagdollPiece& b) { return Volume(a) > Volume(b); });
        if (measured.size() > kMaxPieces) measured.resize(kMaxPieces);

        // The core: the piece nearest the root, the larger if several are as near (the list is
        // largest first, so the first such piece). Unity looks for the torso first; "largest"
        // alone would pick a shin on the mech's leg model.
        if (!measured.empty()) {
            std::size_t core = 0;
            for (std::size_t i = 1; i < measured.size(); ++i)
                if (Depth(model, measured[i].bone) < Depth(model, measured[core].bone)) core = i;
            std::rotate(measured.begin(), measured.begin() + static_cast<std::ptrdiff_t>(core),
                        measured.begin() + static_cast<std::ptrdiff_t>(core) + 1);
        }
        rig.pieces = std::move(measured);
        return rig;
    }   // ECSRagdollSystem::BuildRig()

    void ECSRagdollSystem::LogRig(std::uint64_t modelAsset, const ModelResource& model, const RagdollRig& rig,
                                  std::size_t skippedSkinned) {
        CRU_INFO("[Ragdoll] rig for model {:016X}: {} pieces{}{}", modelAsset, rig.pieces.size(),
                 rig.pieces.empty() ? std::string() : ", core " + model.nodes[rig.pieces[0].bone].name,
                 skippedSkinned ? ", " + std::to_string(skippedSkinned) + " skinned primitives skipped" : std::string());
        for (const RagdollPiece& piece : rig.pieces)
            CRU_INFO("[Ragdoll]   {:<20} box {:.2f} x {:.2f} x {:.2f}  centre ({:.2f}, {:.2f}, {:.2f})",
                     model.nodes[piece.bone].name, piece.size.x, piece.size.y, piece.size.z,
                     piece.center.x, piece.center.y, piece.center.z);
    }   // ECSRagdollSystem::LogRig()

}   // NAMESPACE CRUCIBLE
