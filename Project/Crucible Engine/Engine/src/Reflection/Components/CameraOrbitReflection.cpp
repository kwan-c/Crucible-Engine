/*************************************************************************
  \file       CameraOrbitReflection.cpp
  \author     Hugo Low Ren Hao
  \par        Email: low.h@digipen.edu
  \date       2 October 2026
  \version    1.0
  \brief
    RTTR registration for CameraOrbit.
  \details
    Every field is saved and editable, the current angles included: yaw and
    pitch as they stand in the editor are the view the game starts with, so
    they are a setting as much as they are state. During Play the system
    rewrites them as the mouse moves, and Stop puts the saved ones back.

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
    PlayerControllerReflection.cpp, ECS/ECSPlayerControllerSystem.h
  \history
    - 1.0 (2 October 2026): First version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/

#include "Reflection/ReflectionRegistration.h"
#include "Physics/PhysicsComponents.h"

RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::class_<CameraOrbit>("CameraOrbit")
        .constructor<>()
        .property("sensitivity", &CameraOrbit::m_sensitivity)
        .property("minPitch",    &CameraOrbit::m_minPitch)
        .property("maxPitch",    &CameraOrbit::m_maxPitch)
        .property("yaw",         &CameraOrbit::m_yaw)
        .property("pitch",       &CameraOrbit::m_pitch)
    ;
}
