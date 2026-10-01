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
#include "Resources/ResourceManager.h"   // ResourceKind, for ASSET_REF metadata

RTTR_REGISTRATION
{
	using namespace Crucible::Math;

    // -----------------
    // Registration for Math Library
    // -----------------

	// The math library is GLM (see Math/Math.h). Its vector and quaternion
	// components are the plain fields x / y / z / w, so they register the
	// same way any struct does.
	//
	// mat3 and mat4 are deliberately NOT registered. GLM stores a matrix as a
	// private array of columns with no named fields to point RTTR at, and
	// nothing that gets serialised holds one: the only matrix component,
	// WorldTransform, is recomputed every frame and never written to a file.
	// If one is ever needed, register it through getter/setter functions.
	//
	// Older scene files wrote vec4 as w, x, y, z. Keys are matched by name on
	// load, so those files still read correctly.

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

    // -----------------
    // Components (Crucible::)
    // -----------------

    using namespace Crucible;

    rttr::registration::class_<Transform>("Transform")
        .constructor<>()
        .property("positions", &Transform::positions)
        .property("rotation",  &Transform::rotation)
        .property("scale",     &Transform::scale)
    ;

    rttr::registration::class_<MeshRenderer>("MeshRenderer")
        .constructor<>()
        .property("visible",       &MeshRenderer::visible)
        .property("colour",        &MeshRenderer::colour)
            (rttr::metadata(MetaDataTypes::COLOUR, true))
        // Asset ids, not the runtime handles: handles are meaningless after a reload, ids are
        // resolved back to handles by ResourceManager (see MeshRenderer's doc comment).
        .property("meshAsset",     &MeshRenderer::meshAsset)
            (rttr::metadata(MetaDataTypes::ASSET_REF, ResourceKind::Mesh))
        .property("materialAsset", &MeshRenderer::materialAsset)
            (rttr::metadata(MetaDataTypes::ASSET_REF, ResourceKind::Material))
    ;

    // Light type by name, same reasoning as ShapeType below: the scene file
    // says "Spot", not 2.
    rttr::registration::enumeration<LightType>("LightType")
    (
        rttr::value("Directional", LightType::Directional),
        rttr::value("Point",       LightType::Point),
        rttr::value("Spot",        LightType::Spot)
    );

    rttr::registration::class_<Light>("Light")
        .constructor<>()
        .property("type",         &Light::type)
        .property("colour",       &Light::colour)
            (rttr::metadata(MetaDataTypes::COLOUR, true))
        .property("intensity",    &Light::intensity)
        .property("range",        &Light::range)
        .property("innerConeDeg", &Light::innerConeDeg)
        .property("outerConeDeg", &Light::outerConeDeg)
        .property("enabled",      &Light::enabled)
        .property("castShadows",  &Light::castShadows)
    ;

    // Skybox::cubemap is a runtime GPU handle (meaningless after a reload),
    // so like MeshRenderer::mesh it is not registered; skyboxAsset (the
    // asset id, resolved by ResourceManager) is what gets saved.
    rttr::registration::class_<Skybox>("Skybox")
        .constructor<>()
        .property("visible",          &Skybox::visible)
        .property("intensity",        &Skybox::intensity)
        .property("ambientSky",       &Skybox::ambientSky)
            (rttr::metadata(MetaDataTypes::COLOUR, true))
        .property("ambientGround",    &Skybox::ambientGround)
            (rttr::metadata(MetaDataTypes::COLOUR, true))
        .property("ambientIntensity", &Skybox::ambientIntensity)
        .property("exposure",         &Skybox::exposure)
        .property("skyboxAsset",      &Skybox::skyboxAsset)
            (rttr::metadata(MetaDataTypes::ASSET_REF, ResourceKind::Texture))
    ;

    rttr::registration::class_<AudioSource>("AudioSource")
        .constructor<>()
        .property("play", &AudioSource::play)
        .property("loop", &AudioSource::loop)
    ;

    // Nested in RigidBody, so the Inspector shows it as a "constraints" tree and the
    // scene file as a "constraints": { ... } object.
    rttr::registration::class_<RigidBodyConstraints>("RigidBodyConstraints")
        .constructor<>()
        .property("freezePositionX", &RigidBodyConstraints::freezePositionX)
        .property("freezePositionY", &RigidBodyConstraints::freezePositionY)
        .property("freezePositionZ", &RigidBodyConstraints::freezePositionZ)
        .property("freezeRotationX", &RigidBodyConstraints::freezeRotationX)
        .property("freezeRotationY", &RigidBodyConstraints::freezeRotationY)
        .property("freezeRotationZ", &RigidBodyConstraints::freezeRotationZ)
    ;

    rttr::registration::class_<RigidBody>("RigidBody")
        .constructor<>()
        .property("velocity",    &RigidBody::velocity)
        .property("mass",        &RigidBody::mass)
        .property("useGravity",  &RigidBody::useGravity)
        .property("drag",        &RigidBody::drag)
        .property("angularDrag", &RigidBody::angularDrag)
        .property("constraints", &RigidBody::constraints)
    ;

    // -----------------
    // Physics (Physics/PhysicsComponents.h)
    // -----------------
    //
    // Enums are registered by name so the scene file says "Capsule" and
    // "Player", not 2 and 2. Reordering an enum then cannot silently change
    // what an existing scene means. PhysicsLayer::Count is deliberately left
    // out - it is a table size, not a layer anyone should pick.

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
    ;

    // Settings are saved. Results and step-to-step state are runtime-only:
    // tagged DO_NOT_SERIALIZE so the Inspector can still show them, but a
    // scene never stores "is grounded" or a half-finished jump.
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

    // HierarchyNode and WorldTransform are intentionally NOT registered for
    // serialization: EntityIO skips them. The tree is saved by Scene as guid
    // references, and WorldTransform is recomputed every frame.
}
