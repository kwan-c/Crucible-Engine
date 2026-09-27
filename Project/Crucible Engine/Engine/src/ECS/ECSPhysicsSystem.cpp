/*************************************************************************
* \file:         ECSPhysicsSystem.cpp
* \author:       Abhishek Akilandeswari Patchamuthu (v1.0 skeleton),
*                Hugo (v2.0 physics binding)
* \date          27 September 2026
* \version       2.0
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
*                  - the velocity changed  -> gameplay set it: pass it on
*                An entity that no longer has physics components, or no
*                longer exists, is not found by the query, so its record
*                and its body are removed.
*
* \history
*    -v1.0       stub implementation, no physics logic yet
*    -v2.0       the real binding
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
        return a.m_X == b.m_X && a.m_Y == b.m_Y && a.m_Z == b.m_Z;
    }

    bool Same(const Math::quat& a, const Math::quat& b) {
        return a.m_X == b.m_X && a.m_Y == b.m_Y && a.m_Z == b.m_Z && a.m_W == b.m_W;
    }

    bool Finite(const Math::vec3& v) { return std::isfinite(v.m_X) && std::isfinite(v.m_Y) && std::isfinite(v.m_Z); }
    bool Finite(const Math::quat& q) { return std::isfinite(q.m_X) && std::isfinite(q.m_Y) && std::isfinite(q.m_Z) && std::isfinite(q.m_W); }

    // The collider as gameplay states it (full sizes, Unity style) turned into
    // what Jolt wants (half sizes). The ONE place that conversion happens.
    ShapeDesc ToShapeDesc(const Collider& collider) {

        ShapeDesc shape;
        shape.m_type        = collider.m_type;
        shape.m_halfExtents = Math::vec3(collider.m_size.m_X * 0.5f, collider.m_size.m_Y * 0.5f, collider.m_size.m_Z * 0.5f);
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
            && a.m_friction == b.m_friction && a.m_restitution == b.m_restitution;

    }   // SameRecipe()

    bool SameRecipe(const CharacterDesc& a, const CharacterDesc& b) {
        return a.m_height == b.m_height && a.m_radius == b.m_radius && Same(a.m_center, b.m_center)
            && a.m_slopeLimit == b.m_slopeLimit && a.m_stepOffset == b.m_stepOffset
            && a.m_pushStrength == b.m_pushStrength && a.m_layer == b.m_layer;
    }   // SameRecipe()

    // Once per entity, ever. A warning that repeats every frame buries the log.
    void WarnOnce(bool& warned, const Entity& entity, const char* message) {
        if (warned) return;
        warned = true;
        CRU_WARN("entity {}:{} {}", entity.index, entity.generation, message);
    }   // WarnOnce()

    // --- one entity's physics, as the binding remembers it ------------------ //

    struct Record {

        Entity        entity    {};
        BodyHandle    body      {};
        BodyDesc      recipe    {};      // what the body was built from
        bool          failed    = false; // building from that description failed: wait for it to change

        Math::vec3    position  = Math::vec3(0.0f, 0.0f, 0.0f);   // the Transform as physics last left it
        Math::quat    rotation  = Math::quat::fromIdentity();
        Math::vec3    velocity  = Math::vec3(0.0f, 0.0f, 0.0f);

        std::uint64_t seenFrame = 0;     // the last frame the query found this entity
        bool          warned    = false; // one warning per entity is plenty

    };  // RECORD STRUCT

    // The same notebook for a mech. Kept apart from Record because almost
    // nothing about the two paths is shared: different handle, different
    // description, and a character is walked by hand every step.
    struct CharacterRecord {

        Entity          entity    {};
        CharacterHandle character {};
        CharacterDesc   recipe    {};
        bool            failed    = false;

        Math::vec3      position  = Math::vec3(0.0f, 0.0f, 0.0f);   // where physics last put it
        std::uint64_t   seenFrame = 0;
        bool            warned    = false;

    };  // CHARACTERRECORD STRUCT

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    // ------------------------------------------------------------------- //
    // STATE
    // ------------------------------------------------------------------- //

    struct ECSPhysicsSystem::State {

        explicit State(const PhysicsSettings& physicsSettings) : settings(physicsSettings) {}

        PhysicsSettings settings;
        Physics         physics;
        World*          world = nullptr;

        // Read-only on purpose: taking them non-const would mark every Transform
        // as changed every frame, and other systems use that flag to decide what
        // to re-upload. Physics writes back through GetComponent, so only the
        // bodies that actually moved are stamped.
        std::optional<Query<const Transform, const Collider>> colliders;

        std::optional<Query<const Transform, CharacterController>> characters;

        std::unordered_map<std::uint64_t, Record>          records;           // keyed by EntityToUserData(entity)
        std::unordered_map<std::uint64_t, CharacterRecord> characterRecords;  // same key, the mechs

        float         accumulator    = 0.0f;                    // time owed to the simulation, under one step
        std::uint64_t frame          = 0;
        std::uint32_t stepsLastFrame = 0;

        void BindWorld(World& ecs);
        void Sync(World& ecs);
        void SyncBody(Record& record, const Transform& transform, const Collider& collider, const RigidBody* rigidbody);
        void SyncCharacter(CharacterRecord& record, const Transform& transform, CharacterController& controller);
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

    ECSPhysicsSystem::ECSPhysicsSystem(const PhysicsSettings& settings) : m_state{ std::make_unique<State>(settings) } {}

    ECSPhysicsSystem::~ECSPhysicsSystem() { Shutdown(); }

    void ECSPhysicsSystem::Init() {
        if (!m_state->physics.Init(m_state->settings))
            CRU_ERROR("ECSPhysicsSystem: the physics world did not start - physics components will be ignored");
    }

    void ECSPhysicsSystem::Shutdown() noexcept {
        if (!m_state) return;
        m_state->RemoveAll();
        m_state->physics.Shutdown();
        m_state->world = nullptr;
    }

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

        s.accumulator += dt;
        const std::uint32_t steps = std::min(static_cast<std::uint32_t>(s.accumulator / kFixedDeltaTime + 1.0e-4f), kMaxStepsPerFrame);
        s.accumulator = std::max(s.accumulator - static_cast<float>(steps) * kFixedDeltaTime, 0.0f);

        // SYNC ONCE, THEN STEP -------------------------------------------------- //
        // Gameplay ran once this frame, so its intentions only need carrying over once.
        s.Sync(world);
        for (std::uint32_t i = 0; i < steps; ++i) s.Step(world);
        s.stepsLastFrame = steps;

    }   // ECSPhysicsSystem::Advance()

    // A different world means a different scene: every body belongs to entities
    // that are gone, so they all go.
    void ECSPhysicsSystem::State::BindWorld(World& ecs) {

        if (world == &ecs) return;

        RemoveAll();
        world = &ecs;
        colliders.emplace(ecs.CreateQuery<const Transform, const Collider>());
        characters.emplace(ecs.CreateQuery<const Transform, CharacterController>());

    }   // ECSPhysicsSystem::State::BindWorld()

    // ------------------------------------------------------------------- //
    // SYNC: COMPONENTS INTO THE PHYSICS WORLD
    // ------------------------------------------------------------------- //

    void ECSPhysicsSystem::State::Sync(World& ecs) {

        ++frame;

        colliders->ForEach([&](Entity entity, const Transform& transform, const Collider& collider) {
            Record& record = Touch(entity);
            SyncBody(record, transform, collider, ecs.Read<RigidBody>(entity));
        });

        characters->ForEach([&](Entity entity, const Transform& transform, CharacterController& controller) {
            CharacterRecord& record = TouchCharacter(entity);
            if (ecs.Read<Collider>(entity) != nullptr)
                WarnOnce(record.warned, entity, "has a Collider and a CharacterController - only the CharacterController is used");
            SyncCharacter(record, transform, controller);
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

    }   // ECSPhysicsSystem::State::Sync()

    /*!
        \brief
        Makes one entity's body match its Collider, RigidBody and Transform.
    */
    void ECSPhysicsSystem::State::SyncBody(Record& record, const Transform& transform, const Collider& collider, const RigidBody* rigidbody) {

        if (!Finite(transform.positions) || !Finite(transform.rotation)) {
            WarnOnce(record.warned, record.entity, "has NaN or infinity in its Transform - physics is leaving it alone until that is fixed");
            return;
        }

        // A RigidBody makes it dynamic; a collider on its own is part of the arena.
        BodyDesc desc;
        desc.m_shape       = ToShapeDesc(collider);
        desc.m_position    = transform.positions;
        desc.m_rotation    = transform.rotation;
        desc.m_motion      = rigidbody != nullptr ? BodyMotion::Dynamic : BodyMotion::Static;
        desc.m_layer       = collider.m_layer;
        desc.m_friction    = collider.m_friction;
        desc.m_restitution = collider.m_bounciness;
        desc.m_userData    = EntityToUserData(record.entity);
        if (rigidbody != nullptr) desc.m_mass = rigidbody->mass;

        // Two NonMoving bodies never collide, so a crate left on that layer falls
        // through the floor and through other crates. Easy mistake, hard symptom.
        if (rigidbody != nullptr && collider.m_layer == PhysicsLayer::NonMoving)
            WarnOnce(record.warned, record.entity, "has a RigidBody but sits on the NonMoving layer, which collides with nothing that matters - set Collider::m_layer to Moving");

        if (!Same(transform.scale, Math::vec3(1.0f, 1.0f, 1.0f)))
            WarnOnce(record.warned, record.entity, "has a Transform scale other than 1 - colliders are not scaled yet, so the collider will not match the mesh");

        // NEW, OR A SETTING CHANGED: (RE)BUILD ---------------------------------- //
        if (!record.body.IsValid() || !SameRecipe(desc, record.recipe)) {

            if (record.failed && SameRecipe(desc, record.recipe)) return;    // it failed before; wait for a change

            Remove(record);
            record.body     = physics.CreateBody(desc);
            record.recipe   = desc;
            record.failed   = !record.body.IsValid();
            record.position = transform.positions;
            record.rotation = transform.rotation;
            record.velocity = rigidbody != nullptr ? rigidbody->velocity : Math::vec3(0.0f, 0.0f, 0.0f);

            // Carry the velocity across, so a falling crate keeps falling when a
            // setting is edited mid-flight rather than stopping dead in the air.
            if (record.body.IsValid() && rigidbody != nullptr) physics.SetLinearVelocity(record.body, record.velocity);
            return;
        }

        // GAMEPLAY WROTE THE TRANSFORM: TELEPORT --------------------------------- //
        if (!Same(transform.positions, record.position) || !Same(transform.rotation, record.rotation)) {
            physics.SetTransform(record.body, transform.positions, transform.rotation);
            record.position = transform.positions;
            record.rotation = transform.rotation;
        }

        // GAMEPLAY WROTE THE VELOCITY: PASS IT ON -------------------------------- //
        if (rigidbody != nullptr && !Same(rigidbody->velocity, record.velocity)) {
            physics.SetLinearVelocity(record.body, rigidbody->velocity);
            record.velocity = rigidbody->velocity;
        }

    }   // ECSPhysicsSystem::State::SyncBody()

    /*!
        \brief
        Makes one entity's character match its CharacterController and Transform.
        Simpler than a body: a character has no velocity of its own to argue with.
    */
    void ECSPhysicsSystem::State::SyncCharacter(CharacterRecord& record, const Transform& transform, CharacterController& controller) {

        if (!Finite(transform.positions)) {
            WarnOnce(record.warned, record.entity, "has NaN or infinity in its Transform - physics is leaving it alone until that is fixed");
            return;
        }

        CharacterDesc desc;
        desc.m_position     = transform.positions;
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

            // Answer "am I standing on something" correctly on the very first
            // frame, so a jump on frame one is not silently swallowed.
            controller.m_isGrounded = physics.IsGrounded(record.character);
            return;
        }

        // GAMEPLAY WROTE THE TRANSFORM: A RESPAWN --------------------------------- //
        if (!Same(transform.positions, record.position)) {
            physics.SetCharacterPosition(record.character, transform.positions);
            record.position            = transform.positions;
            controller.m_verticalSpeed = 0.0f;      // do not arrive carrying the last fall
        }

    }   // ECSPhysicsSystem::State::SyncCharacter()

    // ------------------------------------------------------------------- //
    // STEP: ONE TICK, AND THE RESULTS BACK OUT
    // ------------------------------------------------------------------- //

    void ECSPhysicsSystem::State::Step(World& ecs) {

        // Characters move BEFORE the world steps, so a crate the mech just shoved
        // reacts in this same step instead of looking a frame late.
        for (auto& [key, record] : characterRecords)
            if (record.seenFrame == frame) StepCharacter(ecs, record);

        physics.Step(kFixedDeltaTime);
        PullBodies(ecs);

    }   // ECSPhysicsSystem::State::Step()

    /*!
        \brief
        One step of walking: SimpleMove's velocity sideways, gravity and jumping
        up and down. This is the only place either of those is applied, because
        a character Jolt pulled down on its own could never be made to jump.
    */
    void ECSPhysicsSystem::State::StepCharacter(World& ecs, CharacterRecord& record) {

        CharacterController* controller = ecs.GetComponent<CharacterController>(record.entity);
        if (controller == nullptr || !record.character.IsValid()) return;

        const float gravity  = settings.m_gravity.m_Y;      // -9.81
        const bool  grounded = physics.IsGrounded(record.character);

        // UP AND DOWN ------------------------------------------------------------ //
        float vertical = controller->m_verticalSpeed;
        if (grounded && vertical < 0.0f) vertical = 0.0f;   // landed: stop falling

        // The launch speed that reaches exactly the requested height, from
        // v = sqrt(2 * g * h). A jump request lasts one step, then it is spent.
        if (grounded && controller->m_jumpHeight > 0.0f)
            vertical = std::sqrt(2.0f * std::fabs(gravity) * controller->m_jumpHeight);
        controller->m_jumpHeight = 0.0f;

        // Gravity applies even while standing: a little downward speed is what
        // keeps the feet pressed into the floor walking down a ramp.
        vertical += gravity * kFixedDeltaTime;

        // MOVE -------------------------------------------------------------------- //
        const Math::vec3 sideways = controller->m_moveVelocity;
        physics.MoveCharacter(record.character, Math::vec3(sideways.m_X, vertical, sideways.m_Z), kFixedDeltaTime);

        // Rising, but not as fast as asked: something is overhead, so stop
        // rising. Without this the mech keeps "climbing" on paper after a head
        // bump and hangs in the air before falling.
        const Math::vec3 achieved = physics.GetCharacterVelocity(record.character);
        if (vertical > 0.0f && achieved.m_Y < vertical) vertical = std::max(achieved.m_Y, 0.0f);

        // RESULTS ------------------------------------------------------------------ //
        controller->m_verticalSpeed = vertical;
        controller->m_isGrounded    = physics.IsGrounded(record.character);
        controller->m_velocity      = achieved;

        const Math::vec3 position = physics.GetCharacterPosition(record.character);
        if (Transform* transform = ecs.GetComponent<Transform>(record.entity)) transform->positions = position;
        record.position = position;

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

            if (Transform* transform = ecs.GetComponent<Transform>(record.entity)) {
                transform->positions = position;
                transform->rotation  = rotation;
            }
            record.position = position;
            record.rotation = rotation;

            if (RigidBody* rigidbody = ecs.GetComponent<RigidBody>(record.entity)) {
                rigidbody->velocity = physics.GetLinearVelocity(record.body);
                record.velocity     = rigidbody->velocity;
            }
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

        for (auto& [key, record] : records) Remove(record);
        records.clear();

        for (auto& [key, record] : characterRecords) physics.DestroyCharacter(record.character);
        characterRecords.clear();

    }   // ECSPhysicsSystem::State::RemoveAll()

}   // NAMESPACE CRUCIBLE
