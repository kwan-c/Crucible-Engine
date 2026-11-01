/*************************************************************************
* \file:         ECSNavigationSystem.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          31 October 2026
* \version       1.0
* \brief
*                Enemy navigation's ECS side: bakes the NavGrid from the
*                scene's FlowFieldSurface and static colliders, and draws
*                the result.
*
* \details
*                Bakes when the Inspector's Bake button asks (a preview in
*                Edit) and again on its own when Play starts, so the game
*                never runs on a stale or missing grid. Draws the grid's
*                outline and its blocked cells every frame while baked,
*                under the Gizmos menu's Gameplay toggle.
*
*                Runs in Edit as well as Play (no SimulationOnly), because
*                the preview has to show while editing - the same as
*                Unity's NavMesh overlay.
*
*                Later steps add the flow field and the agents here.
*
* \history
*    -v1.0       first version: bake and draw the grid
*
**************************************************************************/
#pragma once

// STL
#include <vector>

// Crucible engine
#include "EngineExport.h"
#include "Navigation/NavGrid.h"
#include "Navigation/NavTerrain.h"
#include "Systems/IECSSystems.h"
#include "Systems/PlayState.h"

namespace Crucible {

    class PlayMode;

    // Not exported as a whole class (like ECSPhysicsSystem): only the members
    // the editor calls are ENGINE_API. That keeps the std::vector and NavGrid
    // members inside the DLL, so no warning C4251.
    class ECSNavigationSystem final : public IECSSystem {

    public:

        // playMode: read every frame to spot the moment Play starts. Core owns
        // it and it outlives this system.
        explicit ECSNavigationSystem(const PlayMode& playMode);

        // --- IECSSystem ---------------------------------------------------- //

        void Update(World& world, CommandBuffer& commands) override;

        // Update: gameplay stage. Later the agents move here, before physics
        // (PostUpdate) and before rendering collects the debug lines (PreRender).
        SystemStage Stage() const override { return SystemStage::Update; }

        // --- Editor -------------------------------------------------------- //

        /*!
            \brief
            Rebuilds the grid from the scene as it is now: the first
            FlowFieldSurface gives the size and settings, and every static,
            non-trigger collider is an obstacle.

            \param world
            The scene to bake.

            \return
            False if there is no FlowFieldSurface or its settings are invalid
            (the reason is logged). The grid is then cleared.
        */
        ENGINE_API bool Bake(World& world);

        ENGINE_API bool IsBaked() const { return m_grid.IsBaked(); }

    private:

        void DrawGrid() const;

        const PlayMode&          m_playMode;
        PlayState                m_lastState = PlayState::Edit;   // last frame's, to catch Edit -> Playing

        NavGrid                  m_grid;
        FlatNavTerrain           m_terrain;     // until a terrain system exists: no walls, ground at 0
        std::vector<NavObstacle> m_obstacles;   // kept between bakes so a re-bake reuses its memory

    };  // ECSNAVIGATIONSYSTEM CLASS

}   // NAMESPACE CRUCIBLE
