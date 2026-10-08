/*************************************************************************
  \file       SkyboxReflection.cpp
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       1 October 2026
  \version    1.0
  \brief
    RTTR registration for Skybox.
  \details
    Skybox::cubemap is a runtime GPU handle, meaningless after a reload, so
    like MeshRenderer::mesh it is not registered. skyboxAsset - the asset id
    ResourceManager resolves - is what gets saved.

    One RTTR_REGISTRATION block per .cpp is legal and is the intended way to
    split registration up: the macro defines a fixed-name struct inside an
    anonymous namespace, so each translation unit gets its own copy. What is
    NOT legal is two blocks in the same .cpp - which is what happens if one
    is ever put in a header. Never put RTTR_REGISTRATION in a header.

    Property names are what the scene file uses as JSON keys. Renaming one
    breaks every scene saved with the old name, so treat them as a file
    format, not as code.
  \dependencies
    Reflection/ReflectionRegistration.h, ECS/Components/Components.h, Resources/ResourceManager.h
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
#include "Resources/ResourceManager.h"   // ResourceKind, for ASSET_REF metadata

RTTR_REGISTRATION
{
    using namespace Crucible;

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
        .property("ambientFromSky",   &Skybox::ambientFromSky)
        .property("fogDensity",       &Skybox::fogDensity)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 0.2f }))
        .property("fogColour",        &Skybox::fogColour)
            (rttr::metadata(MetaDataTypes::COLOUR, true))
        .property("fogHeightFalloff", &Skybox::fogHeightFalloff)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 1.0f }))
        .property("fogBaseHeight",    &Skybox::fogBaseHeight)
        .property("fogStart",         &Skybox::fogStart)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 100.0f }))
        .property("fogMaxOpacity",    &Skybox::fogMaxOpacity)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 1.0f }))
        .property("contrast",            &Skybox::contrast)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.5f, 1.5f }))
        .property("saturation",          &Skybox::saturation)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 2.0f }))
        .property("temperature",         &Skybox::temperature)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ -1.0f, 1.0f }))
        .property("colourFilter",        &Skybox::colourFilter)
            (rttr::metadata(MetaDataTypes::COLOUR, true))
        .property("vignetteIntensity",   &Skybox::vignetteIntensity)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 1.0f }))
        .property("vignetteSoftness",    &Skybox::vignetteSoftness)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 1.0f }))
        .property("filmGrain",           &Skybox::filmGrain)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 1.0f }))
        .property("chromaticAberration", &Skybox::chromaticAberration)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 1.0f }))
        .property("skyboxAsset",      &Skybox::skyboxAsset)
            (rttr::metadata(MetaDataTypes::ASSET_REF, ResourceKind::Texture))
    ;
}
