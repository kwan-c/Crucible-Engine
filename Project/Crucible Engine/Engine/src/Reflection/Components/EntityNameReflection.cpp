/*************************************************************************
  \file       EntityNameReflection.cpp
  \author     Abhishek Akilandeswari Patchamuthu
  \par        Email: a.patchamuthu@digipen.edu
  \date       17 October 2026
  \version    1.0
  \brief
    RTTR registration for EntityName, the mandatory display name.
  \details
    One RTTR_REGISTRATION block per .cpp - see SkyboxReflection.cpp for the
    rules. Property names are scene-file JSON keys. A scene saved before
    EntityName existed has no entry for it: such entities get a numbered
    default once the load ends, after the highest "Entity N" in the file
    (World::DeferredNaming, used by SceneManager).
  \dependencies
    Reflection/ReflectionRegistration.h, ECS/Components/Components.h
  \see
    ECS/World.h (entity names), Scene/EntityIO.h
  \history
    - 1.0 (17 October 2026): Initial version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/

#include "Reflection/ReflectionRegistration.h"
#include "ECS/Components/Components.h"

RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::class_<EntityName>("EntityName")
        .constructor<>()
        .property("name", &EntityName::name)
    ;
}
