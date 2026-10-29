/*************************************************************************
  \file       ConstantForceReflection.cpp
  \author     Hugo Low Ren Hao
  \par        Email: low.h@digipen.edu
  \date       10 October 2026
  \version    1.0
  \brief
    RTTR registration for ConstantForce.
  \details
    All four fields are settings a designer types in, so all four are saved
    and editable. Nothing here is runtime state: ECSPhysicsSystem only reads
    the component, it never writes it back.

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
    RigidBodyReflection.cpp, ECS/ECSPhysicsSystem.cpp
  \history
    - 1.0 (10 October 2026): First version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/

#include "Reflection/ReflectionRegistration.h"
#include "Physics/PhysicsComponents.h"

RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::class_<ConstantForce>("ConstantForce")
        .constructor<>()
        .property("force",          &ConstantForce::m_force)
        .property("relativeForce",  &ConstantForce::m_relativeForce)
        .property("torque",         &ConstantForce::m_torque)
        .property("relativeTorque", &ConstantForce::m_relativeTorque)
    ;
}
