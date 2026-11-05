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
*                never runs on a stale or missing grid. Draws the surface's
*                outline live from its Transform (position = centre, scale
*                X/Z = size), and the grid and its blocked cells while
*                baked, under the Gizmos menu's "Nav grid" toggle (the
*                flow field's arrows have their own, "Flow field arrows").
*
*                Runs in Edit as well as Play (no SimulationOnly), because
*                the preview has to show while editing - the same as
*                Unity's NavMesh overlay.
*
*                While Playing it also keeps the FlowField pointed at every
*                entity with a PlayerController, rebuilding it once a player
*                has moved m_rebuildDistance, and draws one arrow per cell
*                that has a direction. With no player the field is left as
*                it was: frozen at the last-known spot, or empty if there
*                never was one.
*
*                Then it moves every FlowFieldAgent by what it was told to
*                do (FollowField, Stop), the way Unity moves a NavMeshAgent
*                after SetDestination, with separation and alignment from
*                its neighbours (found through an AgentSpatialHash) so a
*                crowd spreads out instead of stacking, and collision: a
*                body-sized sphere cast that swerves agents round walls
*                early and slides them along walls they still reach.
*
* \history
*    -v1.0       first version: bake and draw the grid; the flow field
*                towards the players, and its arrows; moving the agents
*
**************************************************************************/
#pragma once

// STL
#include <vector>

// Crucible engine
#include "EngineExport.h"
#include "Math/Math.h"
#include "Navigation/AgentSpatialHash.h"
#include "Navigation/FlowField.h"
#include "Navigation/NavGrid.h"
#include "Navigation/NavTerrain.h"
#include "Systems/IECSSystems.h"
#include "Systems/PlayState.h"

namespace Crucible {

    class ECSPhysicsSystem;
    class ECSSystemManager;
    class PlayMode;
    struct EntityHit;
    struct FlowFieldAgent;
    struct FlowFieldSurface;
    struct Transform;

    // Not exported as a whole class (like ECSPhysicsSystem): only the members
    // the editor calls are ENGINE_API. That keeps the std::vector and NavGrid
    // members inside the DLL, so no warning C4251.
    class ECSNavigationSystem final : public IECSSystem {

    public:

        // playMode: read every frame to spot the moment Play starts. systems:
        // where physics is found, for the agents' collision probes (looked up
        // on first use, because physics is registered after this system).
        // Core owns both and they outlive this system.
        ECSNavigationSystem(const PlayMode& playMode, ECSSystemManager& systems);

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

        void CollectGoals(World& world);
        Entity NearestGoalEntity(const Math::vec3& from) const;
        void UpdateField(const FlowFieldSurface& surface);
        void UpdateAgents(World& world);
        bool Probe(Entity self, const FlowFieldAgent& agent, const Math::vec3& position, const Math::vec3& direction,
                   float distance, EntityHit& hit) const;
        void DrawGrid() const;
        void DrawArrows() const;

        const PlayMode&          m_playMode;
        ECSSystemManager&        m_systems;
        ECSPhysicsSystem*        m_physics   = nullptr;           // found in m_systems on first use; null = no collision
        PlayState                m_lastState = PlayState::Edit;   // last frame's, to catch Edit -> Playing and back
        float                    m_stepHeight = 0.3f;             // the surface's, from the last bake: where the probe's sweep starts

        NavGrid                  m_grid;
        FlowField                m_field;        // flows over m_grid, which it points at: declared after it
        FlatNavTerrain           m_terrain;      // until a terrain system exists: no walls, ground at the surface's height (set by Bake)
        std::vector<NavObstacle> m_obstacles;    // kept between bakes so a re-bake reuses its memory

        std::vector<Math::vec3>  m_goals;         // this frame's player positions (reused every frame)
        std::vector<Entity>      m_goalEntities;  // the same players, same order - for Arrive(nearest player)
        std::vector<Math::vec3>  m_goalsAtBuild;  // where they were at the last rebuild; empty = rebuild as soon as there is a player
        bool                     m_warnedParented = false;   // "agents must be roots" said once per session, not every frame
        bool                     m_warnedDynamic  = false;   // "tick isKinematic" said once per session

        // One agent for this frame's two-pass update: where to write, plus where
        // it was and how it moved at the START of the frame (what neighbours see).
        struct AgentRef {

            Entity          entity;                // so its own collider is ignored by its probes
            FlowFieldAgent* agent     = nullptr;
            Transform*      transform = nullptr;
            Math::vec3      start{ 0.0f };
            Math::vec3      velocity{ 0.0f };

        };  // AGENTREF STRUCT

        std::vector<AgentRef>    m_agents;        // refilled every frame (memory reused)
        AgentSpatialHash         m_hash;          // the agents by 8 m square, refilled every frame
        std::vector<int>         m_neighbours;    // one agent's neighbours, reused for every query

    };  // ECSNAVIGATIONSYSTEM CLASS

}   // NAMESPACE CRUCIBLE
