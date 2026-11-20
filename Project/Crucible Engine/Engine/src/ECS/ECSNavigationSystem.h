/*************************************************************************
* \file:         ECSNavigationSystem.h
* \author:       Jean David Vidal Cruzado (main author; design and Unity original;
*                v1.2: re-bake on request;
*                v1.3: tagged obstacles; v1.4: rays read only the terrain;
*                v1.5: terrain read from the generator's grid;
*                v1.6: baking and debug drawing split out;
*                v1.7: goals and agent movement split out;
*                v1.9: moving DynamicObstacles re-stamped;
*                v1.10: obstacle tag suggestions, bake warning;
*                v1.11: Walkable boxes read for layers),
*                Hugo Low Ren Hao (initial engine port)
* \par:          Email: jeandavidvidal.c@digipen.edu,
*                Email: low.h@digipen.edu
* \date          31 October 2026
* \version       1.11
* \brief
*                Enemy navigation's ECS side: bakes the NavGrid from the
*                scene's FlowFieldSurface and "Obstacle"-tagged colliders, and draws
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
*                after SetDestination, with Unity's crowd rules (bodies that
*                overlap are moved apart, and the less important agent gives
*                way) and alignment from its neighbours (found through an
*                AgentSpatialHash), and collision: a
*                body-sized sphere cast that swerves agents round walls
*                early and slides them along walls they still reach.
*
* \history
*    -v1.0       first version: bake and draw the grid; the flow field
*                towards the players, and its arrows; moving the agents
*    -v1.1       Unity's crowd: body overlap and avoidance priority replace
*                separation (AgentRef carries radius, priority, arrived);
*                Active(), IsWalkable() and IsPathClear() for C# Navigation;
*                ground heights: the Play bake samples the colliders into a
*                HeightfieldNavTerrain (slopes, ledges), and colliders that
*                are ground no longer count as obstacles
*    -v1.2       RequestBake(): a script that changed the level mid-Play
*                (generated terrain) asks for a fresh bake, which waits
*                for physics to build the new bodies, as Play's does
*    -v1.3       obstacles are the entities tagged "Obstacle"
*                (kObstacleTag); the ground rays pass through them, and the
*                ground-entity list that un-blocked floors is gone
*    -v1.4       SampleTerrain reads only the terrain (HeightField with a
*                built Terrain); untagged colliders are invisible to
*                navigation
*    -v1.5       F2: the terrain's heights and cliffs come straight from the
*                Terrain component's grid (TerrainGridNavTerrain), in Edit and
*                Play - no ground rays, no ledge rule, no
*                HeightfieldNavTerrain. Bakes no longer wait for physics.
*                Tags StaticObstacle and DynamicObstacle replace Obstacle
*                (both stamped at bake until dynamic re-stamping exists).
*                With a generated terrain the grid is fitted to the terrain's
*                bounds (m_fittedToTerrain)
*    -v1.6       build step 3, part 1: the bake's work is NavBaker (which
*                owns the ground), the overlay NavDebugDraw; no behaviour
*                change
*    -v1.7       build step 3, part 2: the goals and the field's rebuild rule
*                are NavGoalTracker, the agents' movement NavAgentMover; no
*                behaviour change
*    -v1.9       build step 4c: while Playing, every DynamicObstacle is
*                checked every dynamicUpdateInterval and re-stamped when the
*                cells it covers change (NavDynamicObstacles); costs are
*                fixed locally and the flow rebuilt only if it is touched.
*                (v1.8, 4a, was a .cpp-only change.)
*    -v1.10      build step 4d: SuggestObstacleTags (the Inspector's
*                Auto-tag obstacles button); the bake warns about
*                StaticObstacles that have a RigidBody
*    -v1.11      build step 6b: box colliders tagged Walkable are read at
*                bake and handed to the grid as layers
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>   // BakeCount
#include <string>
#include <string_view>   // kStaticObstacleTag, kDynamicObstacleTag
#include <vector>

// Crucible engine
#include "EngineExport.h"
#include "Math/Math.h"
#include "Navigation/FlowField.h"
#include "Navigation/NavAgentMover.h"
#include "Navigation/NavBaker.h"
#include "Navigation/NavDynamicObstacles.h"
#include "Navigation/NavGoalTracker.h"
#include "Navigation/NavGrid.h"
#include "Navigation/NavTerrain.h"
#include "Systems/IECSSystems.h"
#include "Systems/PlayState.h"

namespace Crucible {

    // The tags (Assets/Config/Tags.json) that make a collider a navigation
    // obstacle: the bake blocks the cells under it (within the height band).
    // Static ones are stamped once at bake; dynamic ones are stamped at bake
    // and then re-checked every dynamicUpdateInterval while Playing.
    // Untagged colliders are invisible to navigation.
    inline constexpr std::string_view kStaticObstacleTag  = "StaticObstacle";
    inline constexpr std::string_view kDynamicObstacleTag = "DynamicObstacle";

    // The tag that makes a box collider's top face a walkable layer (a bridge
    // deck, a ramp, a platform). Read at bake; it does not move afterwards.
    inline constexpr std::string_view kWalkableTag = "Walkable";

    // One tag the Inspector's "Auto-tag obstacles" button would give
    // (ECSNavigationSystem::SuggestObstacleTags).
    struct NavTagSuggestion {

        Entity           entity;
        std::string_view tag;      // kStaticObstacleTag or kDynamicObstacleTag
        std::string      reason;   // for the log: why this tag

    };  // NAVTAGSUGGESTION STRUCT

    class ECSSystemManager;
    class PlayMode;
    struct FlowFieldSurface;

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
        ~ECSNavigationSystem() override;

        // Rule of 5: copying would leave two systems claiming to be Active().
        ECSNavigationSystem(const ECSNavigationSystem&) = delete;
        ECSNavigationSystem& operator=(const ECSNavigationSystem&) = delete;

        /*!
            \brief
            The navigation system that exists right now, or null before Core
            has made it (and after it is gone). How the script bindings reach
            the grid for the C# Navigation class - the same idea as Unity's
            NavMesh being one static, global thing.

            \details
            A static pointer rather than a setter through ScriptRuntime (the
            way physics is handed over): that keeps it inside the navigation
            files. Fine while there is one Core per process, which there is.
        */
        static ECSNavigationSystem* Active() { return s_active; }

        // --- Queries (C# Navigation, ranged positioning, the horde) -------- //

        /*!
            \brief
            Could an enemy stand here? The grid cell under it is open (inside
            the surface, no obstacle) and the terrain is not a wall there.
            Height is ignored: this is the top-down map only.

            \return
            False if the grid isn't baked (nothing is known yet).
        */
        bool IsWalkable(const Math::vec3& position) const;

        /*!
            \brief
            Could an enemy walk in a straight line from `from` to `to` without
            crossing a blocked cell or a terrain wall? Unity's
            EnemyLocalPositionQuery.ClearSegment (its grid half): samples
            every half cell.

            \details
            An enemy pushed into a blocked cell (by the crowd or a blast) must
            still be able to walk OUT, so blocked cells at the very start of
            the line are allowed; once it has reached an open cell, the next
            blocked one fails it. Without that it would be stuck forever,
            every way out "blocked".

            \return
            False if the grid isn't baked.
        */
        bool IsPathClear(const Math::vec3& from, const Math::vec3& to) const;

        // --- IECSSystem ---------------------------------------------------- //

        void Update(World& world, CommandBuffer& commands) override;

        // Update: gameplay stage. Later the agents move here, before physics
        // (PostUpdate) and before rendering collects the debug lines (PreRender).
        SystemStage Stage() const override { return SystemStage::Update; }

        // --- Editor -------------------------------------------------------- //

        /*!
            \brief
            Rebuilds the grid from the scene as it is now: the first
            FlowFieldSurface gives the size and settings, and every
            non-trigger collider on an entity tagged StaticObstacle or
            DynamicObstacle is an obstacle. Untagged colliders are ignored.
            The ground is the generated terrain's grid (its heights and
            cliffs, in Edit and Play) or, with no terrain, the flat plane
            at the surface's height. With a terrain, the grid covers the
            terrain's own bounds and the surface's scale is ignored.
            Needs no physics.

            \param world
            The scene to bake.

            \return
            False if there is no FlowFieldSurface or its settings are invalid
            (the reason is logged). The grid is then cleared.
        */
        ENGINE_API bool Bake(World& world);

        ENGINE_API bool IsBaked() const { return m_grid.IsBaked(); }

        /*!
            \brief
            What the Inspector's "Auto-tag obstacles" button would tag. Only
            suggests: the editor applies them (as one undo step). Bakes first,
            because the height band needs the ground.
              - Untagged collider reaching into the height band, on the grid:
                StaticObstacle, or DynamicObstacle if it has a RigidBody
                (dynamic or kinematic - physics or a script moves it).
              - StaticObstacle with a RigidBody: DynamicObstacle.
            Never touches: triggers, the terrain, the Nav Surface's own
            collider, enemies (FlowFieldAgent) and players (PlayerController,
            CharacterController) and anything parented under them, or
            entities with any other tag.

            \return
            Empty outside Edit (tags changed in Play are lost at Stop), or if
            there is no surface to bake.
        */
        ENGINE_API std::vector<NavTagSuggestion> SuggestObstacleTags(World& world);

        /*!
            \brief
            Bakes again a frame later while Playing, once the transforms
            scripts changed this frame are worked out. For a script that
            just built or changed the ground (C# Navigation.Rebake); several
            requests before the bake runs are one bake.

            \details
            Ignored outside Play (an Edit bake is the Inspector's Bake
            button). Several requests before the bake runs are one bake.
        */
        ENGINE_API void RequestBake() { m_rebakeRequested = true; }

        // How many bakes have succeeded since the engine started: a script that
        // asked for one waits for this to change (C# Navigation.BakeCount).
        ENGINE_API std::uint32_t BakeCount() const { return m_bakeCount; }

    private:

        void CollectGoals(World& world);
        void ReadObstacles(World& world, bool dynamicOnly);
        void UpdateDynamicObstacles(World& world, const FlowFieldSurface& surface);
        void RestampDynamicObstacles(World& world);

        static inline ECSNavigationSystem* s_active = nullptr;   // see Active()

        const PlayMode&          m_playMode;
        PlayState                m_lastState = PlayState::Edit;   // last frame's, to catch Edit -> Playing and back
        bool                     m_bakePending = false;           // Play started; bake once the surface's WorldTransform is worked out

        NavGrid                  m_grid;
        FlowField                m_field;        // flows over m_grid, which it points at: declared after it

        // The bake's work and the ground (m_baker.Terrain(): the generated
        // terrain's grid when there is one, else the flat plane at the
        // surface's height). Every user asks it without caring which.
        NavBaker                 m_baker;

        std::vector<NavObstacle> m_obstacles;        // the tagged obstacles read for a bake; kept so a re-bake reuses its memory
        std::vector<NavWalkable> m_walkables;        // the Walkable boxes read for a bake
        bool                     m_rebakeRequested = false;  // RequestBake() since the last Update: bake on the next one
        bool                     m_bakeNextFrame   = false;  // a bake was just asked for: wait a frame so WorldTransforms catch up
        std::uint32_t            m_bakeCount = 0;            // successful bakes so far (BakeCount)

        // The DynamicObstacles as last stamped, re-checked every
        // dynamicUpdateInterval while Playing (build step 4c).
        NavDynamicObstacles             m_dynamicObstacles;
        std::vector<NavDynamicObstacle> m_dynamicNow;          // this check's DynamicObstacle boxes (memory reused)
        std::vector<int>                m_changedCells;        // cells that went open <-> blocked in this check
        float                           m_dynamicTimer   = 0.0f;   // seconds since the last check
        int                             m_dynamicRefresh = 0;      // after Stop: frames until the restored positions are re-stamped

        NavGoalTracker           m_goals;   // this frame's players, and when the field is rebuilt towards them
        NavAgentMover            m_mover;   // moves the FlowFieldAgents (owns the probes' physics lookup)

    };  // ECSNAVIGATIONSYSTEM CLASS

}   // NAMESPACE CRUCIBLE
