/*************************************************************************
* \file:         ECSRagdollSystem.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          20 November 2026
* \version       0.8
* \brief
*                Ragdolls built at death from an animated model's bones.
*                Port of Gam3333's CombatRagdoll (Unity).
*
* \details
*                Bones are nodes inside a model (ModelResource), not entities,
*                so physics cannot see them. When an enemy dies this system
*                spawns one physics entity per rigid piece, joins them with
*                PhysicsJoints, and (step 4) every frame copies those bodies
*                back into the model's pose, after AnimatedModelSystem built it.
*
*                A piece is every rigid mesh hung on one bone, boxed in that
*                bone's frame. A model's pieces (its "rig") are measured from
*                the mesh vertices once and cached, so a death costs no
*                measuring.
*
*                The pieces are plain entities: a box Collider on the Corpse
*                layer (it only touches the level, never the mech, enemies or
*                other corpses), a RigidBody, and on every piece but the core a
*                Character PhysicsJoint to its nearest ancestor piece. The
*                numbers are Unity's CombatRagdoll's (see the k constants).
*
*                The system is Exclusive: declared systems must queue new
*                entities, which only get their ids at the end of the stage,
*                too late to wire the joints. An exclusive one creates them
*                directly, outside its query loop.
*
*                Built in steps, smallest first:
*                  1. log each model's bone tree (done, verified)
*                  2. measure each piece's box from its vertices (done)
*                  3. spawn the pieces and joints on request (done, tested)
*                  4. copy the bodies into the pose every frame (done, tested)
*                  5. the C# trigger from Enemy.OnDied (done, tested)
*                  6. lifetime and the 20-ragdoll cap: a ragdoll and its owner
*                     go after resting kRestSeconds or after kMaxLifetime, or
*                     the oldest when a new one would pass kMaxRagdolls (one at
*                     rest first) (done, tested)
*                  7. (this version) frame cost measured, Debug build, 24
*                     kills in one frame: +10 ms that frame, +19 ms the next
*                     (physics creating the bodies), +8 ms a frame while 20
*                     move. No per-frame build limit for now.
*
* \history
*    -v0.1       step 1: logs each model's bone tree once per play session
*    -v0.2       step 2: RagdollRig / RagdollPiece, measured from vertices
*                and cached per model; replaces the tree dump
*    -v0.3       step 3: RequestRagdoll spawns the pieces and joints;
*                Exclusive; the core is the piece nearest the model's root
*    -v0.4       step 4: ApplyPoses puts every ragdolled bone where its body
*                is, each frame; bones placed through ModelNodeTransform
*                (respects a MeshRenderer that draws a model subtree)
*    -v0.5       step 5: Active() for the C# binding; a ragdoll whose model
*                entity is destroyed takes its pieces with it
*    -v0.6       step 6: owner, rest / lifetime removal, the 20-ragdoll cap
*    -v0.7       step 7: the per-death spawn line is CRU_DEBUG
*    -v0.8       review fixes: RequestRagdoll takes the World and checks the
*                model at once (a "no" now reaches the caller, so Enemy topples);
*                a mirrored bone is warned about; the list clears on Stop
*
**************************************************************************/
#pragma once

// STL
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

// Crucible engine
#include "ECS/Components/Components.h"
#include "ECS/Systems/IECSSystems.h"
#include "EngineExport.h"
#include "Math/Math.h"

namespace Crucible {

    class ResourceManager;
    class PlayMode;
    struct ModelResource;

    // One physics body of a ragdoll: every rigid mesh hung on one bone, in one box.
    struct RagdollPiece {
        std::uint32_t bone = 0;          // the model node the body moves (the meshes' parent)
        Math::vec3    center{0.0f};      // box centre, in the bone's frame (model units)
        Math::vec3    size{0.0f};        // full box size, in the bone's frame (model units)
    };  // RAGDOLLPIECE STRUCT

    // What one model's ragdoll is made of. Measured once per model, shared by every corpse.
    struct RagdollRig {
        // pieces[0] is the core: the piece nearest the model's root (Unity looks for the torso),
        // the larger one if several are equally near. The rest are largest first.
        std::vector<RagdollPiece> pieces;
    };  // RAGDOLLRIG STRUCT

    // One ragdoll in the world: the model it moves and the physics entities that move it.
    struct RagdollInstance {
        struct Part {
            Entity        body;                   // the spawned physics entity
            Entity        parent;                 // the part its joint holds on to (kNullEntity for the core)
            std::uint32_t bone = 0;               // the model node it moves
            Math::vec3    boneScale{1.0f};        // the bone's world scale at spawn: the body has none,
                                                  // so it is put back on when the bone is posed
            Math::vec3    anchorInParent{0.0f};   // this body's origin in the parent body's frame at
                                                  // spawn: where the joint keeps it (tests measure gaps)
            Math::quat    sampledRotation{1.0f, 0.0f, 0.0f, 0.0f};   // at the last rest sample
        };
        Entity            model;                  // the entity with the MeshRenderer and AnimatedModel
        Entity            owner;                  // destroyed with the ragdoll (the enemy's root; else the model)
        std::uint64_t     modelAsset = 0;
        std::vector<Part> parts;                  // parts[0] is the core
        float             life = 0.0f;            // seconds since spawn, real time (paused too, as Unity)
        float             restAge = 0.0f;         // seconds every part has been at rest, Play time only
        float             sinceSample = 0.0f;     // Play time since the last rest sample
    };  // RAGDOLLINSTANCE STRUCT

    class ECSRagdollSystem final : public IECSSystem {

    public:

        // --- Unity's CombatRagdoll numbers --------------------------------- //
        static constexpr std::size_t kMaxPieces   = 14;      // bodyLimit: over it, the smallest pieces go
        static constexpr float kBoxScale          = 0.85f;   // boxes a little inside the mesh, so pieces overlap less
        static constexpr float kMinBox            = 0.06f;   // metres: no box side thinner than this
        static constexpr float kCoreMass          = 8.0f;    // kg
        static constexpr float kPartMass          = 2.0f;    // kg
        static constexpr float kDrag              = 0.35f;   // linearDamping
        static constexpr float kAngularDrag       = 1.5f;    // angularDamping
        static constexpr float kFriction          = 0.6f;    // Unity's default material (our Collider default is 0.2)
        static constexpr float kTwistLimit        = 35.0f;   // degrees either way around the piece's X
        static constexpr float kSwingLimit        = 30.0f;   // degrees around Y, and around Z
        static constexpr std::uint32_t kJointIterations = 8; // solverIterations
        // --- Lifetime (Unity's CombatDeathRagdoll; the cap is Hugo's: Unity has 12) --- //
        static constexpr std::size_t kMaxRagdolls = 20;      // over it, the oldest goes (one at rest first)
        static constexpr float kRestSeconds       = 5.0f;    // removed after every part rested this long...
        static constexpr float kMaxLifetime       = 30.0f;   // ...or after this long at most
        static constexpr float kRestSpeed         = 0.15f;   // m/s: slower counts as at rest
        static constexpr float kRestAngularSpeed  = 0.3f;    // rad/s
        static constexpr float kRestSampleSeconds = 0.1f;    // rest is judged over this interval (see UpdateLifetimes)

        // The ResourceManager hands out the models (their bone trees and vertices). PlayMode
        // says whether a session is running. Core owns both and destroys systems first.
        ECSRagdollSystem(ResourceManager& resources, PlayMode& playMode)
            : m_resources(resources), m_playMode(playMode) { s_active = this; }
        // Only clears Active() if it is still this one, so C# never reaches a freed system.
        ~ECSRagdollSystem() override { if (s_active == this) s_active = nullptr; }

        // The one running ragdoll system, for the C# binding (Ragdoll.Spawn), which has no other
        // way to reach it - the same pattern as ECSNavigationSystem::Active(). Null before
        // Core registers it and after it is destroyed.
        static ECSRagdollSystem* Active() { return s_active; }

        // PreRender, right after AnimatedModelSystem: the pose it builds each frame is what a
        // ragdoll will overwrite, before sockets, the hierarchy and rendering read it.
        SystemStage Stage() const override { return SystemStage::PreRender; }
        void Describe(SystemDesc& desc) const override;
        void Update(World& world, SystemContext& ctx) override;

        /*! \brief  Ragdolls this entity's model (it needs a MeshRenderer and an AnimatedModel)
                    at the next PreRender, every piece starting at velocity (m/s, world space).
                    owner is destroyed together with the ragdoll when it is removed (after
                    resting kRestSeconds, after kMaxLifetime, or replaced at the cap); kNullEntity
                    means the model itself.
                    The model is checked now, not when it is built, so the caller still has time
                    to do something else (Enemy topples instead) - hence the World.
            \return False if the entity is gone, was already asked for, or has no MeshRenderer
                    with a loaded model, no AnimatedModel, or no rigid pieces (logged).
        */
        ENGINE_API bool RequestRagdoll(const World& world, Entity model, const Math::vec3& velocity,
                                       Entity owner = kNullEntity);

        // The ragdolls alive now, oldest first (tests and debugging).
        ENGINE_API std::size_t RagdollCount() const noexcept;
        ENGINE_API const RagdollInstance* GetRagdoll(std::size_t index) const noexcept;

    private:

        // A model's rig, measured on first use and again only if the model file is reloaded.
        struct CachedRig {
            std::weak_ptr<const ModelResource> source;   // expired or different: the file was reloaded
            RagdollRig rig;
        };

        struct Request {
            Entity     model;
            Math::vec3 velocity{0.0f};
            Entity     owner;
        };

        const RagdollRig& RigFor(std::uint64_t modelAsset, const std::shared_ptr<const ModelResource>& model);
        static RagdollRig BuildRig(const ModelResource& model, std::size_t& skippedSkinned);
        static void LogRig(std::uint64_t modelAsset, const ModelResource& model, const RagdollRig& rig, std::size_t skippedSkinned);
        void Spawn(World& world, const Request& request);
        void ApplyPoses(World& world);
        bool ApplyPose(World& world, const RagdollInstance& ragdoll);
        void UpdateLifetimes(World& world, float deltaTime, bool playing);
        void Remove(World& world, std::size_t index);

        ResourceManager& m_resources;
        PlayMode&        m_playMode;
        std::uint64_t    m_sessionRevision = 0;
        std::unordered_map<std::uint64_t, CachedRig> m_rigs;   // by model asset id
        std::vector<Request>         m_requests;               // handled at the next Update
        std::vector<RagdollInstance> m_ragdolls;               // alive, oldest first
        // ApplyPose scratch, reused every frame so posing allocates nothing once warmed up.
        std::vector<Math::mat4> m_globals;                     // per node: model-space matrix after posing
        std::vector<int>        m_partOfNode;                  // per node: index into parts, or -1

        static inline ECSRagdollSystem* s_active = nullptr;    // see Active()

    };  // ECSRAGDOLLSYSTEM CLASS

}   // NAMESPACE CRUCIBLE
