/*************************************************************************
  \file       PhysicsJointReflection.cpp
  \author     Hugo Low Ren Hao
  \par        Email: low.h@digipen.edu
  \date       15 October 2026
  \version    1.0
  \brief
    RTTR registration for PhysicsJoint and JointType.
  \details
    Every setting is saved and editable. connectedBody is tagged ENTITY_REF,
    so the Inspector shows "Entity N" with a Hierarchy drop target instead
    of a raw number; the scene file still stores the plain scene id.
    isBroken is runtime state: shown greyed out, never saved, so Stop brings
    every broken joint back. connectedEntity (the code-only link) is not
    registered at all - an Entity handle means nothing in a file.

    One RTTR_REGISTRATION block per .cpp is legal and is the intended way to
    split registration up: the macro defines a fixed-name struct inside an
    anonymous namespace, so each translation unit gets its own copy. What is
    NOT legal is two blocks in the same .cpp - which is what happens if one
    is ever put in a header. Never put RTTR_REGISTRATION in a header.

    Property names are what the scene file uses as JSON keys. Renaming one
    breaks every scene saved with the old name, so treat them as a file
    format, not as code.
  \dependencies
    Reflection/ReflectionRegistration.h, Physics/PhysicsComponents.h
  \see
    ColliderReflection.cpp, ECS/ECSPhysicsSystem.cpp
  \history
    - 1.0 (15 October 2026): First version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/

#include "Reflection/ReflectionRegistration.h"
#include "Physics/PhysicsComponents.h"

RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::enumeration<JointType>("JointType")
    (
        rttr::value("Fixed",     JointType::Fixed),
        rttr::value("Hinge",     JointType::Hinge),
        rttr::value("Ball",      JointType::Ball),
        rttr::value("Character", JointType::Character),
        rttr::value("Spring",    JointType::Spring)
    );

    rttr::registration::class_<PhysicsJoint>("PhysicsJoint")
        .constructor<>()
        .property("type",            &PhysicsJoint::m_type)
        .property("connectedBody",   &PhysicsJoint::m_connectedBody)
            (rttr::metadata(MetaDataTypes::ENTITY_REF, true))
        .property("anchor",          &PhysicsJoint::m_anchor)
        .property("axis",            &PhysicsJoint::m_axis)
        .property("swingAxis",       &PhysicsJoint::m_swingAxis)
        .property("enableCollision", &PhysicsJoint::m_enableCollision)
        .property("useLimits",       &PhysicsJoint::m_useLimits)
        .property("minAngle",        &PhysicsJoint::m_minAngle)
        .property("maxAngle",        &PhysicsJoint::m_maxAngle)
        .property("lowTwistLimit",   &PhysicsJoint::m_lowTwistLimit)
        .property("highTwistLimit",  &PhysicsJoint::m_highTwistLimit)
        .property("swing1Limit",     &PhysicsJoint::m_swing1Limit)
        .property("swing2Limit",     &PhysicsJoint::m_swing2Limit)
        .property("connectedAnchor", &PhysicsJoint::m_connectedAnchor)
        .property("spring",          &PhysicsJoint::m_spring)
        .property("damper",          &PhysicsJoint::m_damper)
        .property("minDistance",     &PhysicsJoint::m_minDistance)
        .property("maxDistance",     &PhysicsJoint::m_maxDistance)
        .property("breakForce",      &PhysicsJoint::m_breakForce)
        .property("breakTorque",     &PhysicsJoint::m_breakTorque)
        .property("solverIterations", &PhysicsJoint::m_solverIterations)
        .property("isBroken",        &PhysicsJoint::m_isBroken)
            (rttr::metadata(MetaDataTypes::DO_NOT_SERIALIZE, true),
             rttr::metadata(MetaDataTypes::DISABLED_ON_IMGUI, true))
    ;
}
