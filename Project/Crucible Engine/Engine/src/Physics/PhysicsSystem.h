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
//   before Step   ECS -> here   spawns, teleports, velocities, kinematic targets
//   Step          the world moves, spread over worker threads, returns when done
//   after Step    here -> ECS   transforms of the bodies that actually moved
//
// Between those two moments, physics owns the transform of every dynamic body.
// Gameplay writing a Transform component mid-step gets silently overwritten -
// the same rule as Unity's "don't set transform.position on a Rigidbody".
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

    enum class BodyMotion : std::uint8_t {

        Static,     // never moves. The arena. Cheapest of the three
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

    };  // SHAPEDESC STRUCT

    struct BodyDesc {

        ShapeDesc        m_shape       {};
        Math::vec3       m_position    = Math::vec3(0.0f, 0.0f, 0.0f);
        Math::quaternion m_rotation    = Math::quaternion::fromIdentity();
        BodyMotion       m_motion      = BodyMotion::Dynamic;
        Layer            m_layer       = Layer::Moving;
        float            m_mass        = 0.0f;    // 0 = derive it from the shape, which is almost always what you want
        float            m_friction    = 0.2f;    // Jolt's defaults. 0 is ice, 1 is glue
        float            m_restitution = 0.0f;    // bounciness. 0 is a sandbag, 1 is a superball
        bool             m_isSensor    = false;   // a trigger: reports touches, blocks nothing
        std::uint64_t    m_userData    = 0;       // yours. The ECS binding puts the Entity in here

    };  // BODYDESC STRUCT

    struct RayHit {

        bool          m_hit      = false;                        // check this first; the rest is undefined when false
        BodyHandle    m_body     {};
        std::uint64_t m_userData = 0;                            // copied off the body, so the caller need not look it up
        Math::vec3    m_point    = Math::vec3(0.0f, 0.0f, 0.0f);
        Math::vec3    m_normal   = Math::vec3(0.0f, 1.0f, 0.0f);
        float         m_distance = 0.0f;

    };  // RAYHIT STRUCT

    struct PhysicsSettings {

        // Fixed at Init and never grown. When one is exceeded Jolt drops contacts
        // instead of resizing, which looks like objects sinking through the floor,
        // so Step() reports it and the log is where you will find out.
        std::uint32_t m_maxBodies          = 10240;
        std::uint32_t m_maxBodyPairs       = 10240;
        std::uint32_t m_maxContacts        = 10240;

        std::uint32_t m_workerThreads      = 0;                  // 0 = hardware threads minus one, because the calling thread joins in
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
        void SetTransform(const BodyHandle& body, const Math::vec3& position, const Math::quaternion& rotation);

        // Reads a body's current transform. This is the simple path for the ECS
        // sync; see GetMovedBody below for the one that skips sleeping bodies.
        void GetTransform(const BodyHandle& body, Math::vec3& outPosition, Math::quaternion& outRotation) const;

        // Moves a kinematic body toward a target over one step by giving it the
        // velocity that gets it there. That velocity is what lets it push dynamic
        // bodies; SetTransform would slide it straight through them.
        void MoveKinematic(const BodyHandle& body, const Math::vec3& targetPosition, const Math::quaternion& targetRotation, float fixedDt);

        void       SetLinearVelocity(const BodyHandle& body, const Math::vec3& velocity);
        Math::vec3 GetLinearVelocity(const BodyHandle& body) const;

        // A kick, measured in kg*m/s, applied instantly - the crate jumping when
        // it is shot. Use this rather than a force for one-off hits: a force is
        // spread over the step and a one-frame force barely moves anything.
        // Both of these wake a sleeping body; the equivalents on Jolt's Body do not.
        void ApplyImpulse(const BodyHandle& body, const Math::vec3& impulse);

        // --- queries ---

        // The weapon. Fires a ray and returns the closest thing it hits.
        // Pass the shooter's own body as 'ignore' or the mech shoots itself in
        // the foot on frame one. Sensors are not hit by this.
        // Direction is expected normalised; distance is in metres.
        RayHit Raycast(const Math::vec3& origin, const Math::vec3& direction, float maxDistance, const BodyHandle& ignore = BodyHandle{}) const;

        // --- reading results back ---

        // After Step, these list only the bodies that actually moved - Jolt puts
        // resting bodies to sleep and skips them, so a scene with 500 crates that
        // have settled costs nothing to sync. Fill these in second: start the ECS
        // binding with GetTransform per body, and switch when the profiler says to.
        std::uint32_t GetMovedBodyCount() const;
        bool          GetMovedBody(std::uint32_t index, std::uint64_t& outUserData, Math::vec3& outPosition, Math::quaternion& outRotation) const;

    private:

        // Everything Jolt hides in here, defined in the .cpp. Two reasons: Game
        // must never see a Jolt header (see the note at the top of this file),
        // and an exported class with std:: members warns C4251 at /W4 - the same
        // trap ScopedTimer dodges. A raw pointer rather than a unique_ptr for the
        // same reason; the destructor deletes it.
        struct Impl;
        Impl* m_impl = nullptr;

    };  // PHYSICS CLASS

}   // NAMESPACE CRUCIBLE
