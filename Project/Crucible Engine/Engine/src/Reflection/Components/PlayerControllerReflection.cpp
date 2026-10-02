/*************************************************************************
  \file       PlayerControllerReflection.cpp
  \author     Hugo
  \date       2 October 2026
  \version    1.0
  \brief
    RTTR registration for PlayerController.
  \details
    Every field is a designer setting, so every field is saved and
    editable in the Inspector (faceCamera shows as a checkbox). There is no runtime state to hide: the
    "what is it doing right now" values (isGrounded, velocity) live on the
    CharacterController next to it, not here.

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
    CharacterControllerReflection.cpp, ECS/ECSPlayerControllerSystem.h
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

    rttr::registration::class_<PlayerController>("PlayerController")
        .constructor<>()
        .property("moveSpeed",  &PlayerController::m_moveSpeed)
        .property("jumpHeight", &PlayerController::m_jumpHeight)
        .property("faceCamera", &PlayerController::m_faceCamera)
    ;
}
