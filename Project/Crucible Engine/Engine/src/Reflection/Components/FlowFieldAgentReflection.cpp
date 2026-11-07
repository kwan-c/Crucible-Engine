/*************************************************************************
  \file       FlowFieldAgentReflection.cpp
  \author     Hugo Low Ren Hao
  \par        Email: low.h@digipen.edu
  \date       1 November 2026
  \version    1.1
  \brief
    RTTR registration for FlowFieldAgent and AgentStart.
  \details
    Only the settings are registered. The runtime state (m_movement,
    m_destination, m_target, m_velocity, m_remainingDistance, m_arrived,
    m_started) is left out on purpose: it is never saved and
    never shown, so a scene always starts with agents standing still until
    m_startWith or a behaviour tells them otherwise.

    Property and enum value names are what the scene file uses, so treat
    them as a file format. One RTTR_REGISTRATION block per .cpp, never in a
    header (see CameraOrbitReflection.cpp for why).
  \dependencies
    Reflection/ReflectionRegistration.h, Navigation/NavigationComponents.h
  \see
    ECS/ECSNavigationSystem.h
  \history
    - 1.0 (1 November 2026): First version.
    - 1.1 (6 November 2026): avoidancePriority replaces separationRadius and
      separationWeight. Scenes that still have those two keys load fine: a
      key with no property is skipped, and avoidancePriority starts at 50.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/

#include "Reflection/ReflectionRegistration.h"
#include "Navigation/NavigationComponents.h"

RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::enumeration<AgentStart>("AgentStart")
    (
        rttr::value("Nothing",     AgentStart::Nothing),
        rttr::value("FollowField", AgentStart::FollowField),
        rttr::value("ArrivePlayer", AgentStart::ArrivePlayer)
    );

    rttr::registration::class_<FlowFieldAgent>("FlowFieldAgent")
        .constructor<>()
        .property("speed",        &FlowFieldAgent::m_speed)
        .property("acceleration", &FlowFieldAgent::m_acceleration)
        .property("angularSpeed", &FlowFieldAgent::m_angularSpeed)
        .property("baseOffset",   &FlowFieldAgent::m_baseOffset)
        .property("startWith",    &FlowFieldAgent::m_startWith)
        .property("stoppingDistance", &FlowFieldAgent::m_stoppingDistance)
        .property("slowingDistance",  &FlowFieldAgent::m_slowingDistance)
        .property("neighbourRadius",   &FlowFieldAgent::m_neighbourRadius)
        .property("avoidancePriority", &FlowFieldAgent::m_avoidancePriority)
        .property("alignmentWeight",   &FlowFieldAgent::m_alignmentWeight)
        .property("radius",            &FlowFieldAgent::m_radius)
        .property("avoidanceDistance", &FlowFieldAgent::m_avoidanceDistance)
        .property("avoidanceWeight",   &FlowFieldAgent::m_avoidanceWeight)
    ;
}
