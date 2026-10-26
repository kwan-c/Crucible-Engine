/*************************************************************************
  \file       PrefabInstanceReflection.cpp
  \author     Tan Chun Kwang Xavier
  \par        Email: chunkwangxavier.tan@digipen.edu
  \date       25 October 2026
  \version    1.0
  \brief
    RTTR registration for PrefabInstance, the link from an entity to the
    .cprefab it was placed from.
  \details
    One RTTR_REGISTRATION block per .cpp - see SkyboxReflection.cpp for the
    rules. Property names are scene-file JSON keys. Saved in scenes, undo
    snapshots and the clipboard like any other component; Prefab::SaveFrom
    strips it from .cprefab files, so a template never links to anything.
    The Inspector never lists it (it shows the prefab bar instead), so no
    property needs NOT_ON_IMGUI.
  \dependencies
    Reflection/ReflectionRegistration.h, ECS/Components/Components.h
  \see
    Scene/Prefab.h, Editor/src/Prefabs/PrefabAuthoring.h
  \history
    - 1.0 (25 October 2026): Initial version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/

#include "Reflection/ReflectionRegistration.h"
#include "ECS/Components/Components.h"

RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::class_<PrefabInstance>("PrefabInstance")
        .constructor<>()
        .property("prefabAsset", &PrefabInstance::prefabAsset)
        .property("fileId",      &PrefabInstance::fileId)
        .property("isRoot",      &PrefabInstance::isRoot)
        .property("prefabHash",  &PrefabInstance::prefabHash)
        .property("overrides",   &PrefabInstance::overrides)
    ;
}
