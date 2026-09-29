#pragma once

// STL
#include <cstdint>
#include <cstddef>

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

    struct BodyDesc {

        ShapeDesc     m_shape       {};
        Math::vec3    m_position    = Math::vec3(0.0f, 0.0f, 0.0f);
        Math::quat    m_rotation    = Math::quat::fromIdentity();
        BodyMotion    m_motion      = BodyMotion::Dynamic;
        PhysicsLayer  m_layer       = PhysicsLayer::Moving;
        float         m_mass        = 0.0f;    // 0 = derive it from the shape, which is almost always what you want
        float         m_friction    = 0.2f;    // Jolt's defaults. 0 is ice, 1 is glue
        float         m_restitution = 0.0f;    // bounciness. 0 is a sandbag, 1 is a superball
        std::uint64_t m_userData    = 0;       // yours. The ECS binding puts the Entity in here

    };  // BODYDESC STRUCT

    // Unity's RaycastHit. Only filled in when Raycast returns true.
    struct RaycastHit {

        BodyHandle    m_body     {};
        std::uint64_t m_userData = 0;                            // copied off the body, so the caller need not look it up
        Math::vec3    m_point    = Math::vec3(0.0f, 0.0f, 0.0f);
        Math::vec3    m_normal   = Math::vec3(0.0f, 1.0f, 0.0f);   // which way the surface faces where it was hit
        float         m_distance = 0.0f;

    };  // RAYCASTHIT STRUCT

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

        // The hidden body that makes a character visible to raycasts. Pass this
        // as Raycast's `ignore`, or the mech shoots itself: a ray starting inside
        // a shape hits that shape at distance zero.
        BodyHandle GetCharacterBody(const CharacterHandle& character) const;

        Math::vec3 GetCharacterPosition(const CharacterHandle& character) const;
        void       SetCharacterPosition(const CharacterHandle& character, const Math::vec3& position);   // a respawn

        // --- queries ---

        // The weapon. True if the ray hit something, with the closest hit in
        // outHit; false leaves outHit untouched. Argument order matches Unity's
        // Physics.Raycast(origin, direction, out hit, maxDistance) on purpose,
        // so this is the one place out-parameters are not last.
        // Pass the shooter's own body as 'ignore' or the mech shoots itself in
        // the foot on frame one.
        // The direction need not be normalised - it is normalised for you, the
        // way Unity does it. Distance is in metres.
        bool Raycast(const Math::vec3& origin, const Math::vec3& direction, RaycastHit& outHit, float maxDistance,
                     const BodyHandle& ignore = BodyHandle{}) const;

        // --- reading results back ---

        // After Step, these list only the bodies that actually moved - Jolt puts
        // resting bodies to sleep and skips them, so a scene with 500 crates that
        // have settled costs nothing to sync. Fill these in second: start the ECS
        // binding with GetTransform per body, and switch when the profiler says to.
        std::uint32_t GetMovedBodyCount() const;
        bool          GetMovedBody(std::uint32_t index, std::uint64_t& outUserData, Math::vec3& outPosition, Math::quat& outRotation) const;

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
