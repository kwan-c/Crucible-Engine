/*************************************************************************
* \file:         ECSNavigationSystem.cpp
* \author:       Jean David Vidal Cruzado (main author; design and Unity original;
*                v1.2: terrain, re-bake on request;
*                v1.3: tagged obstacles;
*                v1.4: rays read only the terrain;
*                v1.5: terrain read from the generator's grid;
*                v1.6: baking and debug drawing split out;
*                v1.7: goals and agent movement split out;
*                v1.9: moving DynamicObstacles re-stamped;
*                v1.10: obstacle tag suggestions, bake warning;
*                v1.11: Walkable boxes read for layers;
*                v1.12: queries and agents per layer),
*                Hugo Low Ren Hao (initial engine port)
* \par:          Email: jeandavidvidal.c@digipen.edu,
*                Email: low.h@digipen.edu
* \date          31 October 2026
* \version       1.12
* \brief
*                Bakes the NavGrid from the scene, and draws it.
*
* \details
*                The ECS-to-grid translation lives here, so NavGrid itself
*                never sees a component: this file turns the colliders of
*                entities tagged "Obstacle" into NavObstacle boxes and the
*                FlowFieldSurface into NavGridSettings.
*
* \history
*    -v1.0       first version
*    -v1.1       Unity's crowd: overlapping bodies are moved apart and the
*                less important agent gives way (CrowdPush), instead of the
*                full-speed separation push from 3.5 m. Alignment only under
*                FollowField (it made arrived agents drift at 1.7 m/s). Play
*                bakes once the surface's WorldTransform is valid (the Game's
*                first frame baked a 1 x 1 m grid). StandOnGround: agents
*                stand on the surface under them (ray down, ease, fall off
*                ledges), not on a flat plane at the surface entity's height.
*                Dynamic agents: a dynamic RigidBody is moved by physics and
*                the agent steers only its XZ velocity (knockback fades at
*                m_acceleration; no steering while airborne)
*    -v1.2       FlowFieldSurface::m_maxWalkableSlope instead of a fixed
*                35 degrees; RequestBake(); a HeightField collider (terrain) is always
*                ground, never an obstacle: its heights come from the
*                sampling like any other floor
*    -v1.3       Obstacles are the colliders of entities tagged "Obstacle"
*                (Unity's NavGrid rule), not every NonMoving collider without
*                a RigidBody. The ground rays pass through tagged obstacles,
*                so the ground under one is the floor it stands on, and the
*                "colliders found to be ground stop being obstacles" step is
*                gone: the tag alone decides. Untagged walls and crates
*                still block through the slope and ledge rules
*    -v1.4       Unity's rule in full: untagged colliders are invisible to
*                navigation. The ground rays only read the terrain (a
*                HeightField collider with a built Terrain grid): every other
*                hit is ignored, and the ray range comes from the terrain's
*                own heights. A scene with no terrain is not sampled: its
*                ground is the flat plane at the surface's height
*    -v1.5       F2, the generator hand-off (as Unity's NavTerrain.Register):
*                the terrain's heights and cliffs are read from the Terrain
*                component's grid (TerrainGridNavTerrain), so a nav cell is a
*                wall when the generator cell under its centre is a Cliff.
*                Works in Edit mode too. The ground rays, the ledge rule and
*                the wait for a physics step before baking are gone: the bake
*                needs no physics. Obstacles are tagged StaticObstacle or
*                DynamicObstacle (both stamped at bake for now). With a
*                generated terrain the grid covers the terrain exactly (its
*                own bounds; the surface's scale is ignored), in Edit and
*                Play, and the outline shows that area
*    -v1.6       Build step 3, part 1 (no behaviour change): the bake's work
*                moved to Navigation/NavBaker (which also owns the ground) and
*                the overlay to Navigation/NavDebugDraw. This file still reads
*                the scene and hands them plain data
*    -v1.7       Build step 3, part 2 (no behaviour change): the goals and
*                the field's rebuild rule moved to Navigation/NavGoalTracker,
*                and the agents' movement (steering, crowd, collision,
*                facing, ground) with its helpers to Navigation/NavAgentMover.
*                This file reads the scene and runs them in order
*    -v1.8       Build step 4a: DynamicObstacle boxes are marked dynamic, so
*                the grid counts them separately (still stamped at bake)
*    -v1.9       Build step 4c: while Playing, every dynamicUpdateInterval
*                the DynamicObstacles are read again and NavDynamicObstacles
*                re-stamps those whose cells changed (and stamps new ones,
*                unstamps gone ones); FlowField::UpdateCosts fixes those
*                cells and the flow is rebuilt this frame if the change
*                touches it. After Stop the restored positions are
*                re-stamped, so the Edit view matches the scene
*    -v1.10      Build step 4d: SuggestObstacleTags for the Inspector's
*                Auto-tag obstacles button; the bake warns once about
*                StaticObstacles that have a RigidBody (their cells would
*                not follow them)
*    -v1.11      Build step 6b: the bake reads box colliders tagged Walkable
*                into NavBakeInput::walkables (and maxLayersPerCell); other
*                shapes tagged Walkable are named in a warning
*    -v1.12      Build step 6d-2: IsWalkable / IsPathClear are the grid's
*                (layers and links); the mover gets agentHeight and the
*                padding as well as the step height
*
**************************************************************************/
#include "ECSNavigationSystem.h"

// STL
#include <algorithm>    // std::max
#include <string>       // std::to_string

// Crucible engine
#include "Components/Components.h"            // WorldTransform
#include "Diagnostics/Logger.h"
#include "Navigation/NavDebugDraw.h"         // the overlay
#include "Navigation/NavigationComponents.h"  // FlowFieldSurface
#include "Physics/PhysicsComponents.h"        // Collider
#include "Systems/PlayMode.h"
#include "Systems/Query.h"
#include "World.h"

namespace Crucible {

    namespace {

        /*!
            \brief
            A collider's world axis-aligned box, the way physics sizes it: the
            shape's local box around m_center, scaled, turned and moved by the
            entity's world matrix.

            \details
            Turning a box makes its AABB bigger than the box itself (a cube
            turned 45 degrees is about 1.4 times as wide). That is the
            conservative footprint Jean David's bake uses on purpose.
        */
        NavObstacle WorldBounds(const Collider& collider, const Math::mat4& world) {

            // Half the shape's size along each local axis.
            Math::vec3 half(0.0f);
            switch (collider.m_type) {
                case ShapeType::Box:     half = collider.m_size * 0.5f; break;
                case ShapeType::Sphere:  half = Math::vec3(collider.m_radius); break;
                // A capsule stands along its local Y. m_height is the full height, ends
                // included, but never less than the two end caps together.
                case ShapeType::Capsule: half = Math::vec3(collider.m_radius, std::max(collider.m_height * 0.5f, collider.m_radius), collider.m_radius); break;
                // Never an obstacle (Bake skips it): terrain is ground.
                case ShapeType::HeightField: break;
            }

            // Transform all 8 corners and keep the smallest and biggest of each axis.
            // Only corners are needed: a box's furthest points are always corners.
            NavObstacle bounds{ Math::vec3(1e30f), Math::vec3(-1e30f) };
            for (int corner = 0; corner < 8; ++corner) {

                const Math::vec3 sign((corner & 1) ? 1.0f : -1.0f, (corner & 2) ? 1.0f : -1.0f, (corner & 4) ? 1.0f : -1.0f);
                const Math::vec3 point = Math::vec3(world * Math::vec4(collider.m_center + sign * half, 1.0f));
                bounds.min = glm::min(bounds.min, point);
                bounds.max = glm::max(bounds.max, point);

            }
            return bounds;

        }   // WorldBounds()

        /*!
            \brief
            The position is the centre, and the scale along X and Z is the width
            and depth in metres - so the designer sizes the surface with the
            ordinary scale gizmo, like a box volume.

            \details
            A matrix's first three columns are its X, Y and Z axes, each
            stretched by that axis's scale, so a column's length IS the scale
            whether or not the entity is rotated. The rotation itself is
            ignored: the grid always lines up with the world axes, which is what
            keeps "which cell am I in" a single floor() per axis.
        */
        NavArea AreaOf(const Math::mat4& world) {

            return { Math::vec3(world[3]), glm::length(Math::vec3(world[0])), glm::length(Math::vec3(world[2])) };

        }   // AreaOf()

        // An entity's name for the log: its EntityName, or "Entity <index>".
        std::string NameOf(const World& world, Entity entity) {

            const EntityName* name = world.Read<EntityName>(entity);
            if (name && !name->name.empty()) return name->name;
            return "Entity " + std::to_string(entity.index);

        }   // NameOf()

        // An enemy, a player, or something parented under one (a weapon, a
        // part): never an obstacle to itself or the horde.
        bool IsAgentOrPlayer(const World& world, Entity entity) {

            for (Entity at = entity; !(at == kNullEntity); at = world.GetParent(at)) {
                if (world.HasComponent<FlowFieldAgent>(at) || world.HasComponent<PlayerController>(at)
                    || world.HasComponent<CharacterController>(at)) return true;
            }
            return false;

        }   // IsAgentOrPlayer()

    }   // ANONYMOUS NAMESPACE

    ECSNavigationSystem::ECSNavigationSystem(const PlayMode& playMode, ECSSystemManager& systems)
        : m_playMode(playMode), m_mover(systems) {

        s_active = this;

    }   // ECSNavigationSystem::ECSNavigationSystem()

    // Only clears it if it is still us, so a newer system made before this one
    // is destroyed keeps its claim. Without the clear, C# could reach a freed
    // system after Core shuts down.
    ECSNavigationSystem::~ECSNavigationSystem() {

        if (s_active == this) s_active = nullptr;

    }   // ECSNavigationSystem::~ECSNavigationSystem()

    // Both on the grid, which knows the layers and links (NavGrid v1.7).
    bool ECSNavigationSystem::IsWalkable(const Math::vec3& position) const {

        return m_grid.IsWalkable(position, m_baker.Terrain());

    }   // ECSNavigationSystem::IsWalkable()

    bool ECSNavigationSystem::IsPathClear(const Math::vec3& from, const Math::vec3& to) const {

        return m_grid.IsPathClear(from, to, m_baker.Terrain());

    }   // ECSNavigationSystem::IsPathClear()

    /*!
        \brief
        Bakes when Play starts, draws the surface's outline, forgets the bake
        when the surface is gone, keeps the flow field on the players while
        Playing, and draws the grid and the arrows.
    */
    void ECSNavigationSystem::Update(World& world, [[maybe_unused]] CommandBuffer& commands) {

        // Play bakes on its own: the designer may have moved things since the
        // last Bake click, or never clicked it, and the game must not run on a
        // stale grid. Only Edit -> Playing - Resume carries on with the same one.
        const PlayState state = m_playMode.State();
        if (m_lastState == PlayState::Edit && state == PlayState::Playing) { m_bakePending = true; m_bakeNextFrame = true; }

        // A script changed the level mid-Play (RequestBake): bake again. The bake
        // reads components only (the terrain's grid, obstacles' colliders), not
        // physics, so it doesn't wait for physics to build the new bodies.
        if (m_rebakeRequested && state == PlayState::Playing) { m_bakePending = true; m_bakeNextFrame = true; }
        m_rebakeRequested = false;

        // But it does wait one frame: scripts run in Update and change
        // Transforms (the generator sizes the Nav Surface to the map, then asks
        // for this bake), and WorldTransform only catches up in PreRender
        // (HierarchyTransformSystem), after this system. Baking in the same
        // frame read the surface's old size - a 120 m grid on a 500 m map.
        const bool waitThisFrame = m_bakeNextFrame;
        m_bakeNextFrame = false;

        // Not on the very frame Play starts, though, if the surface's world
        // matrix isn't worked out yet: the Game starts Playing on its first
        // frame, before HierarchyTransformSystem has run, and baking then read
        // an identity matrix - a 1 x 1 m grid at the origin, so no flow field.
        // (The Editor never hit this: its transforms exist long before Play.)
        // No surface at all bakes at once, so Bake's warning still comes. The
        // terrain's matrix must be worked out too, or the bake would place it
        // at the origin.
        bool surfaceReady = true;
        world.CreateQuery<const FlowFieldSurface, const WorldTransform>()
            .ForEach([&](Entity, const FlowFieldSurface&, const WorldTransform& transform) {
                if (!transform.valid) surfaceReady = false;
            });
        world.CreateQuery<const Terrain, const WorldTransform>()
            .ForEach([&](Entity, const Terrain&, const WorldTransform& transform) {
                if (!transform.valid) surfaceReady = false;
            });

        if (m_bakePending && state == PlayState::Playing && surfaceReady && !waitThisFrame) {

            m_bakePending = false;
            Bake(world);

            // Say why there will be no arrows, once, rather than leaving the
            // designer to guess.
            bool hasPlayer = false;
            world.BuildQuery<const PlayerController>().IncludeDisabled().Build().ForEach([&](Entity, const PlayerController&) { hasPlayer = true; });
            if (!hasPlayer) CRU_WARN("Navigation: no PlayerController in the scene - the flow field has no goal");

        }

        // Stop: the arrows pointed at a player in a session that is over, and
        // Stop puts the scene back as it was - so forget the flow (not the grid).
        if (m_lastState != PlayState::Edit && state == PlayState::Edit) {
            m_field.ClearFlow();
            m_goals.ForgetLastBuild();
            m_bakePending = false;
            m_mover.ResetWarnings();
            m_dynamicRefresh = 2;   // the grid still has the obstacles where Play left them
        }
        m_lastState = state;

        // Stop put the DynamicObstacles back where they were, but their
        // WorldTransforms only catch up in PreRender: re-stamp them one
        // frame later, so the red cells in Edit match the scene again.
        if (state == PlayState::Edit && m_dynamicRefresh > 0 && --m_dynamicRefresh == 0 && m_grid.IsBaked()) {
            RestampDynamicObstacles(world);
        }

        // The surface's outline comes straight from its Transform every frame,
        // baked or not, so it follows the scale gizmo live while the designer
        // sizes it - like Unity's volume gizmo. If it no longer matches the
        // grid underneath, that is the cue to bake again.
        FlowFieldSurface surface;
        bool             hasSurface = false;
        world.CreateQuery<const FlowFieldSurface, const WorldTransform>()
            .ForEach([&](Entity, const FlowFieldSurface& found, const WorldTransform& transform) {
                if (hasSurface) return;     // only the first surface counts, as in Bake
                hasSurface = true;
                surface    = found;
                // Fitted to a terrain, the grid covers the terrain, not the
                // surface's scale: outline what was really baked.
                if (m_baker.FittedToTerrain() && m_grid.IsBaked()) NavDebugDraw::Outline(m_baker.BakedArea());
                else                                               NavDebugDraw::Outline(AreaOf(transform.matrix));
            });

        // No surface any more (removed, switched off, or a scene without one):
        // the grid describes something that isn't there, so stop showing it.
        if (!hasSurface) {
            m_field.Clear();     // first: it points at the grid being reset
            m_grid = NavGrid{};
            m_baker.Clear();
            m_goals.ForgetLastBuild();
            m_dynamicObstacles.Clear();
        }

        // Only while Playing: Paused freezes the field and the agents along
        // with everything else, and in Edit there is no session to serve.
        // Field first, then agents, so every agent reads this frame's field.
        // Moving obstacles first, so the field below is built on this
        // frame's grid.
        if (state == PlayState::Playing) {
            if (m_grid.IsBaked()) UpdateDynamicObstacles(world, surface);
            CollectGoals(world);
            if (m_grid.IsBaked()) m_goals.UpdateField(m_field, surface.m_rebuildDistance, surface.m_floodRadius);
            m_mover.Move(world, m_field, m_goals, m_grid, m_baker.Terrain());
        }

        // Skipped outright when nothing would show them (Gizmos off, or their
        // category off): on a 500 m map the loops visit 62,500 cells a frame.
        if (m_grid.IsBaked()  && NavDebugDraw::WantsGrid())   NavDebugDraw::Grid(m_grid, m_baker.Terrain());
        if (m_field.HasFlow() && NavDebugDraw::WantsArrows()) NavDebugDraw::Arrows(m_grid, m_field);

    }   // ECSNavigationSystem::Update()

    // Every player's position this frame: the field's goals, and where agents
    // head when the field has no arrow for them. Gathered even with no grid,
    // so agents can still go straight at a player on a scene without a surface.
    void ECSNavigationSystem::CollectGoals(World& world) {

        m_goals.ClearGoals();
        // IncludeDisabled: a scripted player (MechPlayer) switches its PlayerController off and
        // moves itself, but it is still the player. Its CharacterController is only off when
        // the whole entity was (Entity.SetActive(false)), and then it is out of the game.
        world.BuildQuery<const PlayerController, const WorldTransform>().IncludeDisabled().Build()
            .ForEach([&](Entity player, const PlayerController&, const WorldTransform& transform) {
                if (!transform.valid) return;
                if (world.HasComponent<CharacterController>(player) && !world.IsComponentActive<CharacterController>(player)) return;
                m_goals.Add(Math::vec3(transform.matrix[3]), player);
            });

    }   // ECSNavigationSystem::CollectGoals()

    /*!
        \brief
        Reads the obstacles off the scene: the colliders of entities tagged
        StaticObstacle or DynamicObstacle - the designer's call, as in
        Unity's NavGrid, not a guess from the layer and RigidBody. Triggers
        block nothing in physics, so not here either. Untagged colliders are
        invisible to navigation (Unity's rule). The terrain (a HeightField
        collider) is ground, never an obstacle.

        \param dynamicOnly
        False (a bake): every obstacle into m_obstacles, the dynamic ones
        also into m_dynamicNow with their entities, and the box colliders
        tagged Walkable into m_walkables. True (a re-check): only
        m_dynamicNow.
    */
    void ECSNavigationSystem::ReadObstacles(World& world, bool dynamicOnly) {

        if (!dynamicOnly) { m_obstacles.clear(); m_walkables.clear(); }
        m_dynamicNow.clear();
        std::vector<Entity> staticWithBody;   // a bake's warning: StaticObstacles something can move
        std::vector<Entity> notBoxes;         // a bake's warning: Walkable on a shape with no top face
        world.CreateQuery<const Collider, const WorldTransform>()
            .ForEach([&](Entity entity, const Collider& collider, const WorldTransform& transform) {
                if (collider.m_isTrigger || !transform.valid || collider.m_type == ShapeType::HeightField) return;

                // Walkable: a layer, not an obstacle. Only boxes have a top face.
                if (!dynamicOnly && world.CompareTag(entity, kWalkableTag)) {
                    if (collider.m_type == ShapeType::Box) m_walkables.push_back({ transform.matrix, collider.m_center, collider.m_size * 0.5f });
                    else                                   notBoxes.push_back(entity);
                    return;
                }
                const bool isStatic  = !dynamicOnly && world.CompareTag(entity, kStaticObstacleTag);
                const bool isDynamic = !isStatic && world.CompareTag(entity, kDynamicObstacleTag);
                if (!isStatic && !isDynamic) return;
                if (isStatic && world.HasComponent<RigidBody>(entity)) staticWithBody.push_back(entity);
                NavObstacle obstacle = WorldBounds(collider, transform.matrix);
                obstacle.dynamic = isDynamic;   // stamped into the dynamic count, so it can be moved later
                if (!dynamicOnly) m_obstacles.push_back(obstacle);
                if (isDynamic)    m_dynamicNow.push_back({ entity, obstacle });
            });

        // A StaticObstacle is stamped once, so if physics or a script moves
        // it, enemies keep routing round where it was. Say so once per bake,
        // naming a few. (A DynamicObstacle with no RigidBody is fine: a
        // script may move its Transform.)
        if (!notBoxes.empty()) {
            std::string names;
            for (std::size_t i = 0; i < notBoxes.size() && i < 5; ++i) names += (i ? ", " : "") + NameOf(world, notBoxes[i]);
            if (notBoxes.size() > 5) names += ", ...";
            CRU_WARN("Navigation: {} Walkable collider(s) are not boxes ({}) - only a Box collider's top face makes a layer", notBoxes.size(), names);
        }
        if (!staticWithBody.empty()) {
            std::string names;
            for (std::size_t i = 0; i < staticWithBody.size() && i < 5; ++i) names += (i ? ", " : "") + NameOf(world, staticWithBody[i]);
            if (staticWithBody.size() > 5) names += ", ...";
            CRU_WARN("Navigation: {} StaticObstacle(s) have a RigidBody ({}) - their blocked cells won't follow them. "
                     "Tag them DynamicObstacle (Nav Surface > Auto-tag obstacles does it)", staticWithBody.size(), names);
        }

    }   // ECSNavigationSystem::ReadObstacles()

    // Every dynamicUpdateInterval while Playing (0 or less: every frame).
    void ECSNavigationSystem::UpdateDynamicObstacles(World& world, const FlowFieldSurface& surface) {

        m_dynamicTimer += world.DeltaTime();
        if (m_dynamicTimer < surface.m_dynamicUpdateInterval) return;
        m_dynamicTimer = 0.0f;
        RestampDynamicObstacles(world);

    }   // ECSNavigationSystem::UpdateDynamicObstacles()

    /*!
        \brief
        Reads the DynamicObstacles as they are now and re-stamps the ones
        whose cells changed (NavDynamicObstacles). If any cell changed, its
        cost and its neighbours' are fixed, and the flow is rebuilt this
        frame if the change is in the area it covers - or if there is no
        flow (a player sealed in may just have been let out).
    */
    void ECSNavigationSystem::RestampDynamicObstacles(World& world) {

        ReadObstacles(world, true);
        m_dynamicObstacles.Update(m_dynamicNow, m_grid, m_baker.Terrain(), m_changedCells);
        if (m_changedCells.empty()) return;

        m_field.UpdateCosts(m_changedCells);
        if (!m_field.HasFlow() || m_field.TouchesFlow(m_changedCells)) m_goals.ForgetLastBuild();

    }   // ECSNavigationSystem::RestampDynamicObstacles()

    std::vector<NavTagSuggestion> ECSNavigationSystem::SuggestObstacleTags(World& world) {

        std::vector<NavTagSuggestion> suggestions;

        // Tags changed while Playing are thrown away at Stop with the rest of
        // the session, so this is an Edit-mode tool only.
        if (m_playMode.State() != PlayState::Edit) {
            CRU_WARN("Navigation: Auto-tag obstacles works in Edit mode only - press Stop first");
            return suggestions;
        }

        // The height band is measured from the ground, which the bake works
        // out (flat or the generated terrain). It also refreshes the preview.
        if (!Bake(world)) return suggestions;

        std::vector<int> cells;
        world.CreateQuery<const Collider, const WorldTransform>()
            .ForEach([&](Entity entity, const Collider& collider, const WorldTransform& transform) {

                if (collider.m_isTrigger || !transform.valid || collider.m_type == ShapeType::HeightField) return;

                // Only untagged ones, and StaticObstacles to check. A
                // DynamicObstacle is already right; any other tag is someone's choice.
                const bool untagged = world.CompareTag(entity, kUntaggedTag);
                const bool isStatic = world.CompareTag(entity, kStaticObstacleTag);
                if (!untagged && !isStatic) return;

                const bool hasBody = world.HasComponent<RigidBody>(entity);   // dynamic or kinematic: something moves it
                if (isStatic && !hasBody) return;                             // already right
                if (world.HasComponent<FlowFieldSurface>(entity) || IsAgentOrPlayer(world, entity)) return;

                // Untagged: only if it would actually block - reaches into the
                // height band (floors and ceilings don't) and lies on the grid.
                if (untagged && (!m_grid.Footprint(WorldBounds(collider, transform.matrix), m_baker.Terrain(), cells) || cells.empty())) return;

                const std::string_view tag = hasBody ? kDynamicObstacleTag : kStaticObstacleTag;
                std::string reason = isStatic ? "StaticObstacle with a RigidBody"
                                   : hasBody  ? "untagged, in the height band, has a RigidBody"
                                              : "untagged, in the height band";
                suggestions.push_back({ entity, tag, NameOf(world, entity) + ": " + reason });

            });

        return suggestions;

    }   // ECSNavigationSystem::SuggestObstacleTags()

    bool ECSNavigationSystem::Bake(World& world) {

        // This reads the scene into plain data; NavBaker does the work.

        // 1. The surface: its settings, and its entity's position and scale as
        // the area it covers (used when there is no generated terrain).
        NavBakeInput input;
        int          surfaces = 0;
        world.CreateQuery<const FlowFieldSurface, const WorldTransform>()
            .ForEach([&](Entity, const FlowFieldSurface& found, const WorldTransform& transform) {
                if (surfaces++ != 0) return;
                input.surfaceArea       = AreaOf(transform.matrix);
                input.cellSize          = found.m_cellSize;
                input.bodyRadiusPadding = found.m_bodyRadiusPadding;
                input.stepHeight        = found.m_stepHeight;
                input.agentHeight       = found.m_agentHeight;
                input.maxLayersPerCell  = found.m_maxLayersPerCell;
                input.maxDropHeight     = found.m_maxDropHeight;
                input.wallAdjacencyCost = found.m_wallAdjacencyCost;
            });

        // Any bake, good or bad, makes the old flow meaningless: the next
        // UpdateField rebuilds it straight away (ForgetLastBuild).
        m_goals.ForgetLastBuild();

        if (surfaces == 0) {
            CRU_WARN("Navigation: nothing to bake - add a FlowFieldSurface to an entity first");
            m_field.Clear();
            m_grid = NavGrid{};
            m_dynamicObstacles.Clear();
            return false;
        }
        if (surfaces > 1) {
            CRU_WARN("Navigation: {} FlowFieldSurfaces in the scene - only the first one is used", surfaces);
        }
        m_mover.SetBodySettings(input.stepHeight, input.agentHeight, input.bodyRadiusPadding);   // the probes' band, the crowd's layers, the width check

        // 2. The generated terrain, if there is one. Not generated yet: nothing
        // to stand on, so it doesn't count.
        int terrains = 0;
        world.CreateQuery<const Terrain, const WorldTransform>()
            .ForEach([&](Entity, const Terrain& terrain, const WorldTransform& transform) {
                if (!terrain.m_grid || !terrain.m_grid->IsValid() || !transform.valid) return;
                if (terrains++ == 0) { input.terrainGrid = terrain.m_grid; input.terrainMatrix = transform.matrix; }
            });
        if (terrains > 1) CRU_WARN("Navigation: {} generated terrains in the scene - only the first one is used", terrains);

        // 3. The obstacles (ReadObstacles): tagged StaticObstacle or
        // DynamicObstacle; the dynamic ones are also kept with their entities.
        // And the Walkable boxes, whose tops are layers.
        ReadObstacles(world, false);
        input.walkables = m_walkables;

        // 4. The bake: area (fitted to the terrain if there is one), ground,
        // grid and the flow field's cost field.
        if (!m_baker.Bake(input, m_obstacles, m_grid, m_field)) {
            m_dynamicObstacles.Clear();
            return false;
        }

        // 5. The bake stamped the dynamic obstacles too: remember their
        // cells, so the re-checks in Play compare against them.
        m_dynamicObstacles.Adopt(m_dynamicNow, m_grid, m_baker.Terrain());
        m_dynamicTimer = 0.0f;
        ++m_bakeCount;
        return true;

    }   // ECSNavigationSystem::Bake()

}   // NAMESPACE CRUCIBLE
