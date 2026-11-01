/*************************************************************************
  \file       VineReflection.cpp
  \brief
    RTTR registration for Vine (proof of concept) and VineLetGo.
  \details
    One RTTR_REGISTRATION block per .cpp - see SkyboxReflection.cpp for the
    rules. Property names are scene-file JSON keys: treat them as a file
    format, not as code. Registered in the order the Inspector shows them.
  \dependencies
    Reflection/ReflectionRegistration.h, ECS/Components/Components.h, Resources/ResourceManager.h
*************************************************************************/

#include "Reflection/ReflectionRegistration.h"
#include "ECS/Components/Components.h"
#include "Resources/ResourceManager.h"   // ResourceKind, for ASSET_REF metadata

RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::enumeration<VineLetGo>("VineLetGo")
    (
        rttr::value("Unwind", VineLetGo::Unwind),
        rttr::value("Snap",   VineLetGo::Snap),
        rttr::value("Slide",  VineLetGo::Slide)
    );

    rttr::registration::class_<Vine>("Vine")
        .constructor<>()
        // Behaviour
        .property("attached",          &Vine::attached)
        .property("targetChild",       &Vine::targetChild)
        .property("letGo",             &Vine::letGo)
        .property("startGrown",        &Vine::startGrown)
        .property("growTime",          &Vine::growTime)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.1f, 5.0f }))
        .property("unwindTime",        &Vine::unwindTime)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.1f, 5.0f }))
        .property("witherTime",        &Vine::witherTime)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.1f, 5.0f }))
        // Shape
        .property("strands",           &Vine::strands)
        .property("rootOffset",        &Vine::rootOffset)
        .property("rootRadius",        &Vine::rootRadius)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 0.5f }))
        .property("arch",              &Vine::arch)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ -0.3f, 0.5f }))
        .property("wrapFrom",          &Vine::wrapFrom)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 1.0f }))
        .property("wrapTo",            &Vine::wrapTo)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 1.0f }))
        .property("wrapRadius",        &Vine::wrapRadius)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 0.5f }))
        .property("turns",             &Vine::turns)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ -8.0f, 8.0f }))
        .property("tendril",           &Vine::tendril)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 0.5f }))
        .property("thickness",         &Vine::thickness)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.002f, 0.08f }))
        .property("tipThickness",      &Vine::tipThickness)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 1.0f }))
        .property("variation",         &Vine::variation)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 1.0f }))
        // Motion
        .property("cling",             &Vine::cling)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 1.0f, 100.0f }))
        .property("slack",             &Vine::slack)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 1.0f }))
        .property("inertia",           &Vine::inertia)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 1.0f }))
        .property("gravity",           &Vine::gravity)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 2.0f }))
        .property("sway",              &Vine::sway)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 0.1f }))
        // Leaves
        .property("leafSpacing",       &Vine::leafSpacing)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 0.5f }))
        .property("leafSize",          &Vine::leafSize)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.01f, 0.3f }))
        .property("leafDroop",         &Vine::leafDroop)
            (rttr::metadata(MetaDataTypes::SLIDER, Math::vec2{ 0.0f, 1.0f }))
        // Look
        .property("rootColour",        &Vine::rootColour)
            (rttr::metadata(MetaDataTypes::COLOUR, true))
        .property("tipColour",         &Vine::tipColour)
            (rttr::metadata(MetaDataTypes::COLOUR, true))
        .property("leafColour",        &Vine::leafColour)
            (rttr::metadata(MetaDataTypes::COLOUR, true))
        .property("witherColour",      &Vine::witherColour)
            (rttr::metadata(MetaDataTypes::COLOUR, true))
        .property("materialAsset",     &Vine::materialAsset)
            (rttr::metadata(MetaDataTypes::ASSET_REF, ResourceKind::Material))
        .property("leafMaterialAsset", &Vine::leafMaterialAsset)
            (rttr::metadata(MetaDataTypes::ASSET_REF, ResourceKind::Material))
        .property("seed",              &Vine::seed)
    ;
}
