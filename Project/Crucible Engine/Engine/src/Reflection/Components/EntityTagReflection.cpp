/*************************************************************************
  \file       EntityTagReflection.cpp
  \author     Abhishek Akilandeswari Patchamuthu
  \par        Email: a.patchamuthu@digipen.edu
  \date       13 November 2026
  \version    1.0
  \brief
    RTTR registration for EntityTag, the mandatory gameplay tag.
  \details
    One RTTR_REGISTRATION block per .cpp - see SkyboxReflection.cpp for the
    rules. Property names are scene-file JSON keys:
        "EntityTag": { "tag": "Obstacle" }
    A scene or prefab saved before EntityTag existed has no entry for it:
    such entities keep the default, "Untagged". A saved tag the project no
    longer defines is loaded as it is and reported (World::NormalizeTag).
  \dependencies
    Reflection/ReflectionRegistration.h, ECS/Components/Components.h
  \see
    ECS/World.h (entity tags), ECS/TagRegistry.h, Scene/EntityIO.h
  \history
    - 1.0 (13 November 2026): Initial version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/

#include "Reflection/ReflectionRegistration.h"
#include "ECS/Components/Components.h"

RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::class_<EntityTag>("EntityTag")
        .constructor<>()
        .property("tag", &EntityTag::tag)
    ;
}
