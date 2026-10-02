/*************************************************************************
  \file       CharacterControllerReflection.cpp
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       1 October 2026
  \version    1.0
  \brief
    RTTR registration for CharacterController.
  \details
    Settings are saved. Results and step-to-step state are runtime-only:
    tagged DO_NOT_SERIALIZE so a scene never stores "is grounded" or a
    half-finished jump, and DISABLED_ON_IMGUI so the Inspector shows them
    read-only.

    The PhysicsLayer enum used by "layer" is registered in
    ColliderReflection.cpp, not here - see that file for why.

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
    Reflection/MathReflection.cpp, Reflection/Serialization.h, Scene/EntityIO.h
  \history
    - 1.0 (1 October 2026): Split out of the old single registration file
      ReflectionComponentRegistration.cpp, one file per component, following
      the layout of the reference engine.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/

#include "Reflection/ReflectionRegistration.h"
#include "Physics/PhysicsComponents.h"

RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::class_<CharacterController>("CharacterController")
        .constructor<>()
        .property("height",       &CharacterController::m_height)
        .property("radius",       &CharacterController::m_radius)
        .property("center",       &CharacterController::m_center)
        .property("slopeLimit",   &CharacterController::m_slopeLimit)
        .property("stepOffset",   &CharacterController::m_stepOffset)
        .property("pushStrength", &CharacterController::m_pushStrength)
        .property("layer",        &CharacterController::m_layer)
        .property("isGrounded",    &CharacterController::m_isGrounded)
            (rttr::metadata(MetaDataTypes::DO_NOT_SERIALIZE, true),
             rttr::metadata(MetaDataTypes::DISABLED_ON_IMGUI, true))
        .property("velocity",      &CharacterController::m_velocity)
            (rttr::metadata(MetaDataTypes::DO_NOT_SERIALIZE, true),
             rttr::metadata(MetaDataTypes::DISABLED_ON_IMGUI, true))
        .property("moveVelocity",  &CharacterController::m_moveVelocity)
            (rttr::metadata(MetaDataTypes::DO_NOT_SERIALIZE, true),
             rttr::metadata(MetaDataTypes::DISABLED_ON_IMGUI, true))
        .property("verticalSpeed", &CharacterController::m_verticalSpeed)
            (rttr::metadata(MetaDataTypes::DO_NOT_SERIALIZE, true),
             rttr::metadata(MetaDataTypes::DISABLED_ON_IMGUI, true))
        .property("jumpHeight",    &CharacterController::m_jumpHeight)
            (rttr::metadata(MetaDataTypes::DO_NOT_SERIALIZE, true),
             rttr::metadata(MetaDataTypes::DISABLED_ON_IMGUI, true))
    ;
}
