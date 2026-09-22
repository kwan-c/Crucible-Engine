// STL
#include <cstdarg>   // JoltTrace()
#include <cstdio>    // JoltTrace()
#include <format>    // JoltAssertFailed()

// Jolt
#include "Jolt/Jolt.h"
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
// Engine
#include "PhysicsSystem.h"
#include "Diagnostics/Logger.h"

namespace {

    // Jolt only speaks when something is off, so this logs as a warning.
    void JoltTrace(const char* fmt, ...)
    {
        char buffer[1024];

        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

        CRU_WARN("[Jolt] {}", buffer);
    }

#ifdef JPH_ENABLE_ASSERTS
    bool JoltAssertFailed(const char* expression, const char* message, const char* file, JPH::uint line)
    {
        Crucible::Debug::LogMessage(Crucible::Debug::Level::Error, file, static_cast<int>(line),
            std::format("[Jolt] assert failed: {} {}", expression, message != nullptr ? message : ""));

        return true;    // true = break into the debugger
    }
#endif

}   // ANONYMOUS NAMESPACE

namespace Crucible {

	Physics::Physics() {
	
	}

	Physics::~Physics() {
	
	}

    // Builds the Jolt world: allocator, factory, types, job system, temp
        // allocator, layer tables, then the PhysicsSystem itself. Returns false
        // and logs if any of it fails; Core::Init does the same, which is why
        // this returns bool rather than throwing the way the renderer does.
        // The layer tables must outlive the world, so they cannot be locals here.
    bool Physics::Init(const PhysicsSettings& settings = PhysicsSettings{}){

		// Register the default allocator. Jolt needs this to manage memory for its internal structures.
		JPH::RegisterDefaultAllocator();

        // Trace and Assert callbacks
		JPH::Trace = JoltTrace;
		JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssertFailed;);

		// Create Factory instance. Jolt uses this to create instances of various classes.
		JPH::Factory::sInstance = new JPH::Factory();

		// Register all the types that Jolt knows about. 
        // If we have custom shapes, we need to register them with the CollisionDispatch Class.
        JPH::RegisterTypes();

        // Preallocates 10 MB for Physics system
		// The amount can be changed in the PhysicsSettings struct.
		JPH::TempAllocatorImpl tempAllocator(settings.m_tempAllocatorBytes);

		JPH::JobSystemThreadPool();


        

    }

    // Destroys every body, then tears Jolt down in the reverse order of Init.
    // Safe to call twice, and the destructor calls it for you.
    void Physics::Shutdown() {

    }

    // One physics tick. Call it from the fixed step, never from the frame
    // update, and always with the same dt (1/60). Inside, the work is spread
    // across worker threads and this returns only when all of it is finished.
    // Nothing else may touch bodies while it runs.
    void Physics::Step(float fixedDt) {

    }

    // --- bodies ---

    // Creates the shape and the body and adds it to the world. Returns an
    // invalid handle if the description is nonsense (a capsule with no radius)
    // or the world is full - check it, do not assume.
    BodyHandle Physics::CreateBody(const BodyDesc& desc) {

    }

    // Removes the body from the world and destroys it. Removing is not the
    // same as destroying in Jolt; this does both. Handles to it stay invalid
    // afterwards rather than becoming someone else's body.
    void Physics::DestroyBody(const BodyHandle& body) {

    }

    // --- moving things ---

    // Teleport. No velocity, so the body arrives with whatever it had before.
    // For something that should push what it hits while it moves - a lift, a
    // door, an animated platform - use MoveKinematic instead.
    void Physics::SetTransform(const BodyHandle& body, const Math::vec3& position, const Math::quaternion& rotation) {

    }

    // Reads a body's current transform. This is the simple path for the ECS
    // sync; see GetMovedBody below for the one that skips sleeping bodies.
    void Physics::GetTransform(const BodyHandle& body, Math::vec3& outPosition, Math::quaternion& outRotation) const {

    }

    // Moves a kinematic body toward a target over one step by giving it the
    // velocity that gets it there. That velocity is what lets it push dynamic
    // bodies; SetTransform would slide it straight through them.
    void Physics::MoveKinematic(const BodyHandle& body, const Math::vec3& targetPosition, const Math::quaternion& targetRotation, float fixedDt) {

    }

    void Physics::SetLinearVelocity(const BodyHandle& body, const Math::vec3& velocity) {

    }

    Math::vec3 Physics::GetLinearVelocity(const BodyHandle& body) const {

    }

    // A kick, measured in kg*m/s, applied instantly - the crate jumping when
    // it is shot. Use this rather than a force for one-off hits: a force is
    // spread over the step and a one-frame force barely moves anything.
    // Both of these wake a sleeping body; the equivalents on Jolt's Body do not.
    void Physics::ApplyImpulse(const BodyHandle& body, const Math::vec3& impulse) {

    }

    // --- queries ---

    // The weapon. Fires a ray and returns the closest thing it hits.
    // Pass the shooter's own body as 'ignore' or the mech shoots itself in
    // the foot on frame one. Sensors are not hit by this.
    // Direction is expected normalised; distance is in metres.
    RayHit Physics::Raycast(const Math::vec3& origin, const Math::vec3& direction, float maxDistance, const BodyHandle& ignore = BodyHandle{}) const {

    }
    // --- reading results back ---

    // After Step, these list only the bodies that actually moved - Jolt puts
    // resting bodies to sleep and skips them, so a scene with 500 crates that
    // have settled costs nothing to sync. Fill these in second: start the ECS
    // binding with GetTransform per body, and switch when the profiler says to.
    std::uint32_t Physics::GetMovedBodyCount() const {

    }

    bool Physics::GetMovedBody(std::uint32_t index, std::uint64_t& outUserData, Math::vec3& outPosition, Math::quaternion& outRotation) const {

    }

	

}