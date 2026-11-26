/*************************************************************************
  \file       NetworkObjectAuthoringReflection.cpp
  \author     Abhishek Akilandeswari Patchamuthu
  \par        Email: a.patchamuthu@digipen.edu
  \date       6 November 2026
  \version    1.3
  \brief
    RTTR registration for NetworkObjectAuthoring, an object's saved
    networking settings (Network/NetComponents.h).
  \details
    One RTTR_REGISTRATION block per .cpp - see SkyboxReflection.cpp for the
    rules. Property names are scene and prefab JSON keys: never rename one
    (a renamed key loads as its default, silently dropping the setting).
    Everything is plain data stored by name - serializer and component
    names, numbers and flags - so the saved form holds no pointer, handle,
    runtime NetworkId or engine-internal number.

    Every field is NOT_ON_IMGUI: the Inspector draws them only as the
    networking policy table (Editor/src/Network/NetworkAuthoringInspector).
  \dependencies
    Reflection/ReflectionRegistration.h, Network/NetComponents.h
  \see
    Network/NetComponents.h
  \history
    - 1.0 (6 November 2026): Initial version (Networking Phase 14.1).
    - 1.1 (6 November 2026): fields hidden from the generic drawer; the
      policy table draws them (Networking Phase 14.3).
    - 1.2 (24 November 2026): syncedChildren.
      Absent from older files: it loads as an empty list.
    - 1.3 (26 November 2026): childEnabledReplicated (on/off below the
      root). Absent from older files: empty.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#include "Reflection/ReflectionRegistration.h"
#include "Network/NetComponents.h"

RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::class_<NetworkObjectAuthoring>("NetworkObjectAuthoring")
        .constructor<>()
        .property("replicated",          &NetworkObjectAuthoring::replicated)
        (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))   // drawn by the policy table
        .property("serverOnly",          &NetworkObjectAuthoring::serverOnly)
        (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))   // drawn by the policy table
        .property("ownerLocalOnly",      &NetworkObjectAuthoring::ownerLocalOnly)
        (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))   // drawn by the policy table
        .property("enabledReplicated",   &NetworkObjectAuthoring::enabledReplicated)
        (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))   // drawn by the policy table
        .property("syncedChildren",      &NetworkObjectAuthoring::syncedChildren)
        (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))   // drawn by the policy table
        .property("childEnabledReplicated", &NetworkObjectAuthoring::childEnabledReplicated)
        (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))   // drawn by the policy table
        .property("sendPriority",        &NetworkObjectAuthoring::sendPriority)
        (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))   // drawn by the policy table
        .property("critical",            &NetworkObjectAuthoring::critical)
        (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))   // drawn by the policy table
        .property("allowScenePlacement", &NetworkObjectAuthoring::allowScenePlacement)
        (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))   // drawn by the policy table
        .property("spawnPerPlayer",      &NetworkObjectAuthoring::spawnPerPlayer)
        (rttr::metadata(MetaDataTypes::NOT_ON_IMGUI, true))   // drawn by the policy table
    ;
}
