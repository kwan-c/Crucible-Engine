#include "PhysicsSystem.h"

// Jolt - Jolt.h always comes before any other Jolt header
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

// STL
#include <algorithm>    // CreateBody()

// Crucible engine
#include "Diagnostics/Logger.h"
#include "Physics/JoltConversions.h"
#include "Physics/PhysicsInternal.h"    

// ---------------------------------------------------------------------------
// Bodies: create, destroy, move, push.
//
// Every one of these starts with Has(), so a handle to a body that was
// destroyed does nothing at all instead of reaching into freed memory. That is
// the promise the header makes, and this is where it is kept.
// ---------------------------------------------------------------------------

namespace {

    using namespace Crucible;

    JPH::EMotionType ToJoltMotion(const BodyMotion& motion) {

        switch (motion) {
            case BodyMotion::Static:    return JPH::EMotionType::Static;
            case BodyMotion::Kinematic: return JPH::EMotionType::Kinematic;
            case BodyMotion::Dynamic:   break;
        }

        return JPH::EMotionType::Dynamic;

    }   // ToJoltMotion()

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    using namespace JoltConvert;

    /*!
        \brief
        Builds a body and puts it in the world.
        \param desc
        The shape, where to put it, and how it should behave.
        \return
        A handle to it, or an invalid handle with the reason in the log.
    */
    BodyHandle Physics::CreateBody(const BodyDesc& desc) {

        Impl& impl = *m_impl;
        if (!impl.m_initialised) return BodyHandle{};

        if (!IsFinite(desc.m_position) || !IsFinite(desc.m_rotation)) {
            CRU_ERROR("Physics::CreateBody: the position or rotation holds NaN or infinity");
            return BodyHandle{};
        }

        if (static_cast<std::size_t>(desc.m_layer) >= kPhysicsLayerCount) {
            CRU_ERROR("Physics::CreateBody: layer {} is not one of the {} in PhysicsLayer",
                      static_cast<int>(desc.m_layer), kPhysicsLayerCount);
            return BodyHandle{};
        }

        const JPH::RefConst<JPH::Shape> shape = Impl::BuildShape(desc.m_shape);
        if (shape == nullptr) return BodyHandle{};      // BuildShape already logged why

        // WHAT KIND OF BODY ------------------------------------------------- //
        const JPH::EMotionType motion = ToJoltMotion(desc.m_motion);

        JPH::BodyCreationSettings creation(shape, ToJolt(desc.m_position), ToJolt(desc.m_rotation),
                                           motion, static_cast<JPH::ObjectLayer>(desc.m_layer));

        creation.mFriction    = std::max(desc.m_friction, 0.0f);
        creation.mRestitution = std::clamp(desc.m_restitution, 0.0f, 1.0f);
        creation.mUserData    = desc.m_userData;

        // Unity's drag defaults to 0; Jolt's damping defaults to 0.05, which
        // gently brakes everything. Match Unity, or a thrown crate quietly loses
        // the speed it was given and nobody can see why.
        creation.mLinearDamping = 0.0f;

        // Unity sets mass directly. Jolt would rather work it out from the shape's
        // volume, which is what m_mass = 0 asks for. Given a real mass,
        // CalculateInertia keeps the shape's spin behaviour and scales it to fit.
        if (motion == JPH::EMotionType::Dynamic && desc.m_mass > 0.0f) {
            creation.mOverrideMassProperties       = JPH::EOverrideMassProperties::CalculateInertia;
            creation.mMassPropertiesOverride.mMass = desc.m_mass;
        }

        // CREATE, THEN ADD --------------------------------------------------- //
        // Two steps in Jolt: creating is cheap, adding is what makes the
        // simulation see it. A static body is added asleep - it has nothing to do.
        JPH::BodyInterface& bodies = impl.Bodies();

        JPH::Body* body = bodies.CreateBody(creation);
        if (body == nullptr) {
            CRU_ERROR("Physics::CreateBody: the world is full at {} bodies - raise PhysicsSettings::m_maxBodies",
                      impl.m_settings.m_maxBodies);
            return BodyHandle{};
        }

        bodies.AddBody(body->GetID(), motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate
                                                                         : JPH::EActivation::Activate);

        const std::uint32_t id = body->GetID().GetIndexAndSequenceNumber();
        impl.m_bodies.insert(id);
        return BodyHandle{ id };

    }   // Physics::CreateBody()

    void Physics::DestroyBody(const BodyHandle& body) {

        Impl& impl = *m_impl;
        if (!impl.Has(body)) return;

        impl.Bodies().RemoveBody(Impl::ToId(body));     // out of the simulation...
        impl.Bodies().DestroyBody(Impl::ToId(body));    // ...then gone
        impl.m_bodies.erase(body.m_id);

    }   // Physics::DestroyBody()

    // A teleport: the body arrives with whatever velocity it already had.
    // Wakes it, because something that was asleep somewhere else has work to do.
    void Physics::SetTransform(const BodyHandle& body, const Math::vec3& position, const Math::quat& rotation) {

        Impl& impl = *m_impl;
        if (!impl.Has(body) || !IsFinite(position) || !IsFinite(rotation)) return;

        impl.Bodies().SetPositionAndRotation(Impl::ToId(body), ToJolt(position), ToJolt(rotation), JPH::EActivation::Activate);

    }   // Physics::SetTransform()

    void Physics::GetTransform(const BodyHandle& body, Math::vec3& outPosition, Math::quat& outRotation) const {

        const Impl& impl = *m_impl;

        if (!impl.Has(body)) {          // a stale handle answers with the origin, never with rubbish
            outPosition = Math::vec3(0.0f, 0.0f, 0.0f);
            outRotation = Math::quat(1.0f, 0.0f, 0.0f, 0.0f);
            return;
        }

        JPH::RVec3 position;
        JPH::Quat  rotation;
        impl.Bodies().GetPositionAndRotation(Impl::ToId(body), position, rotation);

        outPosition = FromJolt(position);
        outRotation = FromJolt(rotation);

    }   // Physics::GetTransform()

    // Drives a kinematic body toward a target over one step. Jolt works out the
    // velocity that gets it there, and it is that velocity - not the teleport -
    // that lets a lift carry a crate instead of sliding through it.
    void Physics::MoveKinematic(const BodyHandle& body, const Math::vec3& targetPosition, const Math::quat& targetRotation, float fixedDt) {

        Impl& impl = *m_impl;
        if (!impl.Has(body) || !IsFinite(targetPosition) || !IsFinite(targetRotation) || !(fixedDt > 0.0f)) return;

        // Unlike the velocity and impulse calls, Jolt's MoveKinematic has no
        // static-body guard. A static body owns no motion properties, so this
        // would assert in Debug and dereference null in Release.
        if (impl.Bodies().GetMotionType(Impl::ToId(body)) == JPH::EMotionType::Static) {
            CRU_WARN("Physics::MoveKinematic: that body is static and cannot be driven - create it as Kinematic");
            return;
        }

        impl.Bodies().MoveKinematic(Impl::ToId(body), ToJolt(targetPosition), ToJolt(targetRotation), fixedDt);

    }   // Physics::MoveKinematic()

    void Physics::SetLinearVelocity(const BodyHandle& body, const Math::vec3& velocity) {

        Impl& impl = *m_impl;
        if (!impl.Has(body) || !IsFinite(velocity)) return;

        impl.Bodies().SetLinearVelocity(Impl::ToId(body), ToJolt(velocity));    // ignored on static bodies; wakes the rest

    }   // Physics::SetLinearVelocity()

    Math::vec3 Physics::GetLinearVelocity(const BodyHandle& body) const {

        const Impl& impl = *m_impl;
        if (!impl.Has(body)) return Math::vec3(0.0f, 0.0f, 0.0f);

        return FromJolt(impl.Bodies().GetLinearVelocity(Impl::ToId(body)));

    }   // Physics::GetLinearVelocity()

    // The crate jumping when it is shot. Jolt applies this to dynamic bodies
    // only, and wakes them, so shooting a wall is harmless rather than special-cased.
    void Physics::ApplyImpulse(const BodyHandle& body, const Math::vec3& impulse) {

        Impl& impl = *m_impl;
        if (!impl.Has(body) || !IsFinite(impulse)) return;

        impl.Bodies().AddImpulse(Impl::ToId(body), ToJolt(impulse));

    }   // Physics::ApplyImpulse()

}   // NAMESPACE CRUCIBLE
