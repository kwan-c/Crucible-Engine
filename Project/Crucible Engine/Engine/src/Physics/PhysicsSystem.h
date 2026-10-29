/*************************************************************************
* \file:         PhysicsSystem.h
* \author:       Hugo Low Ren Hao (main author),
*                Tan Chun Kwang Xavier (v1.3: gravity, damping, axis locks)
* \par:          Email: low.h@digipen.edu,
*                Email: chunkwangxavier.tan@digipen.edu
* \date          22 September 2026
* \version       1.8
* \brief
*                Crucible::Physics: the public face of the Jolt physics
*                world. Bodies, characters, joints, raycasts and forces,
*                all through handles.
*
* \details
*                Knows nothing about the ECS: ECSPhysicsSystem turns
*                components into calls on this class. No Jolt header is
*                included, so Game never sees Jolt; everything Jolt lives
*                behind the Impl pointer (PhysicsInternal.h).
*
* \history
*    -v1.0       Jolt world setup: Init, Shutdown, Step
*    -v1.1       bodies, characters and raycasts behind handles and a
*                pimpl; clean at /W4
*    -v1.2       maths types moved to glm
*    -v1.3       per-body gravity, damping and axis locks (Xavier)
*    -v1.4       forces: AddForce, AddForceAtPosition, AddTorque,
*                ApplyImpulseAtPosition
*    -v1.5       joints: Fixed, Hinge, Ball, Character and Spring, with
*                limits and breaking
*    -v1.6       GetGroundVelocity, so a character rides what it stands on
*    -v1.7       contacts: sensors (BodyDesc::m_isSensor), contact events
*                after each Step, and ForEachContact
*    -v1.8       queries: QueryFilter (layer mask, triggers, ignore) on
*                Raycast, plus RaycastAll, SphereCast, CapsuleCast,
*                OverlapSphere, OverlapBox and OverlapCapsule
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>
#include <cstddef>
#include <functional>   // ForEachContact
#include <vector>       // RaycastAll, Overlap*

// Crucible engine
#include "EngineExport.h"
#include "Math/Math.h"
#include "Physics/PhysicsLayers.h"

// ---------------------------------------------------------------------------
// Crucible::Physics
//
// Jolt keeps its own world of shapes. The ECS keeps the game world. This class
// is the only thing that talks to both, and it deliberately knows nothing about
// entities - it deals in handles and a 64-bit user value that the caller fills
// with whatever it likes (the ECS puts an Entity in there).
//
// It has two doors:
//
//   the fixed step          the rest of the engine
//   ---------------         ----------------------
//   Step(1/60)              Raycast(), ApplyImpulse(), CreateBody()...
//
// and the traffic is one way at each moment:
//
//   before Step   ECS -> here   spawns, teleports, velocities
//   Step          the world moves, spread over worker threads, returns when done
//   after Step    here -> ECS   transforms of the bodies that actually moved
//
// Between those two moments, physics owns the transform of every dynamic body.
// Gameplay writing a Transform component mid-step gets silently overwritten -
// the same rule as Unity's "don't set transform.position on a Rigidbody".
//
// Three kinds of body: Static (the arena), Dynamic (crates), and Kinematic (a
// lift or a door: you drive it, nothing pushes it back). M1 uses the first two;
// the third is here for when the arena gets moving parts. Sensors are cut until
// the pickups and trigger volumes that need contact events exist.
//
// No Jolt header is included here on purpose. Engine is a DLL and Jolt is a
// static library, so if Game could see Jolt it would link a second copy with
// its own uninitialised globals. Everything Jolt lives behind Impl, in the .cpp.
// ---------------------------------------------------------------------------

namespace Crucible {

    // ------------------------------------------------------------------- //
    // HANDLES AND DESCRIPTIONS
    // ------------------------------------------------------------------- //

    /*
        A body lives inside the physics world, not in your arrays, so you never
        hold a pointer to one - you hold this. The id carries an index and a
        sequence number, so a handle to a destroyed body fails its lookup
        instead of quietly pointing at whoever took the slot.
        Zero is a legal body index, so the invalid value is 0xFFFFFFFF.
    */
    struct BodyHandle {

        std::uint32_t m_id = 0xFFFFFFFFu;

        bool IsValid() const { return m_id != 0xFFFFFFFFu; }

    };  // BODYHANDLE STRUCT

    /*
        The same idea as BodyHandle, for character controllers. Index 0 is never
        handed out, so a default-constructed handle means "no character". The
        generation rises when a slot is reused, so an old handle stops matching
        rather than steering somebody else's mech.
    */
    struct CharacterHandle {

        std::uint32_t m_index      = 0;
        std::uint32_t m_generation = 0;

        bool IsValid() const { return m_index != 0; }

    };  // CHARACTERHANDLE STRUCT

    enum class BodyMotion : std::uint8_t {

        Static,     // never moves. The arena. The cheap one: Jolt never simulates it
        Kinematic,  // you move it; it shoves dynamic bodies, nothing shoves it. A lift, a door
        Dynamic     // forces and collisions move it. A crate

    };  // BODYMOTION ENUM

    enum class ShapeType : std::uint8_t {

        Box,
        Sphere,
        Capsule     // the character shape: a cylinder with rounded ends, so it does not snag on steps

    };  // SHAPETYPE ENUM

    struct ShapeDesc {

        ShapeType  m_type        = ShapeType::Box;
        Math::vec3 m_halfExtents = Math::vec3(0.5f, 0.5f, 0.5f);   // Box:     HALF the size, so a 1m cube is 0.5
        float      m_radius      = 0.5f;                           // Sphere, Capsule
        float      m_halfHeight  = 0.5f;                           // Capsule: half the straight middle; the caps are extra
        Math::vec3 m_center      = Math::vec3(0.0f, 0.0f, 0.0f);   // Unity's Collider.center: the shape's offset from the body's origin

    };  // SHAPEDESC STRUCT

    /*
        Bits for BodyDesc::m_lockedAxes (Unity's RigidbodyConstraints). A locked
        axis is one the simulation may not move the body along / turn it around,
        in WORLD space. Same bit order as Jolt's EAllowedDOFs, so the axes Jolt
        is allowed to use are just the ones not locked.
    */
    namespace AxisLock {
        inline constexpr std::uint8_t None      = 0;
        inline constexpr std::uint8_t PositionX = 1u << 0;
        inline constexpr std::uint8_t PositionY = 1u << 1;
        inline constexpr std::uint8_t PositionZ = 1u << 2;
        inline constexpr std::uint8_t RotationX = 1u << 3;
        inline constexpr std::uint8_t RotationY = 1u << 4;
        inline constexpr std::uint8_t RotationZ = 1u << 5;
        inline constexpr std::uint8_t Position  = PositionX | PositionY | PositionZ;
        inline constexpr std::uint8_t Rotation  = RotationX | RotationY | RotationZ;
        inline constexpr std::uint8_t All       = Position | Rotation;
    }   // NAMESPACE AXISLOCK

    struct BodyDesc {

        ShapeDesc     m_shape       {};
        Math::vec3    m_position    = Math::vec3(0.0f, 0.0f, 0.0f);
        Math::quat    m_rotation    = Math::quat(1.0f, 0.0f, 0.0f, 0.0f);
        BodyMotion    m_motion      = BodyMotion::Dynamic;
        PhysicsLayer  m_layer       = PhysicsLayer::Moving;
        float         m_mass        = 0.0f;    // 0 = derive it from the shape, which is almost always what you want
        float         m_friction    = 0.2f;    // Jolt's defaults. 0 is ice, 1 is glue
        float         m_restitution = 0.0f;    // bounciness. 0 is a sandbag, 1 is a superball
        float         m_gravityFactor  = 1.0f; // multiplies world gravity for this body. 0 = it floats
        float         m_linearDamping  = 0.0f; // Unity's drag: speed lost per second. 0 matches Unity's default
        float         m_angularDamping = 0.05f;// Unity's angularDrag: spin lost per second (Unity and Jolt both default to 0.05)
        std::uint8_t  m_lockedAxes     = AxisLock::None; // AxisLock bits. Dynamic bodies only; locking ALL of them is refused (make it Static)
        bool          m_isSensor    = false;   // Unity's isTrigger: reports what enters and leaves, blocks nothing
        std::uint64_t m_userData    = 0;       // yours. The ECS binding puts the Entity in here

    };  // BODYDESC STRUCT

    // Two bodies started or stopped touching during the last Step.
    // Unity's OnCollisionEnter/Exit and OnTriggerEnter/Exit, as a list (GetContactEvent).
    enum class ContactEventType : std::uint8_t { Began, Ended };

    struct ContactEvent {

        ContactEventType m_type        = ContactEventType::Began;
        BodyHandle       m_body1       {};
        BodyHandle       m_body2       {};
        std::uint64_t    m_userData1   = 0;
        std::uint64_t    m_userData2   = 0;
        bool             m_isSensor1   = false;     // either one a sensor: a trigger event, not a collision
        bool             m_isSensor2   = false;
        Math::vec3       m_point       = Math::vec3(0.0f, 0.0f, 0.0f);   // Began only: where they touched first
        Math::vec3       m_normal      = Math::vec3(0.0f, 1.0f, 0.0f);   // Began only: from body 1 toward body 2
        float            m_impactSpeed = 0.0f;      // Began only: how fast they closed in along the normal, m/s

    };  // CONTACTEVENT STRUCT

    // One body that a given body is touching right now (ForEachContact), with
    // the latest contact points - what Unity's Collision.contacts holds.
    struct BodyContact {

        static constexpr std::uint32_t kMaxPoints = 4;

        BodyHandle    m_other         {};
        std::uint64_t m_otherUserData = 0;
        bool          m_otherIsSensor = false;
        bool          m_selfIsSensor  = false;
        Math::vec3    m_normal        = Math::vec3(0.0f, 1.0f, 0.0f);   // from the body asked about toward the other
        Math::vec3    m_points[kMaxPoints] {};
        std::uint32_t m_pointCount    = 0;
        bool          m_resting       = false;   // asleep: still touching, the points are from before it slept

    };  // BODYCONTACT STRUCT

    // Unity's RaycastHit. Only filled in when Raycast returns true.
    struct RaycastHit {

        BodyHandle    m_body     {};
        std::uint64_t m_userData = 0;                            // copied off the body, so the caller need not look it up
        Math::vec3    m_point    = Math::vec3(0.0f, 0.0f, 0.0f);
        Math::vec3    m_normal   = Math::vec3(0.0f, 1.0f, 0.0f);   // which way the surface faces where it was hit
        float         m_distance = 0.0f;

    };  // RAYCASTHIT STRUCT

    // What every query may hit: Unity's layerMask and QueryTriggerInteraction,
    // plus the one body to see through (a shooter's own, or a mech's hidden body).
    struct QueryFilter {

        LayerMask  m_layers      = LayerMask::All();
        bool       m_hitTriggers = false;   // false: sensors are invisible to it - a shot is never stopped by a pickup zone
        BodyHandle m_ignore      {};

    };  // QUERYFILTER STRUCT

    // One body an Overlap query found.
    struct OverlapHit {

        BodyHandle    m_body     {};
        std::uint64_t m_userData = 0;

    };  // OVERLAPHIT STRUCT

    /*
        Unity's CharacterController, as a description. A capsule that walks: it
        slides along walls, climbs steps and shoves crates, and it only moves
        when MoveCharacter is called. Nothing in the simulation pushes it back.
    */
    struct CharacterDesc {

        Math::vec3    m_position     = Math::vec3(0.0f, 0.0f, 0.0f);
        float         m_height       = 2.0f;        // the whole capsule, rounded ends included
        float         m_radius       = 0.5f;
        Math::vec3    m_center       = Math::vec3(0.0f, 0.0f, 0.0f);   // capsule centre relative to m_position

        float         m_slopeLimit   = 45.0f;       // degrees; steeper than this and it slides back down
        float         m_stepOffset   = 0.3f;        // metres it steps up without jumping
        float         m_pushStrength = 100.0f;      // newtons it can shove crates with. A mech wants much more
        PhysicsLayer  m_layer        = PhysicsLayer::Player;
        std::uint64_t m_userData     = 0;           // yours, same as BodyDesc

    };  // CHARACTERDESC STRUCT

    /*
        The same idea as CharacterHandle, for joints. Index 0 is never handed
        out, so a default-constructed handle means "no joint", and the
        generation rises when a slot is reused.
    */
    struct JointHandle {

        std::uint32_t m_index      = 0;
        std::uint32_t m_generation = 0;

        bool IsValid() const { return m_index != 0; }

    };  // JOINTHANDLE STRUCT

    /*
        The kinds of joint, named after Unity's where Unity has one. Jolt's
        name for each is in brackets - that is what PhysicsJoints.cpp builds.
    */
    enum class JointType : std::uint8_t {

        Fixed,      // glued: neither moves nor turns relative to the other (Jolt: Fixed)
        Hinge,      // turns around one axis only, like a door or a knee (Jolt: Hinge)
        Ball,       // turns any way around one point, no limits: ball and socket (Jolt: Point)
        Character,  // Unity's CharacterJoint: twist plus a limited swing cone - shoulders, hips (Jolt: SwingTwist)
        Spring      // Unity's SpringJoint: keeps a distance, springily (Jolt: Distance)

    };  // JOINTTYPE ENUM

    /*
        Everything CreateJoint needs, in WORLD space and as things are right
        now: the joint's rest pose is the pose of the two bodies at the moment
        it is created, and every angle limit is measured from there.

        Which fields matter depends on m_type, the same as ShapeDesc.
    */
    struct JointDesc {

        JointType     m_type      = JointType::Hinge;
        BodyHandle    m_bodyA     {};       // must be a body. The one the joint "belongs" to
        BodyHandle    m_bodyB     {};       // the other body. Invalid = joined to the world, a fixed point in space

        Math::vec3    m_anchor    = Math::vec3(0.0f, 0.0f, 0.0f);   // where the two are joined
        Math::vec3    m_anchorB   = Math::vec3(0.0f, 0.0f, 0.0f);   // Spring only: the other end of the spring
        Math::vec3    m_axis      = Math::vec3(1.0f, 0.0f, 0.0f);   // Hinge: what it turns around. Character: the twist axis, along the limb
        Math::vec3    m_swingAxis = Math::vec3(0.0f, 1.0f, 0.0f);   // Character: swing1 turns around this. Made perpendicular to m_axis for you

        bool          m_collideConnected = false;   // Unity's enableCollision. Off: the two bodies pass through each other

        // Hinge, in degrees. Min must be 0 or less and max 0 or more: the rest pose is always allowed.
        // Positive = body A turning the right-hand-rule way around m_axis (thumb along the axis,
        // fingers curl the positive way), relative to body B. Character twist limits read the same way.
        bool          m_useLimits = false;
        float         m_minAngle  = -90.0f;
        float         m_maxAngle  =  90.0f;

        // Character, in degrees. Twist turns around m_axis; swing1 around
        // m_swingAxis; swing2 around the axis at right angles to both.
        float         m_twistMin  = -20.0f;
        float         m_twistMax  =  20.0f;
        float         m_swing1    =  40.0f;
        float         m_swing2    =  40.0f;

        // Spring. Distances in metres between the two ends; 0 or less means
        // "as far apart as they are now". Stiffness 0 makes a rigid rod.
        float         m_minDistance = 0.0f;
        float         m_maxDistance = 0.0f;
        float         m_spring      = 10.0f;   // stiffness, N/m (Unity's spring)
        float         m_damper      = 0.2f;    // N*s/m (Unity's damper)

        // Breaking: the joint is destroyed in the step where it has to hold
        // more than this. 0 = never breaks (Unity's Infinity).
        float         m_breakForce  = 0.0f;    // newtons
        float         m_breakTorque = 0.0f;    // newton-metres

        // Unity's two solver knobs, per joint. 0 = Jolt's default for each. More is
        // stiffer and slower, and every joint touching the same bodies pays for the
        // highest count among them.
        //   m_solverIterations (position passes, default 2): raise it when a joint
        //     RESTS slightly wrong under weight - a ragdoll's limbs a few cm apart,
        //     a knee lying a little past its limit. 8 suits a ragdoll.
        //   m_solverVelocityIterations (velocity passes, default 10): raise it when
        //     a light body on a long lever pushes visibly past a limit as it swings.
        std::uint32_t m_solverIterations         = 0;
        std::uint32_t m_solverVelocityIterations = 0;

        std::uint64_t m_userData = 0;          // yours, handed back when it breaks. The ECS puts the Entity in here

    };  // JOINTDESC STRUCT

    struct PhysicsSettings {

        // Fixed at Init and never grown. When one is exceeded Jolt drops contacts
        // instead of resizing, which looks like objects sinking through the floor,
        // so Step() reports it and the log is where you will find out.
        std::uint32_t m_maxBodies          = 10240;
        std::uint32_t m_maxBodyPairs       = 10240;
        std::uint32_t m_maxContacts        = 10240;

        std::uint32_t m_workerThreads      = 0;                  // 0 = pick automatically (hardware threads minus one, capped - see Init)
        std::size_t   m_tempAllocatorBytes = 10u * 1024 * 1024;  // scratch memory for one step; freed by the time Step returns
        Math::vec3    m_gravity            = Math::vec3(0.0f, -9.81f, 0.0f);

    };  // PHYSICSSETTINGS STRUCT

    // ------------------------------------------------------------------- //
    // THE SYSTEM
    // ------------------------------------------------------------------- //

    class ENGINE_API Physics {

    public:

        Physics();
        ~Physics();

        Physics(const Physics&)            = delete;   // one owner. Copying would mean two systems sharing one Jolt world
        Physics& operator=(const Physics&) = delete;

        // --- lifecycle ---

        // Builds the Jolt world: allocator, factory, types, job system, temp
        // allocator, layer tables, then the PhysicsSystem itself. Returns false
        // and logs if any of it fails; Core::Init does the same, which is why
        // this returns bool rather than throwing the way the renderer does.
        // The layer tables must outlive the world, so they cannot be locals here.
        bool Init(const PhysicsSettings& settings = PhysicsSettings{});

        // Destroys every body, then tears Jolt down in the reverse order of Init.
        // Safe to call twice, and the destructor calls it for you.
        void Shutdown();

        // Did Init succeed? Here because the ECS binding asks every frame before
        // it touches anything, and the answer belongs with the object rather
        // than in a flag elsewhere that someone has to reset when physics
        // restarts. Everything below is a safe no-op while this is false.
        bool IsInitialised() const;

        // Rebuilds the search tree the arena lives in. Adding a batch of static
        // bodies leaves that tree lopsided, and every raycast and pair test pays
        // for it until this runs. Costly, so call it after building a level, not
        // every frame. The ECS binding calls it for you when static bodies appear.
        void OptimizeBroadPhase();

        // One physics tick. Call it from the fixed step, never from the frame
        // update, and always with the same dt (1/60). Inside, the work is spread
        // across worker threads and this returns only when all of it is finished.
        // Nothing else may touch bodies while it runs.
        void Step(float fixedDt);

        // --- bodies ---

        // Creates the shape and the body and adds it to the world. Returns an
        // invalid handle if the description is nonsense (a capsule with no radius)
        // or the world is full - check it, do not assume.
        BodyHandle CreateBody(const BodyDesc& desc);

        // Removes the body from the world and destroys it. Removing is not the
        // same as destroying in Jolt; this does both. Handles to it stay invalid
        // afterwards rather than becoming someone else's body.
        void DestroyBody(const BodyHandle& body);

        // --- moving things ---

        // Teleport. No velocity, so the body arrives with whatever it had before.
        // For something that should push what it hits while it moves - a lift, a
        // door, an animated platform - use MoveKinematic instead.
        void SetTransform(const BodyHandle& body, const Math::vec3& position, const Math::quat& rotation);

        // Reads a body's current transform. This is the simple path for the ECS
        // sync; see GetMovedBody below for the one that skips sleeping bodies.
        void GetTransform(const BodyHandle& body, Math::vec3& outPosition, Math::quat& outRotation) const;

        // Moves a kinematic body toward a target over one step by giving it the
        // velocity that gets it there. That velocity is what lets it push dynamic
        // bodies; SetTransform would slide it straight through them.
        void MoveKinematic(const BodyHandle& body, const Math::vec3& targetPosition, const Math::quat& targetRotation, float fixedDt);

        void       SetLinearVelocity(const BodyHandle& body, const Math::vec3& velocity);
        Math::vec3 GetLinearVelocity(const BodyHandle& body) const;

        // A kick, measured in kg*m/s, applied instantly - the crate jumping when
        // it is shot. Use this rather than a force for one-off hits: a force is
        // spread over the step and a one-frame force barely moves anything.
        // This wakes a sleeping body; the equivalent on Jolt's Body does not.
        void ApplyImpulse(const BodyHandle& body, const Math::vec3& impulse);

        // A push that lasts ONE step, in newtons - a thruster, wind, water
        // lifting a crate. Jolt clears it after every Step, so to keep pushing,
        // call it again before every Step. Unity's AddForce (ForceMode.Force).
        // Wakes a sleeping body; ignored on static and kinematic bodies.
        void AddForce(const BodyHandle& body, const Math::vec3& force);

        // The same push at a world-space point instead of the centre of mass,
        // so it turns the body as well: push a door near its edge and it swings.
        // Unity's AddForceAtPosition.
        void AddForceAtPosition(const BodyHandle& body, const Math::vec3& force, const Math::vec3& worldPoint);

        // A twist that lasts one step, in newton-metres. The vector's direction
        // is the axis it turns around (right-hand rule), its length is how hard.
        // Same one-step rule as AddForce. Unity's AddTorque.
        void AddTorque(const BodyHandle& body, const Math::vec3& torque);

        // ApplyImpulse at a world-space point, so the kick spins as well as
        // shoves. A shot on a ragdoll's shin should kick the leg, not slide the
        // whole body. RaycastHit::m_point goes straight in.
        void ApplyImpulseAtPosition(const BodyHandle& body, const Math::vec3& impulse, const Math::vec3& worldPoint);

        // --- characters (Unity's CharacterController) ---

        // Builds the capsule and puts it in the world. Invalid handle, with the
        // reason logged, if the description makes no sense.
        CharacterHandle CreateCharacter(const CharacterDesc& desc);
        void            DestroyCharacter(const CharacterHandle& character);

        // One step of walking. `velocity` is the whole thing, including up and
        // down: nothing adds gravity for you, because a character that fell on
        // its own could not be made to jump. Slides along walls, climbs steps up
        // to the step offset, and stays stuck to the floor walking down slopes.
        void MoveCharacter(const CharacterHandle& character, const Math::vec3& velocity, float fixedDt);

        // Standing on something walkable. Sliding down a too-steep slope is not
        // grounded, which is what stops a jump halfway up a ramp.
        bool IsGrounded(const CharacterHandle& character) const;

        // What the last MoveCharacter actually achieved, after walls and steps
        // had their say. Ask for this rather than assuming the velocity you gave.
        Math::vec3 GetCharacterVelocity(const CharacterHandle& character) const;

        // How fast the ground is moving right under the character's feet: a lift's
        // speed, or the edge of a spinning platform's. Zero in the air and on static
        // ground. Add it to the walk, and the character rides what it stands on.
        // It needs the ground to HAVE a velocity - a kinematic body moved with
        // MoveKinematic, not a static one moved with SetTransform.
        // Not const: Jolt re-reads the ground body's velocity as it is now, since
        // the character's last move may be a step out of date.
        Math::vec3 GetGroundVelocity(const CharacterHandle& character);

        // The hidden body that makes a character visible to raycasts. Pass this
        // as Raycast's `ignore`, or the mech shoots itself: a ray starting inside
        // a shape hits that shape at distance zero.
        BodyHandle GetCharacterBody(const CharacterHandle& character) const;

        Math::vec3 GetCharacterPosition(const CharacterHandle& character) const;
        void       SetCharacterPosition(const CharacterHandle& character, const Math::vec3& position);   // a respawn

        // --- joints (Unity's HingeJoint, FixedJoint, CharacterJoint, SpringJoint) --- //

        // Joins two bodies - or one body to the world - as they are posed right
        // now. Invalid handle, with the reason logged, if the description makes
        // no sense: a missing body, a body joined to itself, a zero axis.
        JointHandle CreateJoint(const JointDesc& desc);

        // Takes the joint out of the world and wakes both bodies, so a crate
        // hanging from it falls straight away instead of when something nudges it.
        // Destroying a body destroys every joint on it, so handles to those go stale.
        void DestroyJoint(const JointHandle& joint);

        // Unity's HingeJoint.angle: degrees turned from the rest pose, around the
        // axis. 0 for anything that is not a live hinge.
        float GetHingeAngle(const JointHandle& joint) const;

        // After Step: the joints that broke in it (m_breakForce / m_breakTorque),
        // already destroyed. Unity's OnJointBreak, as a list - the same pattern as
        // GetMovedBody. The user value is the one the JointDesc carried.
        std::uint32_t GetBrokenJointCount() const;
        bool          GetBrokenJoint(std::uint32_t index, JointHandle& outJoint, std::uint64_t& outUserData) const;

        // --- queries (Unity's Physics.Raycast, SphereCast, OverlapSphere...) ---
        //
        // Argument order matches Unity's on purpose, so this is the one place
        // out-parameters are not last. Directions need not be normalised - that
        // is done for you, the way Unity does it. Distances are in metres. Each
        // takes a QueryFilter: which layers, whether triggers count, and one body
        // to see through. A query that STARTS inside a shape hits it at distance
        // 0 - pass the shooter's own body as m_ignore, or the mech shoots itself
        // in the foot on frame one.
        // Between steps, on the main thread (where all gameplay runs).

        // The weapon. True if the ray hit something, with the closest hit in
        // outHit; false leaves outHit untouched.
        bool Raycast(const Math::vec3& origin, const Math::vec3& direction, RaycastHit& outHit, float maxDistance,
                     const QueryFilter& filter = QueryFilter{}) const;

        // Everything along the ray, nearest first - a piercing shot. Fills `out`
        // (cleared first; reuse the vector and it never reallocates).
        void RaycastAll(const Math::vec3& origin, const Math::vec3& direction, float maxDistance, std::vector<RaycastHit>& out,
                        const QueryFilter& filter = QueryFilter{}) const;

        // A ball of `radius` swept along a line: the first thing it would hit. A
        // thick shot, a melee swing, a camera that must not poke into walls.
        bool SphereCast(const Math::vec3& origin, float radius, const Math::vec3& direction, RaycastHit& outHit, float maxDistance,
                        const QueryFilter& filter = QueryFilter{}) const;

        // A capsule swept along a line. point1 and point2 are the centres of its
        // two rounded ends, as in Unity - "would a mech this size get through?".
        bool CapsuleCast(const Math::vec3& point1, const Math::vec3& point2, float radius, const Math::vec3& direction,
                         RaycastHit& outHit, float maxDistance, const QueryFilter& filter = QueryFilter{}) const;

        // Everything inside a shape right now, each body once. Explosions, area
        // damage, "what is around me". Fill `out`, cleared first.
        void OverlapSphere(const Math::vec3& center, float radius, std::vector<OverlapHit>& out,
                           const QueryFilter& filter = QueryFilter{}) const;
        void OverlapBox(const Math::vec3& center, const Math::vec3& halfExtents, const Math::quat& orientation, std::vector<OverlapHit>& out,
                        const QueryFilter& filter = QueryFilter{}) const;
        void OverlapCapsule(const Math::vec3& point0, const Math::vec3& point1, float radius, std::vector<OverlapHit>& out,
                            const QueryFilter& filter = QueryFilter{}) const;

        // --- reading results back ---

        // After Step, these list only the bodies that actually moved - Jolt puts
        // resting bodies to sleep and skips them, so a scene with 500 crates that
        // have settled costs nothing to sync. Fill these in second: start the ECS
        // binding with GetTransform per body, and switch when the profiler says to.
        std::uint32_t GetMovedBodyCount() const;
        bool          GetMovedBody(std::uint32_t index, std::uint64_t& outUserData, Math::vec3& outPosition, Math::quat& outRotation) const;

        // --- contacts (Unity's collision and trigger messages) ---

        // After Step: the pairs that started or stopped touching in it, the same
        // list pattern as GetMovedBody. A body going to SLEEP is not an Ended:
        // a settled crate is still lying on the floor. Ended does come for a
        // destroyed body, one step after it is destroyed.
        std::uint32_t GetContactEventCount() const;
        bool          GetContactEvent(std::uint32_t index, ContactEvent& outEvent) const;

        // Everything a body is touching right now, sleepers included - for
        // "who is inside this trigger" and Unity's OnCollisionStay contacts.
        // Visits nothing for a stale handle. Costs one pass over the touching
        // pairs, and only when called.
        void ForEachContact(const BodyHandle& body, const std::function<void(const BodyContact&)>& visit) const;

    private:

        // Everything Jolt hides in here, defined in PhysicsInternal.h. Two reasons:
        // Game must never see a Jolt header (see the note at the top of this file),
        // and an exported class with std:: members warns C4251 at /W4 - the same
        // trap ScopedTimer dodges. A raw pointer rather than a unique_ptr for the
        // same reason; the destructor deletes it.
        struct Impl;
        Impl* m_impl = nullptr;

    };  // PHYSICS CLASS

}   // NAMESPACE CRUCIBLE
