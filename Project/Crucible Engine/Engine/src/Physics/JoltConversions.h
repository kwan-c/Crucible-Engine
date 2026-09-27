#pragma once

// Jolt - Jolt.h always comes before any other Jolt header
#pragma warning(push)
#pragma warning(disable : 26495)   // uninitialised member inside Jolt's own headers; not our code to fix
#include <Jolt/Jolt.h>
#pragma warning(pop)

// STL
#include <cmath>

// Crucible engine
#include "Math/Math.h"

// ---------------------------------------------------------------------------
// Engine-private: xmath <-> Jolt.
//
// Never include this from a header that Game can see. It drags in Jolt, which
// is the one thing PhysicsSystem.h goes out of its way to avoid.
//
// The two libraries already agree on everything that usually goes wrong between
// maths libraries: metres, Y up, right-handed, and quaternions stored x, y, z, w
// in that order. So a conversion is a plain copy of three or four floats, with
// no axis flipping and no re-ordering.
//
// The copy is always BY NAME - never a memcpy, a cast or a reinterpret. Both
// types are 16 bytes wide for SIMD, and the fourth lane is not yours: xmath
// leaves whatever was last in it, and Jolt keeps a copy of Z there. Copying
// the bits across would read that.
//
// In its own namespace because IsFinite is a name the engine will want again
// one day. Physics .cpp files put `using namespace JoltConvert;` at the top so
// the call sites stay short, and keep the escape hatch if that ever collides.
// ---------------------------------------------------------------------------

namespace Crucible::JoltConvert {

    inline JPH::Vec3 ToJolt(const Math::vec3& v) { return JPH::Vec3(v.m_X, v.m_Y, v.m_Z); }

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

        const float lengthSq = q.m_X * q.m_X + q.m_Y * q.m_Y + q.m_Z * q.m_Z + q.m_W * q.m_W;

		// written as lengthSq > 1.0e-12f rather than lengthSq != 0 to catch NaN and infinity, which Jolt does not like either
        if (!(lengthSq > 1.0e-12f) || !std::isfinite(lengthSq)) return JPH::Quat::sIdentity();

        const float inverse = 1.0f / std::sqrt(lengthSq);
        return JPH::Quat(q.m_X * inverse, q.m_Y * inverse, q.m_Z * inverse, q.m_W * inverse);

    }   // ToJolt()

    inline Math::vec3 FromJolt(JPH::Vec3Arg v) { return Math::vec3(v.GetX(), v.GetY(), v.GetZ()); }
    inline Math::quat FromJolt(JPH::QuatArg q) { return Math::quat(q.GetX(), q.GetY(), q.GetZ(), q.GetW()); }

    // ------------------------------------------------------------------- //
    // NAN GUARDS
    // ------------------------------------------------------------------- //
    // Checked wherever a number from gameplay crosses into Jolt. One NaN
    // spreads to everything its body touches and then asserts inside the
    // solver, a long way from whoever divided by zero.

    inline bool IsFinite(const float& value) { return std::isfinite(value); }

    inline bool IsFinite(const Math::vec3& v) {
        return std::isfinite(v.m_X) && std::isfinite(v.m_Y) && std::isfinite(v.m_Z);
    }

    inline bool IsFinite(const Math::quat& q) {
        return std::isfinite(q.m_X) && std::isfinite(q.m_Y) && std::isfinite(q.m_Z) && std::isfinite(q.m_W);
    }

}   // NAMESPACE CRUCIBLE::JOLTCONVERT
