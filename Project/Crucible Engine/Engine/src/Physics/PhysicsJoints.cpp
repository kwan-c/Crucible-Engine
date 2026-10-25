/*************************************************************************
* \file:         PhysicsJoints.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          15 October 2026
* \version       1.0
* \brief
*                Joints, Unity's FixedJoint, HingeJoint, CharacterJoint and
*                SpringJoint: create, destroy, read a hinge's angle, and
*                break the ones that held too much.
*
* \details
*                Every joint is built in world space from the two bodies'
*                pose at that moment, so the rest pose is "how they are
*                now" and every limit is measured from it. Jolt's names
*                differ from Unity's: Ball is Jolt's Point constraint,
*                Character is SwingTwist and Spring is Distance.
*
*                A Jolt constraint holds raw pointers to its bodies, so
*                every path that destroys a body removes its joints first.
*
* \history
*    -v1.0       Fixed, Hinge, Ball, Character, Spring; limits, breaking,
*                and jointed bodies passing through each other
*
**************************************************************************/
#include "PhysicsSystem.h"

// Jolt - Jolt.h always comes before any other Jolt header
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

// STL
#include <algorithm>    // std::clamp, std::max, std::swap
#include <cmath>        // std::sqrt, std::fabs

// Crucible engine
#include "Diagnostics/Logger.h"
#include "Physics/JoltConversions.h"
#include "Physics/PhysicsInternal.h"

namespace {

    using namespace Crucible;
    using namespace Crucible::JoltConvert;

    // An axis shorter than this has no direction to speak of: a typo, or a
    // scaled-to-zero entity. Refused rather than normalised into NaN.
    constexpr float kMinAxisLength = 1.0e-4f;

    float Radians(float degrees) { return JPH::DegreesToRadians(degrees); }

    /*!
        \brief
        The swing axis, made exactly perpendicular to the twist axis. Jolt
        needs the two at right angles; a designer typing "roughly up" should
        not have to get that exact, so the part along the twist is removed.
        \return
        A unit axis at right angles to twist. Any perpendicular at all when
        swing is missing or points along twist.
    */
    JPH::Vec3 PerpendicularPart(const JPH::Vec3& twist, const JPH::Vec3& swing) {

        const JPH::Vec3 across = swing - twist * twist.Dot(swing);
        return across.Length() < kMinAxisLength ? twist.GetNormalizedPerpendicular() : across.Normalized();

    }   // PerpendicularPart()

    /*!
        \brief
        A JointDesc turned into the matching Jolt settings object, in world
        space. The settings are a recipe: Jolt copies them into the constraint
        and they can be thrown away after.

        Each case returns settings.GetPtr(), not settings: Jolt's Ref does not
        convert from Ref<Derived> to Ref<Base> the way std::shared_ptr does,
        so the raw pointer is handed over and the returned Ref takes its own
        reference to it before the local one lets go.
    */
    JPH::Ref<JPH::TwoBodyConstraintSettings> BuildSettings(const JointDesc& desc) {

        const JPH::RVec3 anchor = ToJolt(desc.m_anchor);
        const JPH::Vec3  axis   = ToJolt(desc.m_axis).Normalized();   // length checked by the caller

        switch (desc.m_type) {

            case JointType::Fixed: {
                // Both frames given the same axes in world space means "keep the
                // relative pose they have right now", which is what glue is.
                JPH::Ref<JPH::FixedConstraintSettings> settings = new JPH::FixedConstraintSettings();
                settings->mPoint1 = anchor;
                settings->mPoint2 = anchor;
                return settings.GetPtr();
            }

            case JointType::Ball: {
                JPH::Ref<JPH::PointConstraintSettings> settings = new JPH::PointConstraintSettings();
                settings->mPoint1 = anchor;
                settings->mPoint2 = anchor;
                return settings.GetPtr();
            }

            case JointType::Hinge: {
                JPH::Ref<JPH::HingeConstraintSettings> settings = new JPH::HingeConstraintSettings();
                settings->mPoint1      = anchor;
                settings->mPoint2      = anchor;
                settings->mHingeAxis1  = axis;
                settings->mHingeAxis2  = axis;
                // The normal is only the "angle 0" marker. The same one on both
                // sides means angle 0 is the pose right now, whichever
                // perpendicular is picked.
                settings->mNormalAxis1 = axis.GetNormalizedPerpendicular();
                settings->mNormalAxis2 = settings->mNormalAxis1;

                if (desc.m_useLimits) {
                    // Jolt requires min in [-180, 0] and max in [0, 180]: the rest
                    // pose must be inside the limits. Unity allows "10 to 50", which
                    // starts the joint already out of bounds, so that is clamped.
                    const float minAngle = std::clamp(desc.m_minAngle, -180.0f, 0.0f);
                    const float maxAngle = std::clamp(desc.m_maxAngle, 0.0f, 180.0f);
                    if (minAngle != desc.m_minAngle || maxAngle != desc.m_maxAngle)
                        CRU_WARN("Physics::CreateJoint: hinge limits {} to {} must include 0 (the pose it was made in) - using {} to {}",
                                 desc.m_minAngle, desc.m_maxAngle, minAngle, maxAngle);
                    settings->mLimitsMin = Radians(minAngle);
                    settings->mLimitsMax = Radians(maxAngle);
                }
                return settings.GetPtr();
            }

            case JointType::Character: {
                JPH::Ref<JPH::SwingTwistConstraintSettings> settings = new JPH::SwingTwistConstraintSettings();
                const JPH::Vec3 swingAxis = PerpendicularPart(axis, ToJolt(desc.m_swingAxis));

                settings->mPosition1  = anchor;
                settings->mPosition2  = anchor;
                settings->mTwistAxis1 = axis;
                settings->mTwistAxis2 = axis;
                settings->mPlaneAxis1 = swingAxis;
                settings->mPlaneAxis2 = swingAxis;

                // Jolt's "plane" cone limits turning AROUND the plane axis, and its
                // "normal" cone turning around the third axis (SwingTwistConstraint.cpp
                // passes them to SetLimits as swing Y and swing Z). Unity's swing1
                // turns around swingAxis, so swing1 is the plane cone.
                settings->mPlaneHalfConeAngle  = Radians(std::clamp(desc.m_swing1, 0.0f, 180.0f));
                settings->mNormalHalfConeAngle = Radians(std::clamp(desc.m_swing2, 0.0f, 180.0f));

                float twistMin = std::clamp(desc.m_twistMin, -180.0f, 180.0f);
                float twistMax = std::clamp(desc.m_twistMax, -180.0f, 180.0f);
                if (twistMin > twistMax) std::swap(twistMin, twistMax);   // typed the wrong way round
                settings->mTwistMinAngle = Radians(twistMin);
                settings->mTwistMaxAngle = Radians(twistMax);
                return settings.GetPtr();
            }

            case JointType::Spring: {
                JPH::Ref<JPH::DistanceConstraintSettings> settings = new JPH::DistanceConstraintSettings();
                settings->mPoint1 = ToJolt(desc.m_anchorB);     // Jolt's body 1 is the connected one (see CreateJoint)
                settings->mPoint2 = anchor;

                // Jolt's -1 means "the distance they are apart now", which is
                // what 0 means to us: a spring at rest when the joint is made.
                float minDistance = desc.m_minDistance > 0.0f ? desc.m_minDistance : -1.0f;
                float maxDistance = desc.m_maxDistance > 0.0f ? desc.m_maxDistance : -1.0f;
                if (minDistance > 0.0f && maxDistance > 0.0f && minDistance > maxDistance) std::swap(minDistance, maxDistance);
                settings->mMinDistance = minDistance;
                settings->mMaxDistance = maxDistance;

                // Stiffness and damping in Unity's units (N/m and N*s/m). A
                // stiffness of 0 leaves the limits hard: a rigid rod.
                settings->mLimitsSpringSettings = JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping,
                                                                      std::max(desc.m_spring, 0.0f),
                                                                      std::max(desc.m_damper, 0.0f));
                return settings.GetPtr();
            }
        }

        return nullptr;     // a JointType this switch does not know: the caller logs it

    }   // BuildSettings()

    // Does this type use m_axis at all? Only those are refused a zero axis.
    bool UsesAxis(const JointType& type) {
        return type == JointType::Hinge || type == JointType::Character;
    }

}   // ANONYMOUS NAMESPACE

namespace Crucible {

    using namespace JoltConvert;

    /*!
        \brief
        Joins two bodies - or one body to the world - as they are posed now.
        \param desc
        The type, the bodies, and the world-space anchor and axes.
        \return
        A handle to the joint, or an invalid handle with the reason in the log.
    */
    JointHandle Physics::CreateJoint(const JointDesc& desc) {

        Impl& impl = *m_impl;
        if (!impl.m_initialised) return JointHandle{};

        // CHECKS -------------------------------------------------------------- //
        const bool toWorld = !desc.m_bodyB.IsValid();

        if (!impl.Has(desc.m_bodyA)) {
            CRU_ERROR("Physics::CreateJoint: body A is not a live body");
            return JointHandle{};
        }
        if (!toWorld && !impl.Has(desc.m_bodyB)) {
            CRU_ERROR("Physics::CreateJoint: body B is not a live body - leave it invalid to join A to the world");
            return JointHandle{};
        }
        if (!toWorld && desc.m_bodyA.m_id == desc.m_bodyB.m_id) {
            CRU_ERROR("Physics::CreateJoint: a body cannot be joined to itself");
            return JointHandle{};
        }
        if (!IsFinite(desc.m_anchor) || !IsFinite(desc.m_anchorB) || !IsFinite(desc.m_axis) || !IsFinite(desc.m_swingAxis)) {
            CRU_ERROR("Physics::CreateJoint: an anchor or axis holds NaN or infinity");
            return JointHandle{};
        }
        if (UsesAxis(desc.m_type) && ToJolt(desc.m_axis).Length() < kMinAxisLength) {
            CRU_ERROR("Physics::CreateJoint: the axis is zero length - a hinge or character joint needs a direction");
            return JointHandle{};
        }

        const JPH::Ref<JPH::TwoBodyConstraintSettings> settings = BuildSettings(desc);
        if (settings == nullptr) {
            CRU_ERROR("Physics::CreateJoint: joint type {} is not one Physics knows how to build", static_cast<int>(desc.m_type));
            return JointHandle{};
        }

        // CREATE, ADD, WAKE ---------------------------------------------------- //
        // CreateConstraint locks both bodies while it reads their poses. An
        // invalid BodyID is Jolt's own spelling of "the world".
        //
        // The ORDER matters. Jolt measures a hinge's angle (and a character
        // joint's twist) as body 2 turning relative to body 1. With the joint's
        // own body as body 2, a positive angle means "this body turns the
        // right-hand-rule way around the axis" - the way a designer reads a
        // knee's 0 to 140. The other way round every limit came out mirrored
        // (found by a probe: [0, 90] let a bar fall clockwise, i.e. negatively).
        JPH::TwoBodyConstraint* constraint =
            impl.Bodies().CreateConstraint(settings.GetPtr(), toWorld ? JPH::BodyID() : Impl::ToId(desc.m_bodyB), Impl::ToId(desc.m_bodyA));
        if (constraint == nullptr) {
            CRU_ERROR("Physics::CreateJoint: Jolt could not build the joint");
            return JointHandle{};
        }

        // Unity's two solver knobs, per joint. Jolt runs two kinds of repeated
        // fix-up pass each step, and each cures a different symptom (both
        // measured in testing):
        //   position passes (Unity's solverIterations, Jolt's default 2): pull
        //     joints back to where they belong. Too few and a joint RESTS
        //     slightly wrong under load - a ragdoll's knee lying 2 degrees past
        //     its limit and its limbs 3 cm apart; 8 brought that to 0.17 and 1.7.
        //   velocity passes (Unity's solverVelocityIterations, Jolt's default 10):
        //     stop movement into a limit. Too few and a light body on a long
        //     lever pushes several degrees past it; 40 cut 10.5 to 5.7.
        // Jolt keeps each count in a byte, hence the 255.
        if (desc.m_solverIterations > 0)
            constraint->SetNumPositionStepsOverride(std::min(desc.m_solverIterations, 255u));
        if (desc.m_solverVelocityIterations > 0)
            constraint->SetNumVelocityStepsOverride(std::min(desc.m_solverVelocityIterations, 255u));

        // Added like a body: creating is not enough, the world must be told.
        // Then woken, or two sleeping bodies would ignore their new joint
        // until something bumped them.
        impl.m_world->AddConstraint(constraint);
        impl.Bodies().ActivateConstraint(constraint);

        // A SLOT --------------------------------------------------------------- //
        std::uint32_t index = 0;
        if (!impl.m_freeJointSlots.empty()) {
            index = impl.m_freeJointSlots.back();
            impl.m_freeJointSlots.pop_back();
        }
        else {
            index = static_cast<std::uint32_t>(impl.m_joints.size());
            impl.m_joints.emplace_back();
        }

        Impl::Joint& slot = impl.m_joints[index];
        slot.constraint  = constraint;      // the Ref keeps it alive until RemoveJoint drops it
        slot.type        = desc.m_type;
        slot.bodyA       = desc.m_bodyA.m_id;
        slot.bodyB       = toWorld ? Impl::kNoBody : desc.m_bodyB.m_id;
        slot.breakForce  = std::max(desc.m_breakForce, 0.0f);
        slot.breakTorque = std::max(desc.m_breakTorque, 0.0f);
        slot.userData    = desc.m_userData;
        slot.inUse       = true;

        // Unity's enableCollision = false. Only between two bodies: the world
        // is not a body, so there is nothing to pass through.
        slot.ignoring = !desc.m_collideConnected && !toWorld;
        if (slot.ignoring) impl.m_contactFilter->Ignore(slot.bodyA, slot.bodyB);

        return JointHandle{ index, slot.generation };

    }   // Physics::CreateJoint()

    void Physics::DestroyJoint(const JointHandle& joint) {

        Impl& impl = *m_impl;
        if (impl.Slot(joint) == nullptr) return;     // stale: already gone, perhaps with its body

        impl.RemoveJoint(joint.m_index);

    }   // Physics::DestroyJoint()

    float Physics::GetHingeAngle(const JointHandle& joint) const {

        const Impl::Joint* slot = m_impl->Slot(joint);
        if (slot == nullptr || slot->type != JointType::Hinge) return 0.0f;

        const auto* hinge = static_cast<const JPH::HingeConstraint*>(slot->constraint.GetPtr());
        return JPH::RadiansToDegrees(hinge->GetCurrentAngle());

    }   // Physics::GetHingeAngle()

    std::uint32_t Physics::GetBrokenJointCount() const {
        return static_cast<std::uint32_t>(m_impl->m_brokenJoints.size());
    }

    bool Physics::GetBrokenJoint(std::uint32_t index, JointHandle& outJoint, std::uint64_t& outUserData) const {

        const Impl& impl = *m_impl;
        if (index >= impl.m_brokenJoints.size()) return false;

        outJoint    = impl.m_brokenJoints[index].m_joint;
        outUserData = impl.m_brokenJoints[index].m_userData;
        return true;

    }   // Physics::GetBrokenJoint()

    // ------------------------------------------------------------------- //
    // IMPL: THE SHARED PART
    // ------------------------------------------------------------------- //

    /*!
        \brief
        Takes one joint out of Jolt, wakes its bodies and frees its slot. The
        generation rises, so every handle to it goes stale at once.
    */
    void Physics::Impl::RemoveJoint(std::uint32_t index) {

        Joint& slot = m_joints[index];
        if (!slot.inUse) return;

        m_world->RemoveConstraint(slot.constraint);
        if (slot.ignoring) m_contactFilter->Unignore(slot.bodyA, slot.bodyB);

        // A crate hanging from this joint must fall now, not whenever it next
        // gets nudged. Jolt skips the world (an invalid id) and static bodies.
        const JPH::BodyID bodies[2] = { JPH::BodyID(slot.bodyA),
                                        slot.bodyB == kNoBody ? JPH::BodyID() : JPH::BodyID(slot.bodyB) };
        Bodies().ActivateBodies(bodies, 2);

        slot.constraint = nullptr;      // drops the last reference: Jolt deletes it
        slot.ignoring   = false;
        slot.inUse      = false;
        ++slot.generation;
        m_freeJointSlots.push_back(index);

    }   // Physics::Impl::RemoveJoint()

    // Before a body goes: its joints point at it, and would point at freed
    // memory a moment later.
    void Physics::Impl::RemoveJointsOn(std::uint32_t bodyId) {

        for (std::uint32_t index = 1; index < m_joints.size(); ++index) {
            const Joint& slot = m_joints[index];
            if (slot.inUse && (slot.bodyA == bodyId || slot.bodyB == bodyId)) RemoveJoint(index);
        }

    }   // Physics::Impl::RemoveJointsOn()

    /*!
        \brief
        Unity's breakForce and breakTorque, checked after every Step. Jolt
        does not break joints itself, but it does report how hard each one
        pushed: its "lambda" is the impulse the joint applied during the step,
        so dividing by the step's length gives the force it held.
        \param fixedDt
        The length of the step that just ran.
    */
    void Physics::Impl::BreakJoints(float fixedDt) {

        if (!(fixedDt > 0.0f)) return;

        for (std::uint32_t index = 1; index < m_joints.size(); ++index) {

            Joint& slot = m_joints[index];
            if (!slot.inUse || (slot.breakForce <= 0.0f && slot.breakTorque <= 0.0f)) continue;

            // HOW HARD IT HELD -------------------------------------------------- //
            // Position lambdas are what kept the anchors together (a force);
            // rotation lambdas are what stopped the turning (a torque). Each
            // type reports its own parts, so each is asked for what it has.
            float impulse        = 0.0f;
            float angularImpulse = 0.0f;
            JPH::TwoBodyConstraint* constraint = slot.constraint.GetPtr();

            switch (slot.type) {
                case JointType::Fixed: {
                    const auto* fixed = static_cast<const JPH::FixedConstraint*>(constraint);
                    impulse        = fixed->GetTotalLambdaPosition().Length();
                    angularImpulse = fixed->GetTotalLambdaRotation().Length();
                    break;
                }
                case JointType::Ball: {
                    impulse = static_cast<const JPH::PointConstraint*>(constraint)->GetTotalLambdaPosition().Length();
                    break;
                }
                case JointType::Hinge: {
                    const auto* hinge = static_cast<const JPH::HingeConstraint*>(constraint);
                    const JPH::Vector<2> turning = hinge->GetTotalLambdaRotation();
                    impulse        = hinge->GetTotalLambdaPosition().Length();
                    angularImpulse = std::sqrt(turning[0] * turning[0] + turning[1] * turning[1])
                                   + std::fabs(hinge->GetTotalLambdaRotationLimits());
                    break;
                }
                case JointType::Character: {
                    const auto* swingTwist = static_cast<const JPH::SwingTwistConstraint*>(constraint);
                    const float twist  = swingTwist->GetTotalLambdaTwist();
                    const float swingY = swingTwist->GetTotalLambdaSwingY();
                    const float swingZ = swingTwist->GetTotalLambdaSwingZ();
                    impulse        = swingTwist->GetTotalLambdaPosition().Length();
                    angularImpulse = std::sqrt(twist * twist + swingY * swingY + swingZ * swingZ);
                    break;
                }
                case JointType::Spring: {
                    impulse = std::fabs(static_cast<const JPH::DistanceConstraint*>(constraint)->GetTotalLambdaPosition());
                    break;
                }
            }

            const float force  = impulse / fixedDt;
            const float torque = angularImpulse / fixedDt;

            const bool forceBreaks  = slot.breakForce  > 0.0f && force  > slot.breakForce;
            const bool torqueBreaks = slot.breakTorque > 0.0f && torque > slot.breakTorque;
            if (!forceBreaks && !torqueBreaks) continue;

            // SNAP ---------------------------------------------------------------- //
            // Recorded before removal: removing raises the generation, and the
            // handle handed back must be the one the caller is holding.
            CRU_INFO("Physics: a joint broke - held {:.1f} N (limit {}), {:.1f} N*m (limit {})",
                     force, slot.breakForce, torque, slot.breakTorque);
            m_brokenJoints.push_back(Broken{ JointHandle{ index, slot.generation }, slot.userData });
            RemoveJoint(index);
        }

    }   // Physics::Impl::BreakJoints()

}   // NAMESPACE CRUCIBLE
