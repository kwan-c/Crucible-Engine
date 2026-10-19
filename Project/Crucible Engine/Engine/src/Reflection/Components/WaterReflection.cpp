/*************************************************************************
  \file       WaterReflection.cpp
  \brief
    RTTR registration for Water (a water surface - see Components.h).
  \details
    One RTTR_REGISTRATION block per .cpp - see SkyboxReflection.cpp for the
    rules. Property names are scene-file JSON keys: treat them as a file
    format, not as code. Registered in the order the Inspector shows them.
  \dependencies
    Reflection/ReflectionRegistration.h, ECS/Components/Components.h
*************************************************************************/

#include "Reflection/ReflectionRegistration.h"
#include "ECS/Components/Components.h"

RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::class_<Water>("Water")
        .constructor<>()
        // Colour and clarity
        .property("shallowColour",  &Water::shallowColour)
            (rttr::metadata(MetaDataTypes::COLOUR, true))
        .property("deepColour",     &Water::deepColour)
            (rttr::metadata(MetaDataTypes::COLOUR, true))
        .property("deepDepth",      &Water::deepDepth)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.1f, 20.0f }))
        .property("clarity",        &Water::clarity)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.1f, 30.0f }))
        .property("refraction",     &Water::refraction)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 1.0f }))
        // Shore foam
        .property("foamColour",     &Water::foamColour)
            (rttr::metadata(MetaDataTypes::COLOUR, true))
        .property("foamWidth",      &Water::foamWidth)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 2.0f }))
        .property("foamBreakup",    &Water::foamBreakup)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 1.0f }))
        // Ripples
        .property("rippleSize",     &Water::rippleSize)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.1f, 10.0f }))
        .property("rippleSpeed",    &Water::rippleSpeed)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 2.0f }))
        .property("rippleStrength", &Water::rippleStrength)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 1.0f }))
        .property("rippleFoam",     &Water::rippleFoam)        // the drawn strokes on open water, on/off
        .property("rippleLines",    &Water::rippleLines)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 1.0f }))
        .property("rippleLineColour", &Water::rippleLineColour)
            (rttr::metadata(MetaDataTypes::COLOUR, true))
        // Light
        .property("glints",         &Water::glints)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 4.0f }))
        .property("glintColour",    &Water::glintColour)
            (rttr::metadata(MetaDataTypes::COLOUR, true))
        .property("reflection",     &Water::reflection)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 1.0f }))
        .property("reflectionColour", &Water::reflectionColour)
            (rttr::metadata(MetaDataTypes::COLOUR, true))
    ;
}
