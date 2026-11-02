/*************************************************************************
  \file       ColliderReflection.cpp
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       1 October 2026
  \version    1.0
  \brief
    RTTR registration for Collider, ShapeType and PhysicsLayer.
  \details
    Both enums are registered by name, so the scene file says "Capsule" and
    "Player" rather than 2 and 2.

    PhysicsLayer is registered HERE and only here, even though
    CharacterController uses it too. RTTR's registry is global, so
    CharacterControllerReflection.cpp picks it up from this file at
    runtime. Registering the same enum in two files would register it
    twice. PhysicsLayer::Count is left out on purpose - it sizes Jolt's
    tables and is not a layer anyone should pick.

    Property names drop the m_ prefix ("type", not "m_type"). Changing them
    now would break every scene that already contains a Collider.

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

    rttr::registration::enumeration<ShapeType>("ShapeType")
    (
        rttr::value("Box",     ShapeType::Box),
        rttr::value("Sphere",  ShapeType::Sphere),
        rttr::value("Capsule", ShapeType::Capsule)
    );

    rttr::registration::enumeration<PhysicsLayer>("PhysicsLayer")
    (
        rttr::value("NonMoving",  PhysicsLayer::NonMoving),
        rttr::value("Moving",     PhysicsLayer::Moving),
        rttr::value("Player",     PhysicsLayer::Player),
        rttr::value("Enemy",      PhysicsLayer::Enemy),
        rttr::value("Projectile", PhysicsLayer::Projectile)
    );

    rttr::registration::class_<Collider>("Collider")
        .constructor<>()
        .property("type",       &Collider::m_type)
        .property("size",       &Collider::m_size)
        .property("radius",     &Collider::m_radius)
        .property("height",     &Collider::m_height)
        .property("center",     &Collider::m_center)
        .property("friction",   &Collider::m_friction)
        .property("bounciness", &Collider::m_bounciness)
        .property("layer",      &Collider::m_layer)
        .property("isTrigger",  &Collider::m_isTrigger)   // scenes saved before it load as false
    ;
}
