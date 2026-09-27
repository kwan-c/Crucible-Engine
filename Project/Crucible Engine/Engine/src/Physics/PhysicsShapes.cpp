#include "PhysicsSystem.h"

// Jolt - Jolt.h always comes before any other Jolt header
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

// STL
#include <algorithm>    // BuildShape()
#include <cmath>        // BuildShape()

// Crucible engine
#include "Diagnostics/Logger.h"
#include "Physics/JoltConversions.h"
#include "Physics/PhysicsInternal.h"

// ---------------------------------------------------------------------------
// One job: a ShapeDesc in, a Jolt shape out.
//
// Shapes are immutable and reference counted in Jolt, which is why this hands
// back a RefConst rather than a raw pointer, and why two bodies with the same
// shape could share one later without either of them owning it.
// ---------------------------------------------------------------------------

namespace {

    using namespace Crucible;
    using namespace Crucible::JoltConvert;

    // Nothing gets thinner than a millimetre. Jolt refuses a zero-sized shape,
    // and a zero in a collider is a typo far more often than an intention.
    constexpr float kMinSize = 0.001f;

    // Jolt builds shapes from settings objects, and that build can fail - a
    // capsule thinner than its own radius, for instance. This is the one place
    // that failure gets turned into a log line.
    JPH::RefConst<JPH::Shape> Create(const JPH::ShapeSettings& settings, const char* what) {

        const JPH::ShapeSettings::ShapeResult result = settings.Create();
        if (result.HasError()) {
            CRU_ERROR("Physics: could not build the {} shape - {}", what, result.GetError().c_str());
            return nullptr;
        }

        return result.Get();

    }   // Create()

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    /*!
        \brief
        Builds the Jolt shape a body will collide with.
        \param desc
        The shape to build. Sizes are half-sizes, and are clamped rather than
        rejected, so a designer typing 0 gets a millimetre instead of no body.
        \return
        The shape, or null with the reason in the log.
    */
    JPH::RefConst<JPH::Shape> Physics::Impl::BuildShape(const ShapeDesc& desc) {

        if (!IsFinite(desc.m_halfExtents) || !IsFinite(desc.m_radius) || !IsFinite(desc.m_halfHeight) || !IsFinite(desc.m_center)) {
            CRU_ERROR("Physics: the collider holds NaN or infinity, so no shape was built");
            return nullptr;
        }

        JPH::RefConst<JPH::Shape> shape;

        switch (desc.m_type) {

            case ShapeType::Box: {

                const JPH::Vec3 half = JPH::Vec3::sMax(ToJolt(desc.m_halfExtents).Abs(), JPH::Vec3::sReplicate(kMinSize));

                // Jolt rounds a box's edges by a small "convex radius" so that
                // resting contacts are stable. It cannot be larger than the box's
                // smallest half extent, or a thin plate would round away to nothing.
                const float convexRadius = std::min(JPH::cDefaultConvexRadius, half.ReduceMin() * 0.5f);

                shape = Create(JPH::BoxShapeSettings(half, convexRadius), "box");
                break;
            }

            case ShapeType::Sphere: {

                shape = Create(JPH::SphereShapeSettings(std::max(std::abs(desc.m_radius), kMinSize)), "sphere");
                break;
            }

            case ShapeType::Capsule: {

                const float radius     = std::max(std::abs(desc.m_radius), kMinSize);
                const float halfHeight = std::abs(desc.m_halfHeight);

                // A capsule with no straight middle left is a sphere, and Jolt
                // would rather be handed the sphere than a degenerate capsule.
                shape = halfHeight > kMinSize
                      ? Create(JPH::CapsuleShapeSettings(halfHeight, radius), "capsule")
                      : Create(JPH::SphereShapeSettings(radius), "capsule (too short for a middle, built as a sphere)");
                break;
            }
        }

        if (shape == nullptr) return nullptr;

        // Unity's Collider.center. Wrapping the shape is how Jolt offsets one -
        // the wrapper is not free, so a centred collider skips it entirely.
        const JPH::Vec3 center = ToJolt(desc.m_center);
        if (center.IsNearZero()) return shape;

        return Create(JPH::RotatedTranslatedShapeSettings(center, JPH::Quat::sIdentity(), shape.GetPtr()), "offset");

    }   // Physics::Impl::BuildShape()

}   // NAMESPACE CRUCIBLE
