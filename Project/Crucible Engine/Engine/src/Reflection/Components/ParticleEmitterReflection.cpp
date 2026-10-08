/*************************************************************************
  \file       ParticleEmitterReflection.cpp
  \brief
    RTTR registration for ParticleEmitter and its two enums.
  \details
    One RTTR_REGISTRATION block per .cpp - see SkyboxReflection.cpp for the
    rules. Property names are scene-file JSON keys: treat them as a file
    format, not as code. Enums are registered by name so the scene file
    stores "Additive", not 0.
  \dependencies
    Reflection/ReflectionRegistration.h, ECS/Components/Components.h, Resources/ResourceManager.h
*************************************************************************/

#include "Reflection/ReflectionRegistration.h"
#include "ECS/Components/Components.h"
#include "Resources/ResourceManager.h"   // ResourceKind, for ASSET_REF metadata

RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::enumeration<ParticleShape>("ParticleShape")
    (
        rttr::value("Point",  ParticleShape::Point),
        rttr::value("Sphere", ParticleShape::Sphere),
        rttr::value("Box",    ParticleShape::Box)
    );

    rttr::registration::enumeration<ParticleBlend>("ParticleBlend")
    (
        rttr::value("Additive", ParticleBlend::Additive),
        rttr::value("Alpha",    ParticleBlend::Alpha)
    );

    rttr::registration::class_<ParticleEmitter>("ParticleEmitter")
        .constructor<>()
        .property("playing",        &ParticleEmitter::playing)
        .property("loop",           &ParticleEmitter::loop)
        .property("duration",       &ParticleEmitter::duration)
        .property("rate",           &ParticleEmitter::rate)
        .property("burstCount",     &ParticleEmitter::burstCount)
        .property("maxParticles",   &ParticleEmitter::maxParticles)
        .property("shape",          &ParticleEmitter::shape)
        .property("shapeRadius",    &ParticleEmitter::shapeRadius)
        .property("boxExtents",     &ParticleEmitter::boxExtents)
        .property("spreadDeg",      &ParticleEmitter::spreadDeg)
        .property("worldSpace",     &ParticleEmitter::worldSpace)
        .property("lifetimeMin",    &ParticleEmitter::lifetimeMin)
        .property("lifetimeMax",    &ParticleEmitter::lifetimeMax)
        .property("speedMin",       &ParticleEmitter::speedMin)
        .property("speedMax",       &ParticleEmitter::speedMax)
        .property("sizeMin",        &ParticleEmitter::sizeMin)
        .property("sizeMax",        &ParticleEmitter::sizeMax)
        .property("endSizeScale",   &ParticleEmitter::endSizeScale)
        .property("gravityScale",   &ParticleEmitter::gravityScale)
        .property("drag",           &ParticleEmitter::drag)
        .property("spinDeg",        &ParticleEmitter::spinDeg)
        .property("stretch",        &ParticleEmitter::stretch)
        .property("colourStart",    &ParticleEmitter::colourStart)
            (rttr::metadata(MetaDataTypes::COLOUR, true))
        .property("colourEnd",      &ParticleEmitter::colourEnd)
            (rttr::metadata(MetaDataTypes::COLOUR, true))
        .property("alphaStart",     &ParticleEmitter::alphaStart)
        .property("alphaEnd",       &ParticleEmitter::alphaEnd)
        .property("brightness",     &ParticleEmitter::brightness)
        .property("blend",          &ParticleEmitter::blend)
        .property("textureAsset",   &ParticleEmitter::textureAsset)
            (rttr::metadata(MetaDataTypes::ASSET_REF, ResourceKind::Texture))
        .property("flipbookCols",   &ParticleEmitter::flipbookCols)
        .property("flipbookRows",   &ParticleEmitter::flipbookRows)
        .property("emitLight",      &ParticleEmitter::emitLight)
        .property("lightColour",    &ParticleEmitter::lightColour)
            (rttr::metadata(MetaDataTypes::COLOUR, true))
        .property("lightIntensity", &ParticleEmitter::lightIntensity)
        .property("lightRange",     &ParticleEmitter::lightRange)
        .property("seed",           &ParticleEmitter::seed)
    ;
}
