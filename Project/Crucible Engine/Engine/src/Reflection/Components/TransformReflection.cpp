/*************************************************************************
  \file       TransformReflection.cpp
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       1 October 2026
  \version    1.0
  \brief
    RTTR registration for Transform.
  \details
    Position, rotation and scale. All three are local - relative to the
    parent, or to the world for a root - which is why Scene restores the
    hierarchy with worldPositionStays = false.

    One RTTR_REGISTRATION block per .cpp is legal and is the intended way to
    split registration up: the macro defines a fixed-name struct inside an
    anonymous namespace, so each translation unit gets its own copy. What is
    NOT legal is two blocks in the same .cpp - which is what happens if one
    is ever put in a header. Never put RTTR_REGISTRATION in a header.

    Property names are what the scene file uses as JSON keys. Renaming one
    breaks every scene saved with the old name, so treat them as a file
    format, not as code.
  \dependencies
    Reflection/ReflectionRegistration.h, ECS/Components/Components.h
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
#include "ECS/Components/Components.h"

RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::class_<Transform>("Transform")
        .constructor<>()
        .property("positions", &Transform::positions)
        .property("rotation",  &Transform::rotation)
        .property("scale",     &Transform::scale)
    ;
}
