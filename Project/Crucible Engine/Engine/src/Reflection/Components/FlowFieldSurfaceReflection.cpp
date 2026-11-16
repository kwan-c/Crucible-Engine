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
        .property("shoulderHeight",    &FlowFieldSurface::m_shoulderHeight)
        .property("maxWalkableSlope",  &FlowFieldSurface::m_maxWalkableSlope)   // scenes saved before it load as 35
        .property("floodRadius",       &FlowFieldSurface::m_floodRadius)
        .property("rebuildDistance",   &FlowFieldSurface::m_rebuildDistance)
        .property("wallAdjacencyCost", &FlowFieldSurface::m_wallAdjacencyCost)
    ;
}
