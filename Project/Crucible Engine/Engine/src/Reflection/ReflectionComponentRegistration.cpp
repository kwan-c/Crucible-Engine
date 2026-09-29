/*************************************************************************
  \file       ReflectionComponentRegistration.cpp
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       29 September 2026
  \version    1.1
  \brief
    The engine's single RTTR registration block.
  \details
    Serialization.cpp names no concrete type - it only walks whatever RTTR
    reports at runtime. A type is invisible to the serializer, and to the
    Inspector, until it is listed here. This file is that list.

    It must be a .cpp, and there must be only one of it. RTTR_REGISTRATION
    expands to a struct and a function with fixed names in an anonymous
    namespace, so a second one in the same translation unit is a
    redefinition error. That is why the math registrations, which used to
    live inside ReflectionExternalRegistration.h, were moved down here
    rather than being included alongside the component ones.

    Keeping them in the Engine DLL rather than the Editor also means
    Game.exe gets them. Previously only the Editor did, so a Transform
    serialised from the game would have written "positions": {}.

    Adding a component: define the struct in Components.h, then add one
    registration block below. Nothing else in the engine changes.
  \dependencies
    ReflectionExternalRegistration.h, ECS/Components/Components.h
  \see
    Reflection/Serialization.h, ECS/Components/Components.h
  \history
    - 1.0 (29 September 2026): Initial version.
    - 1.1 (29 September 2026): Absorbed the xmath registrations from
      ReflectionExternalRegistration.h, which could not legally coexist
      with a second RTTR_REGISTRATION block.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/

// Brings in the template_type_trait specialisations for the xmath types.
// The registration block that used to follow them in that header is now
// part of the single block below.
#include "Reflection/ReflectionExternalRegistration.h"

#include "ECS/Components/Components.h"

RTTR_REGISTRATION
{
	using namespace Crucible::Math;

    // -----------------
    // Registration for Math Library
    // -----------------

	rttr::registration::class_<vec2>("vec2")
		.property("x", &vec2::m_X)
		.property("y", &vec2::m_Y)
	;
	
	rttr::registration::class_<vec3>("vec3")
		.property("x", &vec3::m_X)
		.property("y", &vec3::m_Y)
		.property("z", &vec3::m_Z)
	;
	
	rttr::registration::class_<vec4>("vec4")
		.property("w", &vec4::m_W)
		.property("x", &vec4::m_X)
		.property("y", &vec4::m_Y)
		.property("z", &vec4::m_Z)
	;
	
    rttr::registration::class_<mat3>("mat3")
        .property("m00", &mat3::m_00)
        .property("m10", &mat3::m_10)
        .property("m20", &mat3::m_20)
        .property("m01", &mat3::m_01)
        .property("m11", &mat3::m_11)
        .property("m21", &mat3::m_21)
        .property("m02", &mat3::m_02)
        .property("m12", &mat3::m_12)
        .property("m22", &mat3::m_22)
    ;
	
	rttr::registration::class_<mat4>("mat4")
        .property("m00", &mat4::m_00)
        .property("m10", &mat4::m_10)
        .property("m20", &mat4::m_20)
        .property("m30", &mat4::m_30)
        .property("m01", &mat4::m_01)
        .property("m11", &mat4::m_11)
        .property("m21", &mat4::m_21)
        .property("m31", &mat4::m_31)
        .property("m02", &mat4::m_02)
        .property("m12", &mat4::m_12)
        .property("m22", &mat4::m_22)
        .property("m32", &mat4::m_32)
	;
	
    rttr::registration::class_<quat>("quat")
        .property("x", &quat::m_X)
        .property("y", &quat::m_Y)
        .property("z", &quat::m_Z)
        .property("w", &quat::m_W)
    ;

    // -----------------
    // Components (Crucible::)
    // -----------------

    using namespace Crucible;

    rttr::registration::class_<Transform>("Transform")
        .constructor<>()
        .property("positions", &Transform::positions)
    ;

    rttr::registration::class_<MeshRenderer>("MeshRenderer")
        .constructor<>()
        .property("visible", &MeshRenderer::visible)
        .property("colour",  &MeshRenderer::colour)
    ;

    rttr::registration::class_<Audio>("Audio")
        .constructor<>()
        .property("play", &Audio::play)
        .property("loop", &Audio::loop)
    ;

    rttr::registration::class_<RigidBody>("RigidBody")
        .constructor<>()
        .property("velocity", &RigidBody::velocity)
        .property("mass",     &RigidBody::mass)
    ;
}
