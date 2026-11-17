/*************************************************************************
* \file:         ECSNavigationSystem.h
* \author:       Jean David Vidal Cruzado (main author; design and Unity original;
*                v1.2: re-bake on request;
*                v1.3: tagged obstacles; v1.4: rays read only the terrain),
*                Hugo Low Ren Hao (initial engine port)
* \par:          Email: jeandavidvidal.c@digipen.edu,
*                Email: low.h@digipen.edu
* \date          31 October 2026
* \version       1.4
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
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>   // BakeCount
#include <string_view>   // kObstacleTag
#include <vector>

// Crucible engine
#include "EngineExport.h"
#include "Math/Math.h"
#include "Navigation/AgentSpatialHash.h"
#include "Navigation/FlowField.h"
#include "Navigation/HeightfieldNavTerrain.h"
#include "Navigation/NavGrid.h"
#include "Navigation/NavTerrain.h"
#include "Systems/IECSSystems.h"
#include "Systems/PlayState.h"

namespace Crucible {

    // The tag (Assets/Config/Tags.json) that makes a collider a navigation
    // obstacle: the bake blocks its cells and the ground rays look through it.
    inline constexpr std::string_view kObstacleTag = "Obstacle";

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
            non-trigger collider on an entity tagged "Obstacle"
            (kObstacleTag) is an obstacle. Untagged colliders are ignored.
            While Playing, a terrain's heights are sampled from physics, so
            its slopes and cliffs count too; without a terrain the ground
            is the flat plane at the surface's height.

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
            Bakes again while Playing, once physics has stepped with the
            level as it is now. For a script that just built or changed
            the ground (C# Navigation.Rebake): calling Bake straight away
            would sample physics before it has made the new bodies, and
            the new ground would bake as nothing.

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
        Entity NearestGoalEntity(const Math::vec3& from) const;
        void UpdateField(const FlowFieldSurface& surface);
        void UpdateAgents(World& world);
        void StandOnGround(Entity self, FlowFieldAgent& agent, Math::vec3& position, float dt) const;
        bool OnGround(Entity self, const FlowFieldAgent& agent, const Math::vec3& position) const;
        bool Probe(Entity self, const FlowFieldAgent& agent, const Math::vec3& position, const Math::vec3& direction,
                   float distance, EntityHit& hit) const;
        bool SampleTerrain(const NavGridSettings& settings, float stepHeight, float maxSlopeDegrees,
                           float top, float bottom, const std::vector<Entity>& terrains);
        void DrawGrid() const;
        void DrawArrows() const;

        static inline ECSNavigationSystem* s_active = nullptr;   // see Active()

        const PlayMode&          m_playMode;
        ECSSystemManager&        m_systems;
        ECSPhysicsSystem*        m_physics   = nullptr;           // found in m_systems on first use; null = no collision
        PlayState                m_lastState = PlayState::Edit;   // last frame's, to catch Edit -> Playing and back
        bool                     m_bakePending = false;           // Play started; bake once the surface's WorldTransform is worked out
        float                    m_stepHeight = 0.3f;             // the surface's, from the last bake: where the probe's sweep starts

        NavGrid                  m_grid;
        FlowField                m_field;        // flows over m_grid, which it points at: declared after it

        // The ground. m_terrain points at one of the two below, chosen by Bake:
        // the heightfield when physics could be sampled (Play), else the flat
        // plane at the surface's height (the Edit preview, or no physics).
        // A pointer so every user asks "the terrain" without caring which.
        FlatNavTerrain           m_flatTerrain;
        HeightfieldNavTerrain    m_heightTerrain;
        const NavTerrain*        m_terrain = &m_flatTerrain;

        std::vector<NavObstacle> m_obstacles;        // kept between bakes so a re-bake reuses its memory
        bool                     m_physicsStepped = false;   // physics has stepped this Play, so its bodies exist to be sampled
        bool                     m_rebakeRequested = false;  // RequestBake() since the last Update: bake after physics steps again
        std::uint32_t            m_bakeCount = 0;            // successful bakes so far (BakeCount)

        std::vector<Math::vec3>  m_goals;         // this frame's player positions (reused every frame)
        std::vector<Entity>      m_goalEntities;  // the same players, same order - for Arrive(nearest player)
        std::vector<Math::vec3>  m_goalsAtBuild;  // where they were at the last rebuild; empty = rebuild as soon as there is a player
        bool                     m_warnedParented = false;   // "agents must be roots" said once per session, not every frame

        // One agent for this frame's two-pass update: where to write, plus where
        // it was and how it moved at the START of the frame (what neighbours see).
        struct AgentRef {

            Entity          entity;                // so its own collider is ignored by its probes
            FlowFieldAgent* agent     = nullptr;
            Transform*      transform = nullptr;
            Math::vec3      start{ 0.0f };
            Math::vec3      velocity{ 0.0f };

            // For the crowd: who gives way when two bodies overlap. Copied at the
            // start of the frame, like start and velocity, so an agent pass 2 has
            // already handled (and maybe marked arrived) can't change the answer
            // for the ones after it.
            float           radius   = 0.0f;
            int             priority = 50;
            bool            arrived  = false;
            bool            dynamic  = false;      // has a dynamic RigidBody: physics moves it, the agent steers its velocity
            Math::vec3      bodyVelocity{ 0.0f };  // dynamic: the body's own XZ velocity, as physics left it

        };  // AGENTREF STRUCT

        std::vector<AgentRef>    m_agents;        // refilled every frame (memory reused)
        AgentSpatialHash         m_hash;          // the agents by 8 m square, refilled every frame
        std::vector<int>         m_neighbours;    // one agent's neighbours, reused for every query

    };  // ECSNAVIGATIONSYSTEM CLASS

}   // NAMESPACE CRUCIBLE
