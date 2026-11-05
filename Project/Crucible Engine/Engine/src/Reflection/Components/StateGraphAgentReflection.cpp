/*************************************************************************
  \file       StateGraphAgentReflection.cpp
  \author     Hugo Low Ren Hao
  \par        Email: low.h@digipen.edu
  \date       3 November 2026
  \version    1.0
  \brief
    RTTR registration for StateGraphAgent.
  \details
    "graph" is tagged ASSET_REF with ResourceKind::StateGraph, so the
    Inspector draws a picker listing every .stategraph instead of a raw id.

    Property names are what the scene file uses as JSON keys, so treat them
    as a file format. One RTTR_REGISTRATION block per .cpp, never in a header
    (see CameraOrbitReflection.cpp for why).
  \dependencies
    Reflection/ReflectionRegistration.h, AI/AIComponents.h
  \see
    ECS/ECSStateGraphSystem.h
  \history
    - 1.0 (3 November 2026): First version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/

#include "Reflection/ReflectionRegistration.h"
#include "AI/AIComponents.h"
#include "Resources/ResourceManager.h"   // ResourceKind, for ASSET_REF metadata

RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::class_<StateGraphAgent>("StateGraphAgent")
        .constructor<>()
        .property("graph", &StateGraphAgent::m_graph)
        (
            rttr::metadata(MetaDataTypes::ASSET_REF, ResourceKind::StateGraph)
        )
        .property("logStateChanges", &StateGraphAgent::m_logStateChanges)
    ;
}
