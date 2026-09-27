#include "PhysicsSystem.h"

// Jolt - Jolt.h always comes before any other Jolt header
#pragma warning(push)
#pragma warning(disable : 26495)   // uninitialised member inside Jolt's own headers; not our code to fix
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#pragma warning(pop)

// STL
#include <algorithm>    // Raycast()

// Crucible engine
#include "Physics/JoltConversions.h"
#include "Physics/PhysicsInternal.h"

namespace {

    // Jolt needs a ray of finite length, so "as far as it goes" becomes 100 km.
    // Nothing in a level is further away than that, and the number keeps the
    // maths well behaved.
    constexpr float kMaxRayDistance = 100000.0f;

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    using namespace JoltConvert;

    /*!
        \brief
        The closest body along a ray. Unity's Physics.Raycast.
        \param origin, direction
        Where the ray starts and which way it points. The direction is
        normalised here, so an un-normalised one still works.
        \param outHit
        Filled in only when this returns true, and left alone otherwise.
        \param maxDistance
        How far to look, in metres.
        \param ignore
        One body the ray passes straight through - usually the shooter.
        \return
        True if the ray hit something.
    */
    bool Physics::Raycast(const Math::vec3& origin, const Math::vec3& direction, RaycastHit& outHit, float maxDistance,
                          const BodyHandle& ignore) const {

        const Impl& impl = *m_impl;
        if (!impl.m_initialised || !IsFinite(origin) || !IsFinite(direction)) return false;

        // THE RAY ------------------------------------------------------------- //
        // Jolt describes a ray as a start point plus the whole path to walk, so
        // the direction has to carry the length rather than just the heading.
        const JPH::Vec3 heading = ToJolt(direction);
        const float     length  = heading.Length();
        if (!(length > 1.0e-6f) || !(maxDistance > 0.0f)) return false;      // a zero-length ray hits nothing

        const float         distance = std::min(maxDistance, kMaxRayDistance);
        const JPH::RRayCast ray(ToJolt(origin), heading / length * distance);

        // THE FILTERS ----------------------------------------------------------- //
        // No layer filtering yet, so the first two accept everything. The third
        // skips the shooter. An invalid handle becomes Jolt's invalid body id,
        // which never matches a real body - so "ignore nothing" needs no branch.
        const JPH::BroadPhaseLayerFilter  anyTree;
        const JPH::ObjectLayerFilter      anyLayer;
        const JPH::IgnoreSingleBodyFilter skipShooter(Impl::ToId(ignore));

        JPH::RayCastResult result;
        if (!impl.m_world->GetNarrowPhaseQuery().CastRay(ray, result, anyTree, anyLayer, skipShooter)) return false;

        // THE HIT ---------------------------------------------------------------- //
        // Jolt answers with a fraction along the ray; everything else is derived
        // from that, or read off the body it landed on.
        const JPH::RVec3 point = ray.GetPointOnRay(result.mFraction);

        outHit            = RaycastHit{};
        outHit.m_body.m_id = result.mBodyID.GetIndexAndSequenceNumber();
        outHit.m_point    = FromJolt(point);
        outHit.m_distance = result.mFraction * distance;

        // The user data and the surface normal live on the body, and reading a
        // body outside a step means taking its lock. Uncontended here, since
        // everything we do is on the main thread between steps.
        const JPH::BodyLockRead lock(impl.m_world->GetBodyLockInterface(), result.mBodyID);
        if (lock.Succeeded()) {
            outHit.m_userData = lock.GetBody().GetUserData();
            outHit.m_normal   = FromJolt(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point));
        }

        return true;

    }   // Physics::Raycast()

}   // NAMESPACE CRUCIBLE
