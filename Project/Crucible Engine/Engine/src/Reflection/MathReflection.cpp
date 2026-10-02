/*************************************************************************
  \file       MathReflection.cpp
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       1 October 2026
  \version    1.0
  \brief
    RTTR registration for the math types (GLM).
  \details
    Registers vec2, vec3, vec4 and quat so that any component holding
    one is serialised field by field instead of as an empty {}.

    mat3 and mat4 are deliberately NOT registered. GLM stores a matrix
    as a private array of columns with no named fields to point RTTR
    at, and nothing that gets serialised holds one: the only matrix
    component, WorldTransform, is recomputed every frame and never
    written to a file. If one is ever needed, register it through
    getter/setter functions.

    Older scene files wrote vec4 as w, x, y, z. Keys are matched by
    name on load, so those files still read correctly.

    Whenever Math/Math.h changes library, this file must change in the
    same commit, or every vector silently saves as {}.

    One RTTR_REGISTRATION block per .cpp is legal and is the intended way to
    split registration up: the macro defines a fixed-name struct inside an
    anonymous namespace, so each translation unit gets its own copy. What is
    NOT legal is two blocks in the same .cpp - which is what happens if one
    is ever put in a header. Never put RTTR_REGISTRATION in a header.

    Property names are what the scene file uses as JSON keys. Renaming one
    breaks every scene saved with the old name, so treat them as a file
    format, not as code.
  \dependencies
    Reflection/ReflectionExternalRegistration.h, Math/Math.h
  \see
    Math/Math.h, Reflection/Serialization.h
  \history
    - 1.0 (1 October 2026): Split out of the old single registration file
      ReflectionComponentRegistration.cpp, one file per component, following
      the layout of the reference engine.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/

// Kept for the template_type_trait hooks it holds for the math types (all
// commented out since the switch to GLM, but this is where they live).
#include "Reflection/ReflectionExternalRegistration.h"
#include "Math/Math.h"

RTTR_REGISTRATION
{
    using namespace Crucible::Math;

    rttr::registration::class_<vec2>("vec2")
        .property("x", &vec2::x)
        .property("y", &vec2::y)
    ;

    rttr::registration::class_<vec3>("vec3")
        .property("x", &vec3::x)
        .property("y", &vec3::y)
        .property("z", &vec3::z)
    ;

    rttr::registration::class_<vec4>("vec4")
        .property("x", &vec4::x)
        .property("y", &vec4::y)
        .property("z", &vec4::z)
        .property("w", &vec4::w)
    ;

    rttr::registration::class_<quat>("quat")
        .property("x", &quat::x)
        .property("y", &quat::y)
        .property("z", &quat::z)
        .property("w", &quat::w)
    ;
}
