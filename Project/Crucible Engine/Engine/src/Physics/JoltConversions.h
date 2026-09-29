#pragma once

// Jolt - Jolt.h always comes before any other Jolt header
#include <Jolt/Jolt.h>

// STL
#include <cmath>

// Crucible engine
#include "Math/Math.h"

// glm's quaternion constructor order is a build setting, not a fixed fact: define
// GLM_FORCE_QUAT_DATA_XYZW and it silently becomes (x, y, z, w). Every rotation in
// the engine would then be subtly wrong rather than obviously broken, which is the
// worst way for a bug to behave. Fail the build instead.
#ifdef GLM_FORCE_QUAT_DATA_XYZW
#error "Crucible assumes glm's default (w, x, y, z) quaternion constructor - see Physics/JoltConversions.h"
#endif

// ---------------------------------------------------------------------------
// Engine-private: glm <-> Jolt.
//
// Never include this from a header that Game can see. It drags in Jolt, which
// is the one thing PhysicsSystem.h goes out of its way to avoid.
//
// The two libraries agree on the things that usually go wrong between maths
// libraries: metres, Y up, right-handed. So there is no axis flipping here.
//
// They disagree on exactly one thing, and it is the easy one to get wrong:
// glm's quaternion constructor takes (w, x, y, z), Jolt's takes (x, y, z, w).
// Every quaternion built in this file reorders for that, and it is the reason
// a wrong rotation shows up as a plausible-looking one rather than a crash.
//
// The copy is always BY NAME - never a memcpy, a cast or a reinterpret. They
// are not even the same size: glm's vec3 is three floats, while Jolt's Vec3 is
// four wide for SIMD and keeps a copy of Z in that fourth lane.
//
// In its own namespace because IsFinite is a name the engine will want again
// one day. Physics .cpp files put `using namespace JoltConvert;` at the top so
// the call sites stay short, and keep the escape hatch if that ever collides.
// ---------------------------------------------------------------------------

namespace Crucible::JoltConvert {

    inline JPH::Vec3 ToJolt(const Math::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }

    /*!
        \brief
        A rotation, normalised on the way in.
        \details
        Jolt asserts that every quaternion handed to it is unit length. Gameplay
        will eventually hand us one that is not - a default-constructed component,
        or one that has drifted after a few thousand multiplications - and an
        assert deep inside Jolt is a bad way to find that out.
        \return
        The rotation as a unit quaternion, or identity if it was zero or broken.
    */
    inline JPH::Quat ToJolt(const Math::quat& q) {

        const float lengthSq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;

		// written as lengthSq > 1.0e-12f rather than lengthSq != 0 to catch NaN and infinity, which Jolt does not like either
        if (!(lengthSq > 1.0e-12f) || !std::isfinite(lengthSq)) return JPH::Quat::sIdentity();

        const float inverse = 1.0f / std::sqrt(lengthSq);
        return JPH::Quat(q.x * inverse, q.y * inverse, q.z * inverse, q.w * inverse);

    }   // ToJolt()

    inline Math::vec3 FromJolt(JPH::Vec3Arg v) { return Math::vec3(v.GetX(), v.GetY(), v.GetZ()); }
    inline Math::quat FromJolt(JPH::QuatArg q) { return Math::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

    // ------------------------------------------------------------------- //
    // NAN GUARDS
    // ------------------------------------------------------------------- //
    // Checked wherever a number from gameplay crosses into Jolt. One NaN
    // spreads to everything its body touches and then asserts inside the
    // solver, a long way from whoever divided by zero.

    inline bool IsFinite(const float& value) { return std::isfinite(value); }

    inline bool IsFinite(const Math::vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    inline bool IsFinite(const Math::quat& q) {
        return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
    }

}   // NAMESPACE CRUCIBLE::JOLTCONVERT
