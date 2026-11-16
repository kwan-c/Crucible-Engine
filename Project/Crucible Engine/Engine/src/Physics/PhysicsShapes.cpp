/*************************************************************************
* \file:         PhysicsShapes.cpp
* \author:       Hugo Low Ren Hao (main author),
*                Jean David Vidal Cruzado (v1.1: heightfields)
* \par:          Email: low.h@digipen.edu,
*                Email: jeandavidvidal.c@digipen.edu
* \date          27 September 2026
* \version       1.1
* \brief
*                Turns a ShapeDesc (box, sphere, capsule, heightfield) into
*                a Jolt shape.
*
* \details
*                Sizes below a millimetre are raised to one, because Jolt
*                refuses zero-sized shapes and a zero is usually a typo. A
*                shape Jolt cannot build is logged here and returns null.
*
* \history
*    -v1.0       first version
*    -v1.1       HeightField: a HeightGrid as a Jolt HeightFieldShape
*
**************************************************************************/
#include "PhysicsSystem.h"

// Jolt - Jolt.h always comes before any other Jolt header
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

// STL
#include <algorithm>    // BuildShape()
#include <cmath>        // BuildShape()
#include <vector>       // BuildHeightField()

// Crucible engine
#include "Diagnostics/Logger.h"
#include "Math/HeightGrid.h"
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

    // Heights are compressed per 2 x 2 block; this keeps the collider within a
    // centimetre of the grid, so it never visibly floats over or sinks into the
    // rendered mesh, even on tall cliffs.
    constexpr float kHeightFieldMaxError = 0.01f;

    // A HeightGrid as Jolt's HeightFieldShape. Jolt wants a square grid of
    // samples at least 4 wide and a whole number of blocks across, so a
    // rectangular or tiny grid is padded with "no collision" samples: the
    // padding is holes, not ground. Jolt places sample (x, z) at
    // offset + scale * (x, height, z) and splits each cell along the same
    // diagonal as HeightGrid, so the two describe one surface.
    JPH::RefConst<JPH::Shape> BuildHeightField(const HeightGrid& grid, const Math::vec3& gridScale) {

        constexpr std::uint32_t kBlockSize = 2;   // Jolt's default; HeightFieldShapeSettings::mBlockSize

        const JPH::Vec3 scale = JPH::Vec3::sMax(ToJolt(gridScale).Abs(), JPH::Vec3::sReplicate(kMinSize));

        std::uint32_t side = std::max({ grid.CornerCols(), grid.CornerRows(), 2 * kBlockSize });
        side = (side + kBlockSize - 1) / kBlockSize * kBlockSize;

        std::vector<float> samples(static_cast<std::size_t>(side) * side, JPH::HeightFieldShapeConstants::cNoCollisionValue);
        for (std::uint32_t z = 0; z < grid.CornerRows(); ++z)
            for (std::uint32_t x = 0; x < grid.CornerCols(); ++x)
                samples[static_cast<std::size_t>(z) * side + x] = grid.Height(x, z);

        const JPH::Vec3 offset(grid.MinX() * scale.GetX(), 0.0f, grid.MinZ() * scale.GetZ());
        const JPH::Vec3 step(grid.cellSize * scale.GetX(), scale.GetY(), grid.cellSize * scale.GetZ());

        JPH::HeightFieldShapeSettings settings(samples.data(), offset, step, side);   // copies the samples
        settings.mBlockSize     = kBlockSize;
        settings.mBitsPerSample = settings.CalculateBitsPerSampleForError(kHeightFieldMaxError);

        return Create(settings, "heightfield");

    }   // BuildHeightField()

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

            case ShapeType::HeightField: {

                // The ground comes from elsewhere (the Terrain on the same entity);
                // until it exists there is nothing to collide with.
                if (desc.m_heightGrid == nullptr || !desc.m_heightGrid->IsValid()) {
                    CRU_ERROR("Physics: a HeightField collider has no ground to build from - give its entity a Terrain and build it");
                    return nullptr;
                }
                if (!IsFinite(desc.m_gridScale)) {
                    CRU_ERROR("Physics: the heightfield's scale holds NaN or infinity, so no shape was built");
                    return nullptr;
                }

                shape = BuildHeightField(*desc.m_heightGrid, desc.m_gridScale);
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
