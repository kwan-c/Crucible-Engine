/*************************************************************************
  \file       FlowFieldSurfaceReflection.cpp
  \author     Jean David Vidal Cruzado (main author; design and Unity original),
              Hugo Low Ren Hao (initial engine port)
  \par        Email: jeandavidvidal.c@digipen.edu,
              Email: low.h@digipen.edu
  \date       31 October 2026
  \version    1.0
  \brief
    RTTR registration for FlowFieldSurface.
  \details
    Every field is saved and editable. Property names are what the scene
    file uses as JSON keys, so treat them as a file format: renaming one
    breaks every scene saved with the old name.

    One RTTR_REGISTRATION block per .cpp, never in a header (see
    CameraOrbitReflection.cpp for why).
  \dependencies
    Reflection/ReflectionRegistration.h, Navigation/NavigationComponents.h
  \see
    ECS/ECSNavigationSystem.h
  \history
    - 1.0 (31 October 2026): First version.
    - 1.1 (10 November 2026, Jean David): maxWalkableSlope.
    - 1.2 (17 November 2026, Jean David): maxWalkableSlope removed (cliffs come
      from the terrain generator since F2). Scenes that still have the key load
      fine: a key with no property is skipped.
    - 1.3 (19 November 2026, Jean David): dynamicUpdateInterval. Scenes saved
      before it load with the default (0.25 s).
    - 1.4 (20 November 2026, Jean David): shoulderHeight renamed agentHeight.
      An old "shoulderHeight" key is skipped like any unknown key, so the
      value loads as the default (1.6 m) - what every scene had.
    - 1.5 (20 November 2026, Jean David): maxLayersPerCell (default 4).
    - 1.6 (20 November 2026, Jean David): maxDropHeight (default 3 m).
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/

#include "Reflection/ReflectionRegistration.h"
#include "Navigation/NavigationComponents.h"

RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::class_<FlowFieldSurface>("FlowFieldSurface")
        .constructor<>()
        .property("cellSize",          &FlowFieldSurface::m_cellSize)
        .property("bodyRadiusPadding", &FlowFieldSurface::m_bodyRadiusPadding)
        .property("stepHeight",        &FlowFieldSurface::m_stepHeight)
        .property("agentHeight",       &FlowFieldSurface::m_agentHeight)
        .property("maxLayersPerCell",  &FlowFieldSurface::m_maxLayersPerCell)
        .property("maxDropHeight",     &FlowFieldSurface::m_maxDropHeight)
        .property("floodRadius",       &FlowFieldSurface::m_floodRadius)
        .property("rebuildDistance",   &FlowFieldSurface::m_rebuildDistance)
        .property("wallAdjacencyCost", &FlowFieldSurface::m_wallAdjacencyCost)
        .property("dynamicUpdateInterval", &FlowFieldSurface::m_dynamicUpdateInterval)
    ;
}
