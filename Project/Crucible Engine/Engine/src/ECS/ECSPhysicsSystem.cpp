/*************************************************************************
* \file:         ECSPhysicsSystem.cpp
* \author:       Abhishek Akilandeswari Patchamuthu (v1.0 skeleton),
*                Hugo Low Ren Hao (v2.0 physics binding)
* \par:          Email: a.patchamuthu@digipen.edu,
*                Email: low.h@digipen.edu
* \date          27 September 2026
* \version       2.6
* \brief
*                Implementation of ECSPhysicsSystem. See the header for
*                what happens each frame.
*
* \details
*                The system keeps one Record per entity that has physics:
*                the body it made, the description it made it from, and
*                the Transform and velocity it last wrote. Each frame the
*                components are compared against that record:
*                  - a setting changed     -> rebuild the body
*                  - the Transform changed -> gameplay moved it: teleport
*                    (a kinematic body is moved there over the frame's
*                    steps instead, so it has a velocity)
*                  - the velocity changed  -> gameplay set it: pass it on
*                An entity that no longer has physics components, or no
*                longer exists, is not found by the query, so its record
*                and its body are removed.
*
*                Joints get the same treatment in a JointRecord, synced
*                AFTER the bodies so both ends exist: a changed setting,
*                or either body rebuilt, rebuilds the joint.
*
* \history
*    -v1.0       stub implementation, no physics logic yet
*    -v2.0       the real binding
*    -v2.1       BeginSession/EndSession: Jolt and every per-session field
*                start fresh each Play; the object and its queries stay
*    -v2.2       hierarchy-aware: a child's Transform is relative to its
*                parent, so bodies and characters are placed at the world
*                pose (parent frame * Transform) and their results are
*                written back in the parent's frame. Roots are unchanged.
*                (Abhishek)
*    -v2.3       a switched-off RigidBody is a static body; it stops being
*                dynamic even on frames where the pose can't be worked out
*                (StopIfNotDynamic), and its data isn't written back
*                (Abhishek)
*    -v2.4       forces: RigidBody's AddForce/AddTorque/ApplyImpulse and
*                the ConstantForce component, handed to every fixed step
*    -v2.5       PhysicsJoint: joints built, rebuilt and removed with the
*                component; broken joints flagged in m_isBroken. Both
*                respect switched-off components and world scale
*    -v2.6       RigidBody::isKinematic: the Transform drives the body
*                through MoveKinematic (DriveKinematic), spread over the
*                frame's steps; a static collider moved frame after frame
*                is reported (kWarnMovingStatic). Characters ride the
*                ground's velocity and now move after the step, not before
*
**************************************************************************/
#include "ECSPhysicsSystem.h"

// STL
#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>

// Crucible engine
#include "Components/Components.h"
#include "Diagnostics/Logger.h"
#include "Physics/PhysicsComponents.h"
#include "Scene/SceneManager.h"
#include "Systems/Query.h"
#include "World.h"

namespace {

    using namespace Crucible;

    constexpr float         kFixedDeltaTime   = 1.0f / 60.0f;   // one physics step (Unity: Time.fixedDeltaTime)
    constexpr std::uint32_t kMaxStepsPerFrame = 6;               // after a long hitch, stop trying to catch up

    // --- small helpers ---------------------------------------------------- //
    // Exact comparison on purpose: the question is "is this the same value
    // physics itself wrote", and a tolerance would swallow slow drift - a script
    // nudging something a millimetre a frame would never reach the body.

    bool Same(const Math::vec3& a, const Math::vec3& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }

    bool Same(const Math::quat& a, const Math::quat& b) {
        return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
    }

    bool Finite(const Math::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
    bool Finite(const Math::quat& q) { return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w); }

    // The collider as gameplay states it (full sizes, Unity style) turned into
    // what Jolt wants (half sizes). The ONE place that conversion happens.
    ShapeDesc ToShapeDesc(const Collider& collider) {

        ShapeDesc shape;
        shape.m_type        = collider.m_type;
        shape.m_halfExtents = Math::vec3(collider.m_size.x * 0.5f, collider.m_size.y * 0.5f, collider.m_size.z * 0.5f);
        shape.m_radius      = collider.m_radius;
        shape.m_center      = collider.m_center;

        // Unity's capsule height is the whole thing, caps included. Jolt wants
        // half of the straight middle, so both caps come off first.
        shape.m_halfHeight  = std::max(collider.m_height * 0.5f - collider.m_radius, 0.0f);

        return shape;

    }   // ToShapeDesc()

    // Would these two descriptions build the same body? Position, rotation and
    // velocity are deliberately not compared: those change without a rebuild.
    bool SameRecipe(const BodyDesc& a, const BodyDesc& b) {

        const ShapeDesc& x = a.m_shape;
        const ShapeDesc& y = b.m_shape;

        return x.m_type == y.m_type && Same(x.m_halfExtents, y.m_halfExtents) && x.m_radius == y.m_radius
            && x.m_halfHeight == y.m_halfHeight && Same(x.m_center, y.m_center)
            && a.m_motion == b.m_motion && a.m_layer == b.m_layer && a.m_mass == b.m_mass
            && a.m_friction == b.m_friction && a.m_restitution == b.m_restitution
            && a.m_gravityFactor == b.m_gravityFactor
            && a.m_linearDamping == b.m_linearDamping && a.m_angularDamping == b.m_angularDamping
            && a.m_lockedAxes == b.m_lockedAxes;

    }   // SameRecipe()

    bool SameRecipe(const CharacterDesc& a, const CharacterDesc& b) {
        return a.m_height == b.m_height && a.m_radius == b.m_radius && Same(a.m_center, b.m_center)
            && a.m_slopeLimit == b.m_slopeLimit && a.m_stepOffset == b.m_stepOffset
            && a.m_pushStrength == b.m_pushStrength && a.m_layer == b.m_layer;
    }   // SameRecipe()

    // Would these two components build the same joint? Every setting, and both
    // ways of naming the other entity. m_isBroken is left out: it is a result,
    // not a setting, and is handled before this is ever asked.
    bool SameRecipe(const PhysicsJoint& a, const PhysicsJoint& b) {
        return a.m_type == b.m_type && a.m_connectedBody == b.m_connectedBody && a.m_connectedEntity == b.m_connectedEntity
            && Same(a.m_anchor, b.m_anchor) && Same(a.m_axis, b.m_axis) && Same(a.m_swingAxis, b.m_swingAxis)
            && a.m_enableCollision == b.m_enableCollision
            && a.m_useLimits == b.m_useLimits && a.m_minAngle == b.m_minAngle && a.m_maxAngle == b.m_maxAngle
            && a.m_lowTwistLimit == b.m_lowTwistLimit && a.m_highTwistLimit == b.m_highTwistLimit
            && a.m_swing1Limit == b.m_swing1Limit && a.m_swing2Limit == b.m_swing2Limit
            && Same(a.m_connectedAnchor, b.m_connectedAnchor) && a.m_spring == b.m_spring && a.m_damper == b.m_damper
            && a.m_minDistance == b.m_minDistance && a.m_maxDistance == b.m_maxDistance
            && a.m_breakForce == b.m_breakForce && a.m_breakTorque == b.m_breakTorque
            && a.m_solverIterations == b.m_solverIterations && a.m_solverVelocityIterations == b.m_solverVelocityIterations;
    }   // SameRecipe()

    // A point given in an entity's local space - Unity's joint anchors are -
    // turned into world space from where physics has the body right now.
    // Scale counts, as it does for the collider: an anchor at the tip of a
    // capsule stays at the tip when the capsule is scaled up.
    Math::vec3 LocalToWorld(const Math::vec3& point, const Math::vec3& position, const Math::quat& rotation, const Math::vec3& scale) {
        return position + rotation * (point * scale);
    }

    // --- hierarchy: Transform is LOCAL, Jolt is WORLD --------------------- //
    // A child's Transform is relative to its parent's frame (World::TryGetParentFrame);
    // a root's frame is the identity. For a root both helpers pass the values through
    // untouched, bit for bit, so the exact Same() bookkeeping works as it always has.

    // Local pose -> world pose (and world scale, which sizes the collider).
    bool ToWorld(const Math::mat4& parentFrame, const Transform& local,
                 Math::vec3& position, Math::quat& rotation, Math::vec3& scale) {
        if (parentFrame == Math::mat4(1.0f)) {
            position = local.positions;
            rotation = local.rotation;
            scale    = local.scale;
            return true;
        }
        return Detail::DecomposeTRS(parentFrame * Detail::ComposeTRS(local.positions, local.rotation, local.scale),
                                    position, rotation, scale);
    }

    // World pose (what Jolt reports) -> the pose the Transform needs inside parentFrame.
    void ToLocal(const Math::mat4& parentFrame, Math::vec3& position, Math::quat& rotation) {
        if (parentFrame == Math::mat4(1.0f)) return;
        position = Math::vec3(glm::inverse(parentFrame) * Math::vec4(position, 1.0f));
        Math::vec3 parentPosition, parentScale;
        Math::quat parentRotation;
        if (Detail::DecomposeTRS(parentFrame, parentPosition, parentRotation, parentScale))
            rotation = glm::normalize(glm::inverse(parentRotation) * rotation);
    }

    // An entity's scale in the world: its own times its parents'. A joint's anchor is
    // placed with it, the way SyncBody sizes the collider with it, so an anchor at a
    // capsule's tip stays at the tip on a child whose parent is scaled too.
    Math::vec3 WorldScaleOf(const World& ecs, const Entity& entity) {
        const Transform* transform = ecs.Read<Transform>(entity);
        if (transform == nullptr) return Math::vec3(1.0f, 1.0f, 1.0f);

        Math::mat4 parentFrame(1.0f);
        Math::vec3 position, scale;
        Math::quat rotation;
        if (!ecs.TryGetParentFrame(entity, parentFrame) || !ToWorld(parentFrame, *transform, position, rotation, scale))
            return transform->scale;     // no usable parent frame this frame: the local scale is the best guess
        return scale;
    }

    // One bit per message. An entity can have both a Collider and a
    // CharacterController AND sit on the wrong layer, and hearing about only
    // the first one is how the second gets missed.
    enum WarnBit : std::uint32_t {
        kWarnNaN          = 1u << 0,
        kWarnLayer        = 1u << 1,
        kWarnTwoColliders = 1u << 2,
        kWarnLoneForce    = 1u << 3,
        kWarnJointNoBody  = 1u << 4,
        kWarnJointMissing = 1u << 5,
        kWarnJointSelf    = 1u << 6,
        kWarnJointNoOther = 1u << 7,
        kWarnJointStatic  = 1u << 8,
        kWarnMovingStatic = 1u << 9,
    };

    // How many frames in a row a static body may be moved before it is reported. Not 1: a
    // respawn or a one-off teleport is fine. Moved this long, something is animating it.
    constexpr std::uint32_t kMovingStaticFrames = 30;

    // Once per entity per message, ever. A warning that repeats every frame
    // buries the log.
    void WarnOnce(std::uint32_t& flags, const WarnBit& bit, const Entity& entity, const char* message) {
        if ((flags & bit) != 0u) return;
        flags |= bit;
        CRU_WARN("entity {}:{} {}", entity.index, entity.generation, message);
    }   // WarnOnce()

    // --- one entity's physics, as the binding remembers it ------------------ //

    struct Record {

        Entity        entity    {};
        BodyHandle    body      {};
        BodyDesc      recipe    {};      // what the body was built from
        bool          failed    = false; // building from that description failed: wait for it to change

        Math::vec3    position  = Math::vec3(0.0f, 0.0f, 0.0f);   // the Transform as physics last left it
        Math::quat    rotation  = Math::quat(1.0f, 0.0f, 0.0f, 0.0f);
        Math::vec3    velocity  = Math::vec3(0.0f, 0.0f, 0.0f);
        Math::mat4    parentFrame = Math::mat4(1.0f);   // what position/rotation are relative to (identity for a root)

        // This frame's pushes, collected once in Sync and handed to Jolt before
        // EVERY step this frame, because Jolt forgets a force after each step.
        // force/torque are world axes; the relative pair is the body's own axes.
        Math::vec3    force          = Math::vec3(0.0f, 0.0f, 0.0f);
        Math::vec3    torque         = Math::vec3(0.0f, 0.0f, 0.0f);
        Math::vec3    relativeForce  = Math::vec3(0.0f, 0.0f, 0.0f);
        Math::vec3    relativeTorque = Math::vec3(0.0f, 0.0f, 0.0f);
        Math::vec3    impulse        = Math::vec3(0.0f, 0.0f, 0.0f);   // instant: the first step only, then spent

        // A kinematic body's Transform at the end of last frame and of this frame, in world
        // space. Each step aims at the point between them that matches the step's moment in
        // time (DriveKinematic), so the body moves at the Transform's real speed whatever
        // the frame rate is.
        Math::vec3    kinematicFrom    = Math::vec3(0.0f, 0.0f, 0.0f);
        Math::quat    kinematicFromRot = Math::quat(1.0f, 0.0f, 0.0f, 0.0f);
        Math::vec3    kinematicTo      = Math::vec3(0.0f, 0.0f, 0.0f);
        Math::quat    kinematicToRot   = Math::quat(1.0f, 0.0f, 0.0f, 0.0f);

        std::uint32_t movedFrames = 0;   // frames in a row a STATIC body was teleported (kWarnMovingStatic)

        std::uint64_t seenFrame  = 0;    // the last frame the query found this entity
        std::uint64_t pulledStep = 0;    // the last step this body turned up in the moved list
        std::uint32_t warned     = 0;    // WarnBit flags already logged for this entity

    };  // RECORD STRUCT

    // The same notebook for a mech. Kept apart from Record because almost
    // nothing about the two paths is shared: different handle, different
    // description, and a character is walked by hand every step.
    struct CharacterRecord {

        Entity          entity    {};
        CharacterHandle character {};
        CharacterDesc   recipe    {};
        bool            failed    = false;

        Math::vec3      position  = Math::vec3(0.0f, 0.0f, 0.0f);   // where physics last put it (in parentFrame)
        Math::mat4      parentFrame = Math::mat4(1.0f);              // identity for a root
        std::uint64_t   seenFrame = 0;
        std::uint32_t   warned    = 0;

    };  // CHARACTERRECORD STRUCT

    // The notebook for one PhysicsJoint. A joint is rebuilt when its settings
    // change OR when either body is rebuilt (a body edited mid-Play gets a new
    // handle, and Physics already removed the joints that were on the old one).
    struct JointRecord {

        Entity        entity     {};
        JointHandle   joint      {};
        BodyHandle    bodyA      {};    // the bodies it was built between
        BodyHandle    bodyB      {};    // invalid = the world
        PhysicsJoint  recipe     {};    // the component it was built from
        bool          failed     = false;

        // What recipe.m_connectedBody (a scene id) resolved to. Looking a scene
        // id up searches the whole scene, so it is done once and remembered,
        // and redone only when the id changes or that entity dies.
        std::uint64_t resolvedId = 0;
        Entity        resolved   {};

        std::uint64_t seenFrame  = 0;
        std::uint32_t warned     = 0;

    };  // JOINTRECORD STRUCT

    bool SameBody(const BodyHandle& a, const BodyHandle& b) { return a.m_id == b.m_id; }

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    // ------------------------------------------------------------------- //
    // STATE
    // ------------------------------------------------------------------- //

    struct ECSPhysicsSystem::State {

        State(SceneManager* sceneManager, const PhysicsSettings& physicsSettings) : settings(physicsSettings), scenes(sceneManager) {}

        PhysicsSettings settings;
        Physics         physics;
        World*          world  = nullptr;
        SceneManager*   scenes = nullptr;   // turns a joint's saved connected body (a scene id) into an entity

        // Read-only on purpose: taking them non-const would mark every Transform
        // as changed every frame, and other systems use that flag to decide what
        // to re-upload. Physics writes back through GetComponent, so only the
        // bodies that actually moved are stamped.
        std::optional<Query<const Transform, const Collider>> colliders;

        std::optional<Query<const Transform, CharacterController>> characters;

        std::optional<Query<const PhysicsJoint>> joints;    // read-only: only a joint that breaks is written, through GetComponent

        std::unordered_map<std::uint64_t, Record>          records;           // keyed by EntityToUserData(entity)
        std::unordered_map<std::uint64_t, CharacterRecord> characterRecords;  // same key, the mechs
        std::unordered_map<std::uint64_t, JointRecord>     jointRecords;      // same key, the joints

        bool          staticBodyMade = false;                   // a new arena piece appeared: the static tree wants rebuilding
        float         accumulator    = 0.0f;                    // time owed to the simulation, under one step
        std::uint64_t frame          = 0;
        std::uint64_t step           = 0;
        std::uint32_t stepsLastFrame = 0;
        std::uint32_t stepsThisFrame = 0;                       // set before Sync, so Sync knows whether a kick can land now
        std::uint32_t stepOfFrame    = 0;                       // which of this frame's steps is running (DriveKinematic)
        float         frameTime      = 0.0f;                    // this frame's time, as the clock counted it
        float         owedBefore     = 0.0f;                    // the accumulator before this frame's time was added
        bool          inSession      = false;                   // between BeginSession and EndSession

        void BindWorld(World& ecs);
        void ResetClock();
        void Sync(World& ecs);
        void SyncBody(Record& record, const Transform& transform, const Math::mat4& parentFrame, const Collider& collider, const RigidBody* rigidbody);
        void StopIfNotDynamic(Record& record, const RigidBody* rigidbody);
        void SyncCharacter(CharacterRecord& record, const Transform& transform, const Math::mat4& parentFrame, CharacterController& controller);
        void TakeForces(World& ecs, Record& record, const RigidBody* rigidbody);
        void PushForces(Record& record);
        void DriveKinematic(Record& record);
        void SyncJoints(World& ecs);
        void SyncJoint(World& ecs, JointRecord& record, const PhysicsJoint& joint);
        void DropJoint(JointRecord& record);
        void PullBrokenJoints(World& ecs);
        void Step(World& ecs);
        void StepCharacter(World& ecs, CharacterRecord& record);
        void PullBodies(World& ecs);

        Record&          Touch(const Entity& entity);
        CharacterRecord& TouchCharacter(const Entity& entity);
        void             Remove(Record& record);
        void             RemoveAll();

    };  // ECSPHYSICSSYSTEM::STATE STRUCT

    // ------------------------------------------------------------------- //
    // LIFECYCLE
    // ------------------------------------------------------------------- //

    ECSPhysicsSystem::ECSPhysicsSystem(SceneManager* scenes, const PhysicsSettings& settings)
        : m_state{ std::make_unique<State>(scenes, settings) } {}

    ECSPhysicsSystem::~ECSPhysicsSystem() { Shutdown(); }

    // Nothing to start here: the Jolt world only exists during a play session
    // (BeginSession), so the editor pays for no physics threads while editing.
    void ECSPhysicsSystem::Init() {}

    void ECSPhysicsSystem::Shutdown() noexcept {

        if (!m_state) return;

        EndSession();

        // A Query holds pointers into the World's archetypes, and must not
        // outlive it. Dropping them here means a restart rebuilds them rather
        // than reusing ones that point at a world that is gone.
        m_state->colliders.reset();
        m_state->characters.reset();
        m_state->joints.reset();
        m_state->world = nullptr;

    }   // ECSPhysicsSystem::Shutdown()

    void ECSPhysicsSystem::Describe(SystemDesc& desc) const {
        desc.SimulationOnly();      // Edit and Pause do not step; access stays undeclared, as before
    }

    // Switched back on (itself, or by its requirements): no fall or speed carried over from
    // before - also when the off/on happened between two physics updates, so the character was
    // never removed. A jump asked for since is kept.
    void ECSPhysicsSystem::OnComponentActivated(World& world, Entity entity, ComponentId component) {
        if (component != ComponentTypeId<CharacterController>()) return;
        if (CharacterController* controller = world.GetComponent<CharacterController>(entity)) {
            controller->m_verticalSpeed = 0.0f;
            controller->m_velocity      = Math::vec3(0.0f, 0.0f, 0.0f);
        }
    }

    /*!
        \brief
        Play: a brand-new Jolt world and a zeroed clock. The records are empty,
        so the first Update builds every body from the components as they are.
    */
    void ECSPhysicsSystem::BeginSession() {

        State& s = *m_state;
        if (s.inSession) return;

        s.ResetClock();
        if (!s.physics.Init(s.settings))
            CRU_ERROR("ECSPhysicsSystem: the physics world did not start - physics components will be ignored");
        s.inSession = true;

    }   // ECSPhysicsSystem::BeginSession()

    /*!
        \brief
        Stop: every body and character out of Jolt, then Jolt itself (and its
        moved-body list and worker threads), then the clock. The World binding
        and its queries stay: Stop restores the scene into the same World.
    */
    void ECSPhysicsSystem::EndSession() noexcept {

        if (!m_state) return;
        State& s = *m_state;
        if (!s.inSession) return;

        s.RemoveAll();
        s.physics.Shutdown();
        s.ResetClock();
        s.inSession = false;

    }   // ECSPhysicsSystem::EndSession()

    bool          ECSPhysicsSystem::IsInSession() const       { return m_state->inSession; }
    Physics&      ECSPhysicsSystem::GetPhysics()              { return m_state->physics; }
    std::uint32_t ECSPhysicsSystem::GetStepsLastFrame() const { return m_state->stepsLastFrame; }

    // ------------------------------------------------------------------- //
    // THE FRAME
    // ------------------------------------------------------------------- //

    void ECSPhysicsSystem::Update(World& world, [[maybe_unused]] CommandBuffer& commands) {
        Advance(world, world.DeltaTime());
    }

    /*!
        \brief
        One frame of physics: count the steps that are due, sync the components
        in, then run those steps.
    */
    void ECSPhysicsSystem::Advance(World& world, float frameDeltaTime) {

        State& s = *m_state;
        s.stepsLastFrame = 0;
        if (!s.physics.IsInitialised()) return;

        s.BindWorld(world);

        // THE CLOCK ------------------------------------------------------------ //
        // Physics is only stable when every step is the same length, so frame time
        // goes into an accumulator and whole 1/60 s steps come out. A hitch longer
        // than six steps is dropped rather than caught up - catching up after a
        // two-second stall would just cause another stall.
        float dt = (frameDeltaTime > 0.0f && std::isfinite(frameDeltaTime)) ? frameDeltaTime : 0.0f;
        dt = std::min(dt, static_cast<float>(kMaxStepsPerFrame) * kFixedDeltaTime);

        // Vsync makes a 60 fps frame 16.9 ms one time and 16.5 ms the next. Left
        // alone that produces 1, 0, 2, 1, 0 steps and the game visibly stutters,
        // so a frame within 2% of one step of a whole number is rounded to it.
        const float whole = std::round(dt / kFixedDeltaTime);
        if (whole >= 1.0f && std::fabs(dt - whole * kFixedDeltaTime) < 0.02f * kFixedDeltaTime) dt = whole * kFixedDeltaTime;

        s.owedBefore = s.accumulator;    // DriveKinematic places each step in time with these two
        s.frameTime  = dt;
        s.accumulator += dt;
        const std::uint32_t steps = std::min(static_cast<std::uint32_t>(s.accumulator / kFixedDeltaTime + 1.0e-4f), kMaxStepsPerFrame);
        s.accumulator = std::max(s.accumulator - static_cast<float>(steps) * kFixedDeltaTime, 0.0f);

        // SYNC ONCE, THEN STEP -------------------------------------------------- //
        // Gameplay ran once this frame, so its intentions only need carrying over once.
        s.stepsThisFrame = steps;
        s.Sync(world);

        // Only on frames where the level actually grew. Jolt's static tree is
        // built for a world that stops changing, so one rebuild after a batch of
        // arena pieces pays for itself in every raycast afterwards.
        if (s.staticBodyMade) {
            s.physics.OptimizeBroadPhase();
            s.staticBodyMade = false;
        }

        for (std::uint32_t i = 0; i < steps; ++i) {
            s.stepOfFrame = i;
            s.Step(world);
        }
        s.stepsLastFrame = steps;

    }   // ECSPhysicsSystem::Advance()

    // A different world means a different scene: every body belongs to entities
    // that are gone, so they all go.
    void ECSPhysicsSystem::State::BindWorld(World& ecs) {

        if (world == &ecs && colliders.has_value()) return;

        RemoveAll();
        world = &ecs;
        // Excluding CharacterController matters: without it an entity with both
        // would get a body AND a capsule, the body would chase the capsule
        // around by teleport, and the mech would fight its own collider.
        colliders.emplace(ecs.BuildQuery<const Transform, const Collider>().Exclude<CharacterController>().Build());
        characters.emplace(ecs.CreateQuery<const Transform, CharacterController>());
        joints.emplace(ecs.CreateQuery<const PhysicsJoint>());

    }   // ECSPhysicsSystem::State::BindWorld()

    // The fixed-step clock and the counters records are stamped with: a new
    // session starts from zero, as a new system object used to.
    void ECSPhysicsSystem::State::ResetClock() {
        accumulator    = 0.0f;
        frame          = 0;
        step           = 0;
        stepsLastFrame = 0;
        staticBodyMade = false;
    }

    // ------------------------------------------------------------------- //
    // SYNC: COMPONENTS INTO THE PHYSICS WORLD
    // ------------------------------------------------------------------- //

    void ECSPhysicsSystem::State::Sync(World& ecs) {

        ++frame;

        // Each Transform is in its parent's frame (identity for a root). If that frame can't be
        // worked out this frame (a socket whose model isn't loaded yet), the entity is left as
        // it is - still marked seen, so its body isn't removed - and tried again next frame.
        colliders->ForEach([&](Entity entity, const Transform& transform, const Collider& collider) {
            Record& record = Touch(entity);
            const RigidBody* rigidbody = ecs.IsComponentActive<RigidBody>(entity) ? ecs.Read<RigidBody>(entity) : nullptr;
            Math::mat4 parentFrame(1.0f);
            if (ecs.TryGetParentFrame(entity, parentFrame)) SyncBody(record, transform, parentFrame, collider, rigidbody);
            else                                            StopIfNotDynamic(record, rigidbody);

            // On BOTH paths, not only when SyncBody ran: it is what clears last frame's
            // pushes from the record, and skipping it would push again with stale ones.
            // After SyncBody, which may have just (re)built the body.
            TakeForces(ecs, record, rigidbody);
        });

        characters->ForEach([&](Entity entity, const Transform& transform, CharacterController& controller) {
            CharacterRecord& record = TouchCharacter(entity);
            if (ecs.Read<Collider>(entity) != nullptr)
                WarnOnce(record.warned, kWarnTwoColliders, entity, "has a Collider and a CharacterController - only the CharacterController is used");
            Math::mat4 parentFrame(1.0f);
            if (!ecs.TryGetParentFrame(entity, parentFrame)) return;
            SyncCharacter(record, transform, parentFrame, controller);
        });

        // Not seen this frame: the entity was destroyed, or lost its collider.
        // Absence from the query is the only "it's gone" signal there is.
        for (auto it = records.begin(); it != records.end();) {
            if (it->second.seenFrame != frame) {
                Remove(it->second);
                it = records.erase(it);
            }
            else {
                ++it;
            }
        }

        for (auto it = characterRecords.begin(); it != characterRecords.end();) {
            if (it->second.seenFrame != frame) {
                physics.DestroyCharacter(it->second.character);
                it = characterRecords.erase(it);
            }
            else {
                ++it;
            }
        }

        // Joints last: a joint needs both of its bodies, so every body this
        // frame creates, rebuilds or removes has to be settled first.
        SyncJoints(ecs);

    }   // ECSPhysicsSystem::State::Sync()

    // ------------------------------------------------------------------- //
    // JOINTS
    // ------------------------------------------------------------------- //

    /*!
        \brief
        Every PhysicsJoint into Jolt: build the new ones, rebuild the changed
        ones, and remove the ones whose component (or entity) is gone.
    */
    void ECSPhysicsSystem::State::SyncJoints(World& ecs) {

        joints->ForEach([&](Entity entity, const PhysicsJoint& joint) {
            auto [it, inserted] = jointRecords.try_emplace(EntityToUserData(entity));
            JointRecord& record = it->second;
            if (inserted) record.entity = entity;
            record.seenFrame = frame;
            SyncJoint(ecs, record, joint);
        });

        // Same rule as bodies: not found by the query this frame means gone.
        for (auto it = jointRecords.begin(); it != jointRecords.end();) {
            if (it->second.seenFrame != frame) {
                physics.DestroyJoint(it->second.joint);     // a no-op if it went with one of its bodies
                it = jointRecords.erase(it);
            }
            else {
                ++it;
            }
        }

    }   // ECSPhysicsSystem::State::SyncJoints()

    /*!
        \brief
        Makes one entity's joint match its PhysicsJoint: finds both bodies,
        and (re)builds the joint if anything it was built from has changed.
    */
    void ECSPhysicsSystem::State::SyncJoint(World& ecs, JointRecord& record, const PhysicsJoint& joint) {

        // Broke earlier in this Play. It stays broken until Stop restores the
        // scene, which resets m_isBroken - Unity's broken joints are gone too.
        if (joint.m_isBroken) {
            DropJoint(record);
            return;
        }

        // THIS ENTITY'S BODY --------------------------------------------------- //
        const auto own = records.find(EntityToUserData(record.entity));
        if (own == records.end() || !own->second.body.IsValid()) {
            WarnOnce(record.warned, kWarnJointNoBody, record.entity, "has a PhysicsJoint but no body - add a Collider (and a RigidBody to let it move)");
            DropJoint(record);
            return;
        }
        const BodyHandle bodyA = own->second.body;
        bool anyDynamic = own->second.recipe.m_motion == BodyMotion::Dynamic;

        // THE OTHER ENTITY ----------------------------------------------------- //
        // Code's direct link wins; otherwise the saved scene id, looked up once
        // and remembered (a lookup searches the whole scene).
        Entity other = kNullEntity;
        if (joint.m_connectedEntity.IsValid()) {
            other = joint.m_connectedEntity;
        }
        else if (joint.m_connectedBody != 0) {
            const bool stale = record.resolvedId != joint.m_connectedBody
                            || (record.resolved.IsValid() && !ecs.IsAlive(record.resolved));
            if (stale) {
                record.resolvedId = joint.m_connectedBody;
                record.resolved   = scenes != nullptr ? scenes->Current().EntityOf(xresource::instance_guid{ joint.m_connectedBody })
                                                      : kNullEntity;
            }
            other = record.resolved;

            // Never quietly fall back to the world: a ragdoll limb pinned in
            // mid-air is far more confusing than a missing joint and a warning.
            if (!other.IsValid()) {
                WarnOnce(record.warned, kWarnJointMissing, record.entity, "has a PhysicsJoint whose connected body is not in this scene - the joint is left out");
                DropJoint(record);
                return;
            }
        }

        BodyHandle bodyB {};    // invalid = the world
        if (other.IsValid()) {
            if (other == record.entity) {
                WarnOnce(record.warned, kWarnJointSelf, record.entity, "has a PhysicsJoint connected to itself - leave the connected body empty to join it to the world");
                DropJoint(record);
                return;
            }
            const auto found = records.find(EntityToUserData(other));
            if (found == records.end() || !found->second.body.IsValid()) {
                WarnOnce(record.warned, kWarnJointNoOther, record.entity, "has a PhysicsJoint whose connected entity has no body - it needs a Collider");
                DropJoint(record);
                return;
            }
            bodyB = found->second.body;
            anyDynamic = anyDynamic || found->second.recipe.m_motion == BodyMotion::Dynamic;
        }

        // Legal, but holds nothing that moves - a joint between two pieces of arena.
        if (!anyDynamic)
            WarnOnce(record.warned, kWarnJointStatic, record.entity, "has a PhysicsJoint but neither end has a RigidBody - nothing can move, so it does nothing");

        // UP TO DATE? ------------------------------------------------------------ //
        const bool sameInputs = SameBody(record.bodyA, bodyA) && SameBody(record.bodyB, bodyB) && SameRecipe(joint, record.recipe);
        if (sameInputs && (record.joint.IsValid() || record.failed)) return;     // built, or failed and nothing has changed

        // (RE)BUILD ---------------------------------------------------------------- //
        // From the pose physics has the bodies in NOW: anchors and axes are
        // local to each entity, and Jolt wants world space.
        physics.DestroyJoint(record.joint);

        Math::vec3 positionA;
        Math::quat rotationA;
        physics.GetTransform(bodyA, positionA, rotationA);
        const Math::vec3 scaleA = WorldScaleOf(ecs, record.entity);   // world, not local: the entity may be a child

        JointDesc desc;
        desc.m_type             = joint.m_type;
        desc.m_bodyA            = bodyA;
        desc.m_bodyB            = bodyB;
        desc.m_anchor           = LocalToWorld(joint.m_anchor, positionA, rotationA, scaleA);
        desc.m_axis             = rotationA * joint.m_axis;          // a direction: turned, never scaled or moved
        desc.m_swingAxis        = rotationA * joint.m_swingAxis;
        desc.m_collideConnected = joint.m_enableCollision;
        desc.m_useLimits        = joint.m_useLimits;
        desc.m_minAngle         = joint.m_minAngle;
        desc.m_maxAngle         = joint.m_maxAngle;
        desc.m_twistMin         = joint.m_lowTwistLimit;
        desc.m_twistMax         = joint.m_highTwistLimit;
        desc.m_swing1           = joint.m_swing1Limit;
        desc.m_swing2           = joint.m_swing2Limit;
        desc.m_minDistance      = joint.m_minDistance;
        desc.m_maxDistance      = joint.m_maxDistance;
        desc.m_spring           = joint.m_spring;
        desc.m_damper           = joint.m_damper;
        desc.m_breakForce       = joint.m_breakForce;
        desc.m_breakTorque      = joint.m_breakTorque;
        desc.m_solverIterations         = joint.m_solverIterations;
        desc.m_solverVelocityIterations = joint.m_solverVelocityIterations;
        desc.m_userData         = EntityToUserData(record.entity);   // how a broken joint finds its way back here

        // The spring's far end: on the other entity, in its local space - or a
        // world point when the spring hangs from the world, as in Unity.
        if (bodyB.IsValid()) {
            Math::vec3 positionB;
            Math::quat rotationB;
            physics.GetTransform(bodyB, positionB, rotationB);
            desc.m_anchorB = LocalToWorld(joint.m_connectedAnchor, positionB, rotationB, WorldScaleOf(ecs, other));
        }
        else {
            desc.m_anchorB = joint.m_connectedAnchor;
        }

        record.joint  = physics.CreateJoint(desc);   // logs its own reason when it fails
        record.bodyA  = bodyA;
        record.bodyB  = bodyB;
        record.recipe = joint;
        record.failed = !record.joint.IsValid();

    }   // ECSPhysicsSystem::State::SyncJoint()

    // Out of Jolt, and forgotten, so the next build starts fresh. The warning
    // flags stay: a joint that keeps failing for the same reason says so once.
    void ECSPhysicsSystem::State::DropJoint(JointRecord& record) {
        physics.DestroyJoint(record.joint);
        record.joint  = JointHandle{};
        record.bodyA  = BodyHandle{};
        record.bodyB  = BodyHandle{};
        record.failed = false;
    }

    /*!
        \brief
        After each step: every joint Physics broke is marked on its component,
        so it is not rebuilt and gameplay can see it happened - Unity's
        OnJointBreak, as a flag to read.
    */
    void ECSPhysicsSystem::State::PullBrokenJoints(World& ecs) {

        const std::uint32_t broken = physics.GetBrokenJointCount();

        for (std::uint32_t i = 0; i < broken; ++i) {

            JointHandle   handle;
            std::uint64_t userData = 0;
            if (!physics.GetBrokenJoint(i, handle, userData)) continue;

            const auto found = jointRecords.find(userData);
            if (found == jointRecords.end()) continue;

            found->second.joint = JointHandle{};     // already destroyed by Physics
            if (PhysicsJoint* joint = ecs.GetComponent<PhysicsJoint>(found->second.entity)) joint->m_isBroken = true;
        }

    }   // ECSPhysicsSystem::State::PullBrokenJoints()

    /*!
        \brief
        Makes one entity's body match its Collider, RigidBody and Transform.
    */
    void ECSPhysicsSystem::State::SyncBody(Record& record, const Transform& transform, const Math::mat4& parentFrame, const Collider& collider, const RigidBody* rigidbody) {

        if (!Finite(transform.positions) || !Finite(transform.rotation)) {
            WarnOnce(record.warned, kWarnNaN, record.entity, "has NaN or infinity in its Transform - physics is leaving it alone until that is fixed");
            StopIfNotDynamic(record, rigidbody);
            return;
        }

        // Where it is in the world: a child's Transform is relative to its parent.
        Math::vec3 position, scale;
        Math::quat rotation;
        if (!ToWorld(parentFrame, transform, position, rotation, scale)) {
            WarnOnce(record.warned, kWarnNaN, record.entity, "has a parent transform that cannot be decomposed (a zero scale?) - physics is leaving it alone until that is fixed");
            StopIfNotDynamic(record, rigidbody);
            return;
        }

        // A RigidBody makes it dynamic (or kinematic, if ticked); a collider on its own is
        // part of the arena.
        const bool kinematic = rigidbody != nullptr && rigidbody->isKinematic;
        BodyDesc desc;
        // Scaled first, Unity-style: the floor cube at scale (15, 0.1, 15) gets a
        // 15 x 0.1 x 15 collider. A scale edit changes the shape, SameRecipe sees
        // that below, and the body is rebuilt - nothing extra needed for it. World scale, so a
        // scaled parent scales its children's colliders too.
        desc.m_shape       = ToShapeDesc(ScaledCollider(collider, scale));
        desc.m_position    = position;
        desc.m_rotation    = rotation;
        desc.m_motion      = kinematic ? BodyMotion::Kinematic : rigidbody != nullptr ? BodyMotion::Dynamic : BodyMotion::Static;
        desc.m_layer       = collider.m_layer;
        desc.m_friction    = collider.m_friction;
        desc.m_restitution = collider.m_bounciness;
        desc.m_userData    = EntityToUserData(record.entity);
        // Only for dynamic bodies: a kinematic one ignores mass, drag and gravity anyway, and
        // freezing axes on it would make no sense - its Transform decides every axis.
        if (rigidbody != nullptr && !kinematic) {
            desc.m_mass           = rigidbody->mass;
            desc.m_gravityFactor  = rigidbody->useGravity ? 1.0f : 0.0f;
            desc.m_linearDamping  = rigidbody->drag;
            desc.m_angularDamping = rigidbody->angularDrag;

            const RigidBodyConstraints& frozen = rigidbody->constraints;
            desc.m_lockedAxes = static_cast<std::uint8_t>(
                  (frozen.freezePositionX ? AxisLock::PositionX : 0) | (frozen.freezePositionY ? AxisLock::PositionY : 0)
                | (frozen.freezePositionZ ? AxisLock::PositionZ : 0) | (frozen.freezeRotationX ? AxisLock::RotationX : 0)
                | (frozen.freezeRotationY ? AxisLock::RotationY : 0) | (frozen.freezeRotationZ ? AxisLock::RotationZ : 0));

            // Everything frozen: nothing left to simulate, and Jolt cannot run a
            // dynamic body with no free axis. Static is what it effectively is -
            // it still blocks other bodies and can still be teleported via Transform.
            if (desc.m_lockedAxes == AxisLock::All) desc.m_motion = BodyMotion::Static;
        }

        // Two NonMoving bodies never collide, so a crate left on that layer falls
        // through the floor and through other crates. Easy mistake, hard symptom.
        // (Not for a kinematic body: it never needs to hit the floor, and NonMoving still
        // collides with the crates and mechs it pushes.)
        if (rigidbody != nullptr && !kinematic && collider.m_layer == PhysicsLayer::NonMoving)
            WarnOnce(record.warned, kWarnLayer, record.entity, "has a RigidBody but sits on the NonMoving layer, which collides with nothing that matters - set Collider::m_layer to Moving");

        // NEW, OR A SETTING CHANGED: (RE)BUILD ---------------------------------- //
        if (!record.body.IsValid() || !SameRecipe(desc, record.recipe)) {

            if (record.failed && SameRecipe(desc, record.recipe)) return;    // it failed before; wait for a change

            Remove(record);
            record.body     = physics.CreateBody(desc);
            record.recipe   = desc;
            record.failed   = !record.body.IsValid();
            record.position    = transform.positions;
            record.rotation    = transform.rotation;
            record.parentFrame = parentFrame;
            record.velocity = rigidbody != nullptr ? rigidbody->velocity : Math::vec3(0.0f, 0.0f, 0.0f);
            record.kinematicFrom    = record.kinematicTo    = position;
            record.kinematicFromRot = record.kinematicToRot = rotation;
            record.movedFrames = 0;

            // Carry the velocity across, so a falling crate keeps falling when a
            // setting is edited mid-flight rather than stopping dead in the air.
            // Also what makes unticking isKinematic mid-Play look right: the body
            // leaves with the speed its Transform was moving it at (PullBodies kept
            // RigidBody::velocity up to date while it was kinematic).
            if (record.body.IsValid() && desc.m_motion == BodyMotion::Dynamic) physics.SetLinearVelocity(record.body, record.velocity);

            if (record.body.IsValid() && desc.m_motion == BodyMotion::Static) staticBodyMade = true;
            return;
        }

        const bool moved = !Same(transform.positions, record.position) || !Same(transform.rotation, record.rotation)
                        || parentFrame != record.parentFrame;

        // KINEMATIC: THE TRANSFORM IS WHERE IT SHOULD BE BY THE END OF THIS FRAME -- //
        // Not teleported: DriveKinematic moves it along the way from last frame's
        // Transform to this one with MoveKinematic, which gives it the velocity that
        // pushes and carries things. Every frame, steps or not, so the pair always
        // spans exactly the last frame.
        if (kinematic) {
            record.kinematicFrom    = record.kinematicTo;
            record.kinematicFromRot = record.kinematicToRot;
            record.kinematicTo    = position;
            record.kinematicToRot = rotation;
            if (moved) {
                record.position    = transform.positions;
                record.rotation    = transform.rotation;
                record.parentFrame = parentFrame;
            }
            return;     // gameplay's velocity is ignored too: the Transform decides it, as in Unity
        }

        // GAMEPLAY WROTE THE TRANSFORM, OR A PARENT MOVED: TELEPORT --------------- //
        if (moved) {
            physics.SetTransform(record.body, position, rotation);
            record.position    = transform.positions;
            record.rotation    = transform.rotation;
            record.parentFrame = parentFrame;
        }

        // A STATIC BODY THAT KEEPS MOVING ----------------------------------------- //
        // Teleported, it has no velocity: it jitters through crates instead of pushing
        // them, things on it are not carried, and sleeping ones are not woken. One
        // teleport (a respawn) is fine; being moved frame after frame means it is
        // animated, and should be kinematic.
        record.movedFrames = (moved && desc.m_motion == BodyMotion::Static) ? record.movedFrames + 1 : 0;
        if (record.movedFrames >= kMovingStaticFrames)
            WarnOnce(record.warned, kWarnMovingStatic, record.entity, "is a static collider that is moved every frame - give it a RigidBody with isKinematic ticked, so it pushes and carries what it touches instead of teleporting through it");

        // GAMEPLAY WROTE THE VELOCITY: PASS IT ON -------------------------------- //
        if (rigidbody != nullptr && !Same(rigidbody->velocity, record.velocity)) {
            physics.SetLinearVelocity(record.body, rigidbody->velocity);
            record.velocity = rigidbody->velocity;
        }

    }   // ECSPhysicsSystem::State::SyncBody()

    /*!
        \brief
        For a frame where SyncBody can't place the body (no parent frame, a
        Transform it can't use): a body whose RigidBody is switched off or gone
        must still stop being dynamic. It is rebuilt static where Jolt has it
        now, so it stops falling; the next good frame moves it to its Transform.
    */
    void ECSPhysicsSystem::State::StopIfNotDynamic(Record& record, const RigidBody* rigidbody) {

        if (rigidbody != nullptr || !record.body.IsValid() || record.recipe.m_motion != BodyMotion::Dynamic) return;

        BodyDesc desc = record.recipe;
        physics.GetTransform(record.body, desc.m_position, desc.m_rotation);
        desc.m_motion = BodyMotion::Static;

        Remove(record);
        record.body     = physics.CreateBody(desc);
        record.recipe   = desc;
        record.failed   = !record.body.IsValid();
        record.velocity = Math::vec3(0.0f, 0.0f, 0.0f);
        if (record.body.IsValid()) staticBodyMade = true;

    }   // ECSPhysicsSystem::State::StopIfNotDynamic()

    /*!
        \brief
        Makes one entity's character match its CharacterController and Transform.
        Simpler than a body: a character has no velocity of its own to argue with.
    */
    void ECSPhysicsSystem::State::SyncCharacter(CharacterRecord& record, const Transform& transform, const Math::mat4& parentFrame, CharacterController& controller) {

        if (!Finite(transform.positions)) {
            WarnOnce(record.warned, kWarnNaN, record.entity, "has NaN or infinity in its Transform - physics is leaving it alone until that is fixed");
            return;
        }

        // Where it is in the world: a child's Transform is relative to its parent.
        const Math::vec3 position = parentFrame == Math::mat4(1.0f)
            ? transform.positions
            : Math::vec3(parentFrame * Math::vec4(transform.positions, 1.0f));

        CharacterDesc desc;
        desc.m_position     = position;
        desc.m_height       = controller.m_height;
        desc.m_radius       = controller.m_radius;
        desc.m_center       = controller.m_center;
        desc.m_slopeLimit   = controller.m_slopeLimit;
        desc.m_stepOffset   = controller.m_stepOffset;
        desc.m_pushStrength = controller.m_pushStrength;
        desc.m_layer        = controller.m_layer;
        desc.m_userData     = EntityToUserData(record.entity);

        // NEW, OR A SETTING CHANGED: (RE)BUILD ---------------------------------- //
        if (!record.character.IsValid() || !SameRecipe(desc, record.recipe)) {

            if (record.failed && SameRecipe(desc, record.recipe)) return;

            physics.DestroyCharacter(record.character);
            record.character = physics.CreateCharacter(desc);
            record.recipe    = desc;
            record.failed    = !record.character.IsValid();
            record.position  = transform.positions;
            record.parentFrame = parentFrame;

            // Answer "am I standing on something" correctly on the very first
            // frame, so a jump on frame one is not silently swallowed.
            controller.m_isGrounded = physics.IsGrounded(record.character);
            return;
        }

        // GAMEPLAY WROTE THE TRANSFORM (A RESPAWN), OR A PARENT MOVED ------------- //
        const bool respawned = !Same(transform.positions, record.position);
        if (respawned || parentFrame != record.parentFrame) {
            physics.SetCharacterPosition(record.character, position);
            record.position    = transform.positions;
            record.parentFrame = parentFrame;
            if (respawned) controller.m_verticalSpeed = 0.0f;   // do not arrive carrying the last fall
        }

    }   // ECSPhysicsSystem::State::SyncCharacter()

    // ------------------------------------------------------------------- //
    // STEP: ONE TICK, AND THE RESULTS BACK OUT
    // ------------------------------------------------------------------- //

    void ECSPhysicsSystem::State::Step(World& ecs) {

        ++step;

        // Kinematic bodies first: setting their velocity for this step is what the
        // characters read as "the ground is moving" after the step, so a mech on a
        // lift moves with the lift's speed for THIS step, not last step's.
        // Every step, even when the Transform did not move: Jolt keeps a kinematic
        // body's velocity after the step, so without a fresh MoveKinematic (to where
        // it already is, which gives zero) a lift that stopped would carry on moving.
        for (auto& [key, record] : records)
            if (record.seenFrame == frame && record.body.IsValid() && record.recipe.m_motion == BodyMotion::Kinematic)
                DriveKinematic(record);

        // Forces go in last, right before the step that uses them. Jolt zeroes
        // every force when the step ends, so this runs again for the next one.
        // Dynamic bodies only: forces do nothing to a kinematic or static body, and
        // TakeForces has still emptied the RigidBody, so pushes don't pile up there.
        for (auto& [key, record] : records)
            if (record.seenFrame == frame && record.body.IsValid() && record.recipe.m_motion == BodyMotion::Dynamic)
                PushForces(record);

        physics.Step(kFixedDeltaTime);

        // Characters move AFTER the world steps, so everything they stand on is already
        // where this step put it. A mech on a lift then ends every step exactly on it:
        // going down it follows the lift onto its new top, going up it climbs out of the
        // lift that just rose into it. Moved before the step instead (as it was until
        // v2.6), it landed on the lift's OLD top going down and trailed a step behind -
        // 3 cm at 2 m/s, measured. The cost: a crate the mech shoves gets the push on
        // the next step, 1/60 s later, which nobody can see.
        for (auto& [key, record] : characterRecords)
            if (record.seenFrame == frame) StepCharacter(ecs, record);

        PullBodies(ecs);
        PullBrokenJoints(ecs);      // every step: Physics refills its broken list each Step

    }   // ECSPhysicsSystem::State::Step()

    /*!
        \brief
        Collects this frame's pushes for one body into its record: the
        designer's ConstantForce, plus whatever gameplay added to the RigidBody
        since last frame. The RigidBody totals are then zeroed, so each
        AddForce call pushes for one frame and no longer, as in Unity.
    */
    void ECSPhysicsSystem::State::TakeForces(World& ecs, Record& record, const RigidBody* rigidbody) {

        const Math::vec3 zero(0.0f, 0.0f, 0.0f);

        // CONSTANTFORCE: re-read every frame, never written -------------------- //
        // Overwriting (not adding to) the record is what clears last frame's push.
        // A switched-off ConstantForce pushes nothing, like a disabled one in Unity.
        const ConstantForce* constant = ecs.IsComponentActive<ConstantForce>(record.entity) ? ecs.Read<ConstantForce>(record.entity) : nullptr;
        record.force          = constant != nullptr ? constant->m_force          : zero;
        record.torque         = constant != nullptr ? constant->m_torque         : zero;
        record.relativeForce  = constant != nullptr ? constant->m_relativeForce  : zero;
        record.relativeTorque = constant != nullptr ? constant->m_relativeTorque : zero;
        record.impulse        = zero;

        if (rigidbody == nullptr) {
            // A static body ignores forces, so without this the designer just sees nothing happen.
            // Only when there is no RigidBody at all: a switched-off one is deliberate.
            if (constant != nullptr && ecs.Read<RigidBody>(record.entity) == nullptr)
                WarnOnce(record.warned, kWarnLoneForce, record.entity, "has a ConstantForce but no RigidBody - only a RigidBody can be pushed, so it does nothing");
            return;
        }

        // RIGIDBODY: gameplay's AddForce / AddTorque / ApplyImpulse ------------- //
        // Checked through the read-only pointer first. A mutable GetComponent
        // stamps the component as changed, and every resting crate would be
        // marked changed every frame for nothing.
        const bool pushed = !Same(rigidbody->force, zero) || !Same(rigidbody->torque, zero);

        // A force is a push FOR THIS FRAME: on a frame with no step it is simply
        // dropped, which is what keeps it frame-rate independent (carrying it over
        // would double the push on the next frame). A kick is one event, not a
        // rate, so it stays on the component until a frame that steps.
        const bool kicked = !Same(rigidbody->impulse, zero) && stepsThisFrame > 0;

        if (!pushed && !kicked) return;

        RigidBody* writable = ecs.GetComponent<RigidBody>(record.entity);
        if (writable == nullptr) return;

        record.force  += writable->force;
        record.torque += writable->torque;
        writable->force  = zero;
        writable->torque = zero;

        if (kicked) {
            record.impulse    = writable->impulse;
            writable->impulse = zero;
        }

    }   // ECSPhysicsSystem::State::TakeForces()

    /*!
        \brief
        Hands one body's pushes for this frame to Jolt, ahead of one step.
        Called before every step of the frame; the impulse only lands on the first.
    */
    void ECSPhysicsSystem::State::PushForces(Record& record) {

        const Math::vec3 zero(0.0f, 0.0f, 0.0f);

        // Every call is guarded, not just tidied: AddForce WAKES the body even
        // when the force is zero, so passing zeros along would keep every
        // sleeping crate in the level awake and simulated forever.
        if (!Same(record.force,  zero)) physics.AddForce (record.body, record.force);
        if (!Same(record.torque, zero)) physics.AddTorque(record.body, record.torque);

        // Relative pushes follow the body as it is NOW, read from Jolt each step.
        // By the second step of a frame the Transform is out of date, and a
        // spinning rocket would thrust where it pointed, not where it points.
        if (!Same(record.relativeForce, zero) || !Same(record.relativeTorque, zero)) {
            Math::vec3 position;
            Math::quat rotation;
            physics.GetTransform(record.body, position, rotation);

            if (!Same(record.relativeForce,  zero)) physics.AddForce (record.body, rotation * record.relativeForce);
            if (!Same(record.relativeTorque, zero)) physics.AddTorque(record.body, rotation * record.relativeTorque);
        }

        // Instant, so once: spent after the first step, or a 2-step frame would kick twice.
        if (!Same(record.impulse, zero)) {
            physics.ApplyImpulse(record.body, record.impulse);
            record.impulse = zero;
        }

    }   // ECSPhysicsSystem::State::PushForces()

    /*!
        \brief
        Moves one kinematic body to where its Transform was at the moment this
        step ends, ahead of the step.
        \details
        Frames and steps don't line up: at 70 fps one step covers one frame's
        move and the next covers two. Aiming each step at the end of the frame's
        move would make the lift speed up and slow down every few frames, and a
        crate riding it would slide. So each step is placed in time instead.
        Physics runs behind the frame by what the accumulator still owes, so this
        step ends (step + 1) * fixed dt - owedBefore after last frame ended.
        That is always inside the last frame (owedBefore is under one step, and
        the last step never passes the frame's end), so the share is 0..1 of the
        way from last frame's Transform to this one. The lift then moves at the
        Transform's real speed - the speed a mech on it rides at.
        MoveKinematic works out the velocity that reaches the aim in one step.
    */
    void ECSPhysicsSystem::State::DriveKinematic(Record& record) {

        const float stepEnd = static_cast<float>(stepOfFrame + 1) * kFixedDeltaTime - owedBefore;
        const float share   = frameTime > 0.0f ? std::clamp(stepEnd / frameTime, 0.0f, 1.0f) : 1.0f;

        const Math::vec3 position = glm::mix(record.kinematicFrom, record.kinematicTo, share);
        const Math::quat rotation = glm::slerp(record.kinematicFromRot, record.kinematicToRot, share);
        physics.MoveKinematic(record.body, position, rotation, kFixedDeltaTime);

    }   // ECSPhysicsSystem::State::DriveKinematic()

    /*!
        \brief
        One step of walking: SimpleMove's velocity sideways, gravity and jumping
        up and down. This is the only place either of those is applied, because
        a character Jolt pulled down on its own could never be made to jump.
    */
    void ECSPhysicsSystem::State::StepCharacter(World& ecs, CharacterRecord& record) {

        CharacterController* controller = ecs.GetComponent<CharacterController>(record.entity);
        if (controller == nullptr || !record.character.IsValid()) return;

        const float gravity  = settings.m_gravity.y;      // -9.81
        const bool  grounded = physics.IsGrounded(record.character);

        // THE GROUND ------------------------------------------------------------- //
        // What the mech stands on may be moving: a kinematic lift or platform. Every
        // speed below is then RELATIVE TO IT, the way you walk on a moving train -
        // which is what carries the mech along. Zero on static ground and in the air,
        // so nothing changes there. (Jolt's own recipe for CharacterVirtual.)
        const Math::vec3 ground = grounded ? physics.GetGroundVelocity(record.character)
                                           : Math::vec3(0.0f, 0.0f, 0.0f);

        // UP AND DOWN ------------------------------------------------------------ //
        // Landed: stop falling - which on a moving floor means moving up or down WITH
        // it, at its speed. Only when not already moving away from it, so a jump that
        // just left a rising lift is not pulled back down onto it.
        float vertical = controller->m_verticalSpeed;
        if (grounded && vertical < ground.y) vertical = ground.y;

        // The launch speed that reaches exactly the requested height, from
        // v = sqrt(2 * g * h), on top of the floor's: a jump from a rising lift
        // goes higher, the same jump from a falling one less high.
        // A jump request lasts one step, then it is spent.
        if (grounded && controller->m_jumpHeight > 0.0f)
            vertical = ground.y + std::sqrt(2.0f * std::fabs(gravity) * controller->m_jumpHeight);
        controller->m_jumpHeight = 0.0f;

        // Gravity applies even while standing: a little downward speed is what
        // keeps the feet pressed into the floor walking down a ramp.
        vertical += gravity * kFixedDeltaTime;

        // MOVE -------------------------------------------------------------------- //
        // The walk plus the floor's own sideways speed. Only while standing on it:
        // stepping or jumping off a sideways-moving platform keeps just the walk, so
        // the mech does not keep the platform's speed in the air (yet).
        const Math::vec3 sideways = controller->m_moveVelocity + Math::vec3(ground.x, 0.0f, ground.z);
        physics.MoveCharacter(record.character, Math::vec3(sideways.x, vertical, sideways.z), kFixedDeltaTime);

        // Rising, but not as fast as asked: something is overhead, so stop
        // rising. Without this the mech keeps "climbing" on paper after a head
        // bump and hangs in the air before falling. "Rising" means faster than the
        // floor: riding a lift up is not a head bump.
        const Math::vec3 achieved = physics.GetCharacterVelocity(record.character);
        if (vertical > ground.y && vertical > 0.0f && achieved.y < vertical) vertical = std::max(achieved.y, 0.0f);

        // RESULTS ------------------------------------------------------------------ //
        controller->m_verticalSpeed = vertical;
        controller->m_isGrounded    = physics.IsGrounded(record.character);
        controller->m_velocity      = achieved;

        // Only touch the Transform when it really moved. A mutable GetComponent
        // stamps the component as changed, and an idle mech standing still would
        // otherwise mark itself changed every single step.
        // Jolt reports world space; the Transform is in the parent's frame.
        Math::vec3 position = physics.GetCharacterPosition(record.character);
        Math::quat unusedRotation(1.0f, 0.0f, 0.0f, 0.0f);
        ToLocal(record.parentFrame, position, unusedRotation);
        if (!Same(position, record.position)) {
            if (Transform* transform = ecs.GetComponent<Transform>(record.entity)) transform->positions = position;
            record.position = position;
        }

    }   // ECSPhysicsSystem::State::StepCharacter()

    // Everything Jolt moved in that step, copied into its Transform.
    void ECSPhysicsSystem::State::PullBodies(World& ecs) {

        const std::uint32_t moved = physics.GetMovedBodyCount();

        for (std::uint32_t i = 0; i < moved; ++i) {

            std::uint64_t userData = 0;
            Math::vec3    position;
            Math::quat    rotation;
            if (!physics.GetMovedBody(i, userData, position, rotation)) continue;

            const auto found = records.find(userData);
            if (found == records.end() || found->second.seenFrame != frame) continue;

            Record& record = found->second;

            // A kinematic body went where its Transform sent it, so the Transform is not
            // written back: it is the source here, and Jolt's version of it (integrated
            // from a velocity) would slowly drift from what the script or animation set.
            if (record.recipe.m_motion != BodyMotion::Kinematic) {

                // Jolt reports world space; the Transform is in the parent's frame.
                ToLocal(record.parentFrame, position, rotation);

                if (Transform* transform = ecs.GetComponent<Transform>(record.entity)) {
                    transform->positions = position;
                    transform->rotation  = rotation;
                }
                record.position = position;
                record.rotation = rotation;
            }

            // A switched-off RigidBody keeps the velocity it had (it comes back with it).
            // Written for a kinematic body too: gameplay can read how fast the lift is going.
            if (RigidBody* rigidbody = ecs.IsComponentActive<RigidBody>(record.entity) ? ecs.GetComponent<RigidBody>(record.entity) : nullptr) {
                rigidbody->velocity = physics.GetLinearVelocity(record.body);
                record.velocity     = rigidbody->velocity;
            }

            record.pulledStep = step;
        }

        // A body that stopped appearing has gone to sleep. Its last reported
        // velocity was whatever was still left under Jolt's sleep threshold, and
        // it would sit in the component forever - so gameplay asking "is this
        // still moving" would say yes about a crate that settled a minute ago.
        // Zero it once; after that record.velocity matches and this does nothing.
        const Math::vec3 zero(0.0f, 0.0f, 0.0f);
        for (auto& [key, record] : records) {

            if (record.pulledStep == step || record.seenFrame != frame) continue;
            if (!record.body.IsValid() || Same(record.velocity, zero)) continue;

            if (RigidBody* rigidbody = ecs.GetComponent<RigidBody>(record.entity)) rigidbody->velocity = zero;
            record.velocity = zero;
        }

    }   // ECSPhysicsSystem::State::PullBodies()

    // ------------------------------------------------------------------- //
    // RECORDS
    // ------------------------------------------------------------------- //

    // Finds (or starts) an entity's record and stamps it with this frame.
    Record& ECSPhysicsSystem::State::Touch(const Entity& entity) {

        auto [it, inserted] = records.try_emplace(EntityToUserData(entity));
        if (inserted) it->second.entity = entity;
        it->second.seenFrame = frame;
        return it->second;

    }   // ECSPhysicsSystem::State::Touch()

    // Takes the entity's body out of the world. The record itself stays: a
    // rebuild calls this and then fills it in again.
    void ECSPhysicsSystem::State::Remove(Record& record) {
        physics.DestroyBody(record.body);
        record.body = BodyHandle{};
    }

    CharacterRecord& ECSPhysicsSystem::State::TouchCharacter(const Entity& entity) {

        auto [it, inserted] = characterRecords.try_emplace(EntityToUserData(entity));
        if (inserted) it->second.entity = entity;
        it->second.seenFrame = frame;
        return it->second;

    }   // ECSPhysicsSystem::State::TouchCharacter()

    void ECSPhysicsSystem::State::RemoveAll() {

        // Joints first: each one points at its bodies. (Removing a body would
        // take its joints with it anyway; this just says so out loud.)
        for (auto& [key, record] : jointRecords) physics.DestroyJoint(record.joint);
        jointRecords.clear();

        for (auto& [key, record] : records) Remove(record);
        records.clear();

        for (auto& [key, record] : characterRecords) physics.DestroyCharacter(record.character);
        characterRecords.clear();

    }   // ECSPhysicsSystem::State::RemoveAll()

}   // NAMESPACE CRUCIBLE
