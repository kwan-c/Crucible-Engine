/*************************************************************************
  \file       CameraReflection.cpp
  \brief
    RTTR registration for CameraComponent (shown and saved as "Camera").
  \details
    One RTTR_REGISTRATION block per .cpp - see SkyboxReflection.cpp for the
    rules. Property names are scene-file JSON keys: treat them as a file
    format, not as code.
  \dependencies
    Reflection/ReflectionRegistration.h, ECS/Components/Components.h
*************************************************************************/

#include "Reflection/ReflectionRegistration.h"
#include "ECS/Components/Components.h"

RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::class_<CameraComponent>("Camera")
        .constructor<>()
        .property("enabled",  &CameraComponent::enabled)
        .property("fovY",     &CameraComponent::fovY)
        .property("nearZ",    &CameraComponent::nearZ)
        .property("farZ",     &CameraComponent::farZ)
        .property("priority", &CameraComponent::priority)
    ;
}
