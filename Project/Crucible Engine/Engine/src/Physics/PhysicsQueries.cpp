/*************************************************************************
* \file:         PhysicsQueries.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          27 September 2026
* \version       2.0
* \brief
*                The queries, Unity's Physics.Raycast / RaycastAll /
*                SphereCast / CapsuleCast / OverlapSphere / OverlapBox /
*                OverlapCapsule.
*
* \details
*                Every query is one of Jolt's three questions:
*                  - CastRay:      what does a line hit
*                  - CastShape:    what does a shape hit when swept along
*                                  a line (the Sphere and Capsule casts)
*                  - CollideShape: what overlaps a shape right now (the
*                                  Overlaps)
*                and every one takes the same QueryFilter, turned into
*                Jolt's filter objects in one place (Filters, below).
*
*                Directions are normalised for you. A query that starts
*                inside a shape hits it at distance zero, so pass the
*                shooter's own body in QueryFilter::m_ignore.
*
* \history
*    -v1.0       Raycast
*    -v2.0       QueryFilter (layer mask, triggers, ignore); RaycastAll,
*                SphereCast, CapsuleCast, OverlapSphere, OverlapBox,
*                OverlapCapsule
*
**************************************************************************/
#include "PhysicsSystem.h"

// Jolt - Jolt.h always comes before any other Jolt header
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

// STL
#include <algorithm>

// Crucible engine
#include "Physics/JoltConversions.h"
#include "Physics/PhysicsInternal.h"

namespace {

    using namespace Crucible;
    using namespace Crucible::JoltConvert;

    // Jolt needs a ray of finite length, so "as far as it goes" becomes 100 km.
    // Nothing in a level is further away than that, and the number keeps the
    // maths well behaved.
    constexpr float kMaxRayDistance = 100000.0f;

    // --- the filter, in Jolt's terms -------------------------------------- //

    // The layer mask. Jolt asks this about every candidate's layer before any
    // shape maths, so a masked-out layer costs next to nothing.
    class MaskFilter final : public JPH::ObjectLayerFilter {
    public:
        explicit MaskFilter(const LayerMask& mask) : m_mask(mask) {}
        bool ShouldCollide(JPH::ObjectLayer layer) const override { return m_mask.Contains(static_cast<std::uint32_t>(layer)); }
    private:
        LayerMask m_mask;
    };  // MASKFILTER CLASS

    // The ignored body, and triggers. The first question needs only the id; the
    // second needs the body, which Jolt has already locked when it asks.
    class QueryBodyFilter final : public JPH::BodyFilter {
    public:
        QueryBodyFilter(const JPH::BodyID& ignore, bool hitTriggers) : m_ignore(ignore), m_hitTriggers(hitTriggers) {}
        bool ShouldCollide(const JPH::BodyID& body) const override { return body != m_ignore; }
        bool ShouldCollideLocked(const JPH::Body& body) const override { return m_hitTriggers || !body.IsSensor(); }
    private:
        JPH::BodyID m_ignore;
        bool        m_hitTriggers;
    };  // QUERYBODYFILTER CLASS

    // All three of Jolt's filters for one QueryFilter. The search-tree filter
    // accepts both trees: the mask does the real work one level down.
    struct Filters {
        explicit Filters(const QueryFilter& filter)
            : layers(filter.m_layers), bodies(JPH::BodyID(filter.m_ignore.m_id), filter.m_hitTriggers) {}
        JPH::BroadPhaseLayerFilter trees;
        MaskFilter                 layers;
        QueryBodyFilter            bodies;
    };  // FILTERS STRUCT

    // A direction and a distance, checked and normalised. False if there is
    // nothing to cast (zero direction, no distance, NaN).
    bool PrepareCast(const Math::vec3& origin, const Math::vec3& direction, float maxDistance, JPH::Vec3& outPath, float& outDistance) {
        if (!IsFinite(origin) || !IsFinite(direction) || !(maxDistance > 0.0f)) return false;
        const JPH::Vec3 heading = ToJolt(direction);
        const float     length  = heading.Length();
        if (!(length > 1.0e-6f)) return false;              // a zero-length direction goes nowhere
        outDistance = std::min(maxDistance, kMaxRayDistance);
        outPath     = heading / length * outDistance;       // Jolt wants the whole path, not just the heading
        return true;
    }

    // A capsule from Unity's description (the centres of its two rounded ends)
    // to Jolt's: a capsule standing along Y, then turned and placed. Returns the
    // capsule's centre and rotation; the shape is built by the caller.
    void CapsulePose(const Math::vec3& point1, const Math::vec3& point2, JPH::Vec3& outCenter, JPH::Quat& outRotation, float& outHalfHeight) {
        const JPH::Vec3 a = ToJolt(point1);
        const JPH::Vec3 b = ToJolt(point2);
        const JPH::Vec3 axis = b - a;
        const float     length = axis.Length();
        outCenter     = (a + b) * 0.5f;
        outHalfHeight = length * 0.5f;
        outRotation   = length > 1.0e-5f ? JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), axis / length) : JPH::Quat::sIdentity();
    }

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    // The user value and the surface normal live on the body, and reading a body
    // outside a step means taking its lock. Uncontended here: everything we do is
    // on the main thread between steps.
    static void FillHit(const JPH::PhysicsSystem& world, const JPH::BodyID& body, const JPH::SubShapeID& subShape,
                        const JPH::RVec3& point, float distance, RaycastHit& out) {
        out            = RaycastHit{};
        out.m_body.m_id = body.GetIndexAndSequenceNumber();
        out.m_point    = FromJolt(point);
        out.m_distance = distance;
        const JPH::BodyLockRead lock(world.GetBodyLockInterface(), body);
        if (lock.Succeeded()) {
            out.m_userData = lock.GetBody().GetUserData();
            out.m_normal   = FromJolt(lock.GetBody().GetWorldSpaceSurfaceNormal(subShape, point));
        }
    }

    // One shape swept along a line: the closest thing it hits. Shared by the
    // Sphere and Capsule casts.
    static bool CastOneShape(const JPH::PhysicsSystem& world, const JPH::Shape& shape, const JPH::RMat44& start, const JPH::Vec3& path,
                             float distance, const QueryFilter& filter, RaycastHit& outHit) {

        const Filters filters(filter);
        const JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(&shape, JPH::Vec3::sOne(), start, path);
        JPH::ShapeCastSettings settings;
        JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> closest;
        world.GetNarrowPhaseQuery().CastShape(cast, settings, JPH::RVec3::sZero(), closest,
                                                      filters.trees, filters.layers, filters.bodies);
        if (!closest.HadHit()) return false;

        // Where they touched, on the thing that was hit, and which way its surface
        // faces: Jolt gives the direction to push the hit body OUT of the shape,
        // so the surface faces the other way.
        const JPH::ShapeCastResult& hit = closest.mHit;
        FillHit(world, hit.mBodyID2, hit.mSubShapeID2, hit.mContactPointOn2, hit.mFraction * distance, outHit);
        if (hit.mPenetrationAxis.LengthSq() > 1.0e-12f) outHit.m_normal = FromJolt(-hit.mPenetrationAxis.Normalized());
        return true;
    }

    // Everything overlapping one shape right now, each body once. Shared by the
    // three Overlaps.
    static void OverlapOneShape(const JPH::PhysicsSystem& world, const JPH::Shape& shape, const JPH::RMat44& transform,
                                const QueryFilter& filter, std::vector<OverlapHit>& out) {

        out.clear();
        const Filters filters(filter);
        JPH::CollideShapeSettings settings;
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> all;
        world.GetNarrowPhaseQuery().CollideShape(&shape, JPH::Vec3::sOne(), transform, settings, JPH::RVec3::sZero(), all,
                                                         filters.trees, filters.layers, filters.bodies);

        // A body can touch the shape in several places; list it once.
        for (const JPH::CollideShapeResult& hit : all.mHits) {
            const std::uint32_t id = hit.mBodyID2.GetIndexAndSequenceNumber();
            const bool listed = std::any_of(out.begin(), out.end(), [id](const OverlapHit& o) { return o.m_body.m_id == id; });
            if (listed) continue;

            OverlapHit entry;
            entry.m_body.m_id = id;
            const JPH::BodyLockRead lock(world.GetBodyLockInterface(), hit.mBodyID2);
            if (lock.Succeeded()) entry.m_userData = lock.GetBody().GetUserData();
            out.push_back(entry);
        }
    }

    /*!
        \brief
        The closest body along a ray. Unity's Physics.Raycast.
    */
    bool Physics::Raycast(const Math::vec3& origin, const Math::vec3& direction, RaycastHit& outHit, float maxDistance,
                          const QueryFilter& filter) const {

        const Impl& impl = *m_impl;
        JPH::Vec3 path;
        float     distance = 0.0f;
        if (!impl.m_initialised || !PrepareCast(origin, direction, maxDistance, path, distance)) return false;

        const Filters       filters(filter);
        const JPH::RRayCast ray(ToJolt(origin), path);
        JPH::RayCastResult  result;
        if (!impl.m_world->GetNarrowPhaseQuery().CastRay(ray, result, filters.trees, filters.layers, filters.bodies)) return false;

        // Jolt answers with a fraction along the ray; the rest is read off the body.
        FillHit(*impl.m_world, result.mBodyID, result.mSubShapeID2, ray.GetPointOnRay(result.mFraction), result.mFraction * distance, outHit);
        return true;

    }   // Physics::Raycast()

    /*!
        \brief
        Everything along a ray, nearest first. Unity's Physics.RaycastAll.
    */
    void Physics::RaycastAll(const Math::vec3& origin, const Math::vec3& direction, float maxDistance, std::vector<RaycastHit>& out,
                             const QueryFilter& filter) const {

        out.clear();
        const Impl& impl = *m_impl;
        JPH::Vec3 path;
        float     distance = 0.0f;
        if (!impl.m_initialised || !PrepareCast(origin, direction, maxDistance, path, distance)) return;

        const Filters       filters(filter);
        const JPH::RRayCast ray(ToJolt(origin), path);
        JPH::RayCastSettings settings;          // solid shapes, back faces ignored: one hit per body, where the ray goes in
        JPH::AllHitCollisionCollector<JPH::CastRayCollector> all;
        impl.m_world->GetNarrowPhaseQuery().CastRay(ray, settings, all, filters.trees, filters.layers, filters.bodies);
        all.Sort();                             // Jolt collects in whatever order it finds them

        for (const JPH::RayCastResult& hit : all.mHits) {
            RaycastHit entry;
            FillHit(*impl.m_world, hit.mBodyID, hit.mSubShapeID2, ray.GetPointOnRay(hit.mFraction), hit.mFraction * distance, entry);
            out.push_back(entry);
        }

    }   // Physics::RaycastAll()

    /*!
        \brief
        A ball swept along a line: the closest thing it hits. Unity's
        Physics.SphereCast.
    */
    bool Physics::SphereCast(const Math::vec3& origin, float radius, const Math::vec3& direction, RaycastHit& outHit, float maxDistance,
                             const QueryFilter& filter) const {

        const Impl& impl = *m_impl;
        JPH::Vec3 path;
        float     distance = 0.0f;
        if (!impl.m_initialised || !(radius > 0.0f) || !PrepareCast(origin, direction, maxDistance, path, distance)) return false;

        // On the stack, for this one question. SetEmbedded tells Jolt nobody owns
        // it through a reference count, so it never tries to delete it.
        JPH::SphereShape sphere(radius);
        sphere.SetEmbedded();
        return CastOneShape(*impl.m_world, sphere, JPH::RMat44::sTranslation(ToJolt(origin)), path, distance, filter, outHit);

    }   // Physics::SphereCast()

    /*!
        \brief
        A capsule swept along a line: the closest thing it hits. Unity's
        Physics.CapsuleCast. A capsule with both ends at one point is a sphere.
    */
    bool Physics::CapsuleCast(const Math::vec3& point1, const Math::vec3& point2, float radius, const Math::vec3& direction,
                              RaycastHit& outHit, float maxDistance, const QueryFilter& filter) const {

        const Impl& impl = *m_impl;
        JPH::Vec3 path;
        float     distance = 0.0f;
        if (!impl.m_initialised || !(radius > 0.0f) || !IsFinite(point2) || !PrepareCast(point1, direction, maxDistance, path, distance)) return false;

        JPH::Vec3 center;
        JPH::Quat rotation;
        float     halfHeight = 0.0f;
        CapsulePose(point1, point2, center, rotation, halfHeight);
        const JPH::RMat44 start = JPH::RMat44::sRotationTranslation(rotation, center);

        if (halfHeight < 1.0e-5f) {
            JPH::SphereShape sphere(radius);
            sphere.SetEmbedded();
            return CastOneShape(*impl.m_world, sphere, start, path, distance, filter, outHit);
        }
        JPH::CapsuleShape capsule(halfHeight, radius);
        capsule.SetEmbedded();
        return CastOneShape(*impl.m_world, capsule, start, path, distance, filter, outHit);

    }   // Physics::CapsuleCast()

    /*!
        \brief
        Everything inside a ball right now. Unity's Physics.OverlapSphere.
    */
    void Physics::OverlapSphere(const Math::vec3& center, float radius, std::vector<OverlapHit>& out, const QueryFilter& filter) const {

        out.clear();
        const Impl& impl = *m_impl;
        if (!impl.m_initialised || !(radius > 0.0f) || !IsFinite(center)) return;

        JPH::SphereShape sphere(radius);
        sphere.SetEmbedded();
        OverlapOneShape(*impl.m_world, sphere, JPH::RMat44::sTranslation(ToJolt(center)), filter, out);

    }   // Physics::OverlapSphere()

    /*!
        \brief
        Everything inside a box right now. Unity's Physics.OverlapBox: half
        sizes, and a rotation.
    */
    void Physics::OverlapBox(const Math::vec3& center, const Math::vec3& halfExtents, const Math::quat& orientation,
                             std::vector<OverlapHit>& out, const QueryFilter& filter) const {

        out.clear();
        const Impl& impl = *m_impl;
        if (!impl.m_initialised || !IsFinite(center) || !IsFinite(halfExtents) || !IsFinite(orientation)) return;
        if (!(halfExtents.x > 0.0f) || !(halfExtents.y > 0.0f) || !(halfExtents.z > 0.0f)) return;

        // Jolt rounds a box's corners by a small radius and shrinks that radius
        // itself for boxes smaller than it, so any size is safe here.
        JPH::BoxShape box(ToJolt(halfExtents));
        box.SetEmbedded();
        OverlapOneShape(*impl.m_world, box, JPH::RMat44::sRotationTranslation(ToJolt(orientation).Normalized(), ToJolt(center)), filter, out);

    }   // Physics::OverlapBox()

    /*!
        \brief
        Everything inside a capsule right now. Unity's Physics.OverlapCapsule:
        the centres of its two rounded ends, and a radius.
    */
    void Physics::OverlapCapsule(const Math::vec3& point0, const Math::vec3& point1, float radius, std::vector<OverlapHit>& out,
                                 const QueryFilter& filter) const {

        out.clear();
        const Impl& impl = *m_impl;
        if (!impl.m_initialised || !(radius > 0.0f) || !IsFinite(point0) || !IsFinite(point1)) return;

        JPH::Vec3 center;
        JPH::Quat rotation;
        float     halfHeight = 0.0f;
        CapsulePose(point0, point1, center, rotation, halfHeight);
        const JPH::RMat44 transform = JPH::RMat44::sRotationTranslation(rotation, center);

        if (halfHeight < 1.0e-5f) {
            JPH::SphereShape sphere(radius);
            sphere.SetEmbedded();
            OverlapOneShape(*impl.m_world, sphere, transform, filter, out);
            return;
        }
        JPH::CapsuleShape capsule(halfHeight, radius);
        capsule.SetEmbedded();
        OverlapOneShape(*impl.m_world, capsule, transform, filter, out);

    }   // Physics::OverlapCapsule()

}   // NAMESPACE CRUCIBLE
