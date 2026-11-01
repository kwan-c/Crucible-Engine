/*************************************************************************
* \file:         NavigationComponents.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          31 October 2026
* \version       1.0
* \brief
*                FlowFieldSurface: where enemies can walk. Unity's
*                NavMeshSurface, for our grid and flow field.
*
* \details
*                One per scene. Its entity's position is the grid's centre
*                and its scale is the grid's size, and its fields are the
*                bake settings. Bake from its Inspector to preview the
*                blocked cells; pressing Play bakes again on its own
*                (ECS/ECSNavigationSystem.h).
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// Crucible engine
#include "ECS/Components/ComponentKind.h"
#include "Math/Math.h"

namespace Crucible {

    /*
        The walkable area enemies navigate. Put it on an empty entity, sit the
        entity on the floor, and scale it to cover the level: scale X is the
        width and scale Z the depth, in metres (Y and rotation are ignored -
        the grid always lines up with the world axes). Its cyan outline shows
        the area live as you scale it; outside it counts as a wall.

        Why the scale and not width/depth fields: the scale gizmo is how every
        other volume in the editor is sized, and it shows the result as you
        drag. A fresh entity has scale 1, so a new surface starts tiny rather
        than 500 m wide, and the bake warns until it is scaled up.

        Sit it on the floor: until a terrain system exists, the entity's height
        IS the ground height, and the step/shoulder band is measured from it.

        Unlike Unity, the bake is not saved to a file. Ours takes milliseconds
        and the terrain is generated fresh every run, so a saved bake would
        only ever be out of date: Play always bakes again from the scene as it
        is.

        What blocks: every static Collider (no RigidBody) on the NonMoving
        layer that is not a trigger and reaches into the band between
        m_stepHeight and m_shoulderHeight above the ground. Floors are under
        the band, so they never block.
    */
    struct FlowFieldSurface {

        float m_cellSize          = 2.0f;     // metres. Smaller is finer but slower: halving it makes 4x the cells
        float m_bodyRadiusPadding = 0.6f;     // the enemy's body radius: obstacles grow by this, so open cells fit a whole body
        float m_stepHeight        = 0.3f;     // obstacles entirely lower than this above the ground are stepped over
        float m_shoulderHeight    = 1.6f;     // obstacles entirely higher than this above the ground are walked under

    };  // FLOWFIELDSURFACE STRUCT

    CRUCIBLE_COMPONENT(FlowFieldSurface, FlowFieldSurface)

}   // NAMESPACE CRUCIBLE
