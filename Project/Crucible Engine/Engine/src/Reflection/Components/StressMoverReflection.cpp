/*************************************************************************
  \file       StressMoverReflection.cpp
  \brief
    RTTR registration for StressMover (TEMPORARY - load testing only).
  \details
    One RTTR_REGISTRATION block per .cpp - see SkyboxReflection.cpp for the
    rules. Property names are scene-file JSON keys.
  \dependencies
    Reflection/ReflectionRegistration.h, ECS/Components/Components.h
*************************************************************************/

#include "Reflection/ReflectionRegistration.h"
#include "ECS/Components/Components.h"

RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::class_<StressMover>("StressMover")
        .constructor<>()
        .property("origin", &StressMover::origin)
        .property("radius", &StressMover::radius)
        .property("speed",  &StressMover::speed)
        .property("phase",  &StressMover::phase)
    ;
}
