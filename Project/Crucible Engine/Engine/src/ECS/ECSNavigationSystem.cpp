/*************************************************************************
* \file:         ECSNavigationSystem.cpp
* \author:       Jean David Vidal Cruzado (main author; design and Unity original;
*                v1.2: terrain, re-bake on request;
*                v1.3: tagged obstacles;
*                v1.4: rays read only the terrain),
*                Hugo Low Ren Hao (initial engine port)
* \par:          Email: jeandavidvidal.c@digipen.edu,
*                Email: low.h@digipen.edu
* \date          31 October 2026
* \version       1.4
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
*
**************************************************************************/
#include "ECSNavigationSystem.h"

// STL
#include <algorithm>    // std::clamp, std::max, std::min, std::minmax_element, std::any_of
#include <cmath>        // std::atan2, std::sqrt, std::ceil, std::tan, std::cos

// Crucible engine
#include "Components/Components.h"            // WorldTransform
#include "Diagnostics/Logger.h"
#include "ECSPhysicsSystem.h"                 // SphereCast, for the agents' probes
#include "Physics/EntityQueries.h"            // EntityHit, EntityFilter
#include "Systems/ECSSystemManager.h"         // FindSystem
#include "Graphics/DebugDraw.h"
#include "Navigation/NavigationComponents.h"  // FlowFieldSurface
#include "Physics/PhysicsComponents.h"        // Collider
#include "Systems/PlayMode.h"
#include "Systems/Query.h"
#include "World.h"

namespace Crucible {

    namespace {

        // Lines are drawn this far above the ground so the floor doesn't hide them.
        constexpr float kDrawLift = 0.05f;

        // Extra length on a sliding-collision probe (Jean David's skin), so an
        // agent stops just short of a wall instead of touching it - touching
        // would make the next frame's probe start inside the wall.
        constexpr float kCollisionSkin = 0.05f;

        // A hit whose surface faces this much upward (about 45 degrees or
        // flatter) is ground to walk on - a ramp, a step - not a wall.
        constexpr float kWalkableNormalY = 0.7f;

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

        // The area a FlowFieldSurface covers, read off its entity's world matrix.
        struct SurfaceArea {

            Math::vec3 centre = Math::vec3(0.0f);
            float      width  = 0.0f;   // metres along X
            float      depth  = 0.0f;   // metres along Z

        };  // SURFACEAREA STRUCT

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
        SurfaceArea AreaOf(const Math::mat4& world) {

            return { Math::vec3(world[3]), glm::length(Math::vec3(world[0])), glm::length(Math::vec3(world[2])) };

        }   // AreaOf()

        // The surface's edge as a bright cyan rectangle at its own height, on top.
        void DrawOutline(const SurfaceArea& area) {

            const DebugStyle outline{ .color = { 0.4f, 0.95f, 1.0f, 1.0f }, .depthTest = false, .category = DebugCategory::NavGrid };

            const float      y = area.centre.y + kDrawLift;
            const float      w = area.width * 0.5f;
            const float      d = area.depth * 0.5f;
            const Math::vec3 a(area.centre.x - w, y, area.centre.z - d);
            const Math::vec3 b(area.centre.x + w, y, area.centre.z - d);
            const Math::vec3 c(area.centre.x + w, y, area.centre.z + d);
            const Math::vec3 e(area.centre.x - w, y, area.centre.z + d);
            DebugDraw::Line(a, b, outline);
            DebugDraw::Line(b, c, outline);
            DebugDraw::Line(c, e, outline);
            DebugDraw::Line(e, a, outline);

        }   // DrawOutline()

        // Unity's Vector3.ClampMagnitude: the same direction, but no longer than max.
        Math::vec3 ClampMagnitude(const Math::vec3& v, float max) {

            const float lengthSq = glm::dot(v, v);
            if (lengthSq <= max * max) return v;
            return v * (max / std::sqrt(lengthSq));

        }   // ClampMagnitude()

        // Reynolds' steering force (Jean David's ToForce): the change of
        // velocity that would turn `velocity` into `desired`, no bigger than
        // maxForce. Every behaviour (follow the field, alignment, avoidance)
        // speaks in these, so they can simply be weighted and added up.
        Math::vec3 Steer(const Math::vec3& desired, const Math::vec3& velocity, float maxForce) {

            return ClampMagnitude(desired - velocity, maxForce);

        }   // Steer()

        // Slack on HasArrived beyond the stopping distance (Jean David's
        // "attackRange + 0.5"): the crowd can hold an agent just outside it.
        constexpr float kArriveSlack = 0.5f;

        // Dynamic agents: how far (m/s) the body's real velocity may be from the
        // agent's own before the agent takes the body's - i.e. physics shoved it.
        // Well above friction's drag between two frames (about 0.03 m/s a step),
        // well below a knockback or a wall stopping it dead.
        constexpr float kShoveSpeed = 1.0f;

        // How fast two overlapping bodies are moved apart: this share of the
        // overlap per second, so 8 clears most of it in about an eighth of a
        // second - quick enough to look solid, slow enough not to snap. It is
        // also never faster than the agent can walk (see CrowdPush's use).
        constexpr float kOverlapResolveRate = 8.0f;

        // Unity's avoidance priority, plus one rule of ours: of two touching
        // agents, how much of the overlap `self` should clear.
        //   0   - `self` is the more important one: the other gives way
        //   0.5 - equals: they share it
        //   1   - `self` is the less important one: it gives way entirely
        // Lower priority numbers are more important (Unity's rule). At equal
        // priority, an agent that has arrived (standing at its attack spot,
        // or stopped) outranks one still on its way, so an enemy already
        // hitting the player isn't shoved off by the next one walking in.
        float YieldShare(int selfPriority, bool selfArrived, int otherPriority, bool otherArrived) {

            if (selfPriority != otherPriority) return selfPriority > otherPriority ? 1.0f : 0.0f;
            if (selfArrived != otherArrived)   return selfArrived ? 0.0f : 1.0f;
            return 0.5f;

        }   // YieldShare()

        /*!
            \brief
            Jean David's Arrive: the velocity that heads straight for `target`
            at full speed, then slows in a straight line from
            `slowingDistance` to a stop at `stoppingDistance`. Shared by
            MoveTo (a point) and Arrive (an entity's position).

            \param stoppingDistance
            Where it stops: m_stoppingDistance for Arrive, the order's own
            for MoveTo (FlowFieldAgent::MoveStoppingDistance).

            \param slowingDistance
            Where it starts slowing: m_slowingDistance for Arrive, the
            order's own for MoveTo (FlowFieldAgent::MoveSlowingDistance).

            \param outDirection
            The flat unit direction to the target (zero if standing on it).

            \param outDistance
            The flat distance to it, for RemainingDistance.
        */
        Math::vec3 ArriveVelocity(const Math::vec3& from, const Math::vec3& target, const FlowFieldAgent& agent,
                                  float stoppingDistance, float slowingDistance, Math::vec3& outDirection, float& outDistance) {

            const Math::vec3 to(target.x - from.x, 0.0f, target.z - from.z);
            outDistance = glm::length(to);
            if (outDistance < 1e-4f) { outDirection = Math::vec3(0.0f); return Math::vec3(0.0f); }
            outDirection = to / outDistance;

            // 1 outside the slowing distance, 0 at the stopping distance, a
            // straight line in between. max(0.01) so slowing <= stopping can't
            // divide by zero (it then just stops dead at the stopping distance).
            const float t = std::clamp((outDistance - stoppingDistance)
                                       / std::max(0.01f, slowingDistance - stoppingDistance), 0.0f, 1.0f);
            return outDirection * agent.m_speed * t;

        }   // ArriveVelocity()

        // A flat unit vector from `from` to the nearest goal, or zero if there is
        // no goal or it is (nearly) right here.
        Math::vec3 TowardsNearest(const Math::vec3& from, const std::vector<Math::vec3>& goals) {

            Math::vec3 best(0.0f);
            float      bestSq = 0.0f;
            for (const Math::vec3& goal : goals) {
                const Math::vec3 to(goal.x - from.x, 0.0f, goal.z - from.z);
                const float      sq = glm::dot(to, to);
                if (best == Math::vec3(0.0f) || sq < bestSq) { best = to; bestSq = sq; }
            }
            if (bestSq < 1e-4f) return Math::vec3(0.0f);   // standing on it: no direction to give
            return best / std::sqrt(bestSq);

        }   // TowardsNearest()

    }   // ANONYMOUS NAMESPACE

    ECSNavigationSystem::ECSNavigationSystem(const PlayMode& playMode, ECSSystemManager& systems)
        : m_playMode(playMode), m_systems(systems) {

        s_active = this;

    }   // ECSNavigationSystem::ECSNavigationSystem()

    // Only clears it if it is still us, so a newer system made before this one
    // is destroyed keeps its claim. Without the clear, C# could reach a freed
    // system after Core shuts down.
    ECSNavigationSystem::~ECSNavigationSystem() {

        if (s_active == this) s_active = nullptr;

    }   // ECSNavigationSystem::~ECSNavigationSystem()

    bool ECSNavigationSystem::IsWalkable(const Math::vec3& position) const {

        // IsBlockedWorld already says "blocked" while unbaked, so no separate check.
        return !m_grid.IsBlockedWorld(position) && !m_terrain->IsWall(position);

    }   // ECSNavigationSystem::IsWalkable()

    bool ECSNavigationSystem::IsPathClear(const Math::vec3& from, const Math::vec3& to) const {

        if (!m_grid.IsBaked()) return false;

        // Half a cell per step (at least 20 cm), so the line can't skip over a
        // one-cell wall between two samples. Unity's spacing exactly.
        const Math::vec3 delta  = to - from;
        const float      length = glm::length(Math::vec3(delta.x, 0.0f, delta.z));
        const int        steps  = std::max(1, static_cast<int>(std::ceil(length / std::max(0.2f, m_grid.CellSize() * 0.5f))));

        // Still inside the blocked cells it started in: those don't count (see the header).
        bool leavingBlockedStart = m_grid.IsBlockedWorld(from);
        for (int i = 0; i <= steps; ++i) {

            const Math::vec3 sample = from + delta * (static_cast<float>(i) / static_cast<float>(steps));
            if (m_terrain->IsWall(sample)) return false;
            if (m_grid.IsBlockedWorld(sample)) {
                if (!leavingBlockedStart) return false;
            } else {
                leavingBlockedStart = false;
            }

        }
        return true;

    }   // ECSNavigationSystem::IsPathClear()

    /*!
        \brief
        The agents' probe: a sphere the size of the agent's body, swept
        horizontally, against walls and crates. Shared by avoidance (look
        ahead) and sliding collision (check the move).

        \details
        Swept from knee to shoulder: its centre sits m_stepHeight + radius
        above the feet, so it covers the same height band the grid bakes
        obstacles from - what the grid calls a wall, the probe does too.
        (Jean David sweeps it 1 m above the entity's position, which on our
        centred capsule would pass over knee-high crates.)

        Only NonMoving and Moving are hit: the player is the goal, not an
        obstacle, and other enemies are the crowd push's job. Filtering them out
        in the cast itself also means an enemy standing in front of a wall
        can't hide the wall, which only looking at the nearest hit would.

        \return
        True if something that counts as an obstacle is in the way; `hit`
        then says where. Ground (a surface facing mostly up) doesn't count.
    */
    bool ECSNavigationSystem::Probe(Entity self, const FlowFieldAgent& agent, const Math::vec3& position, const Math::vec3& direction,
                                    float distance, EntityHit& hit) const {

        if (!m_physics) return false;     // no physics registered: no collision, rather than no movement

        const float feet = position.y - agent.m_baseOffset;
        const Math::vec3 origin(position.x, feet + m_stepHeight + agent.m_radius, position.z);

        EntityFilter filter;
        filter.layers = LayerMask(PhysicsLayer::NonMoving, PhysicsLayer::Moving);
        filter.ignore = self;

        if (!m_physics->SphereCast(origin, agent.m_radius, direction, hit, distance, filter)) return false;
        return hit.normal.y <= kWalkableNormalY;

    }   // ECSNavigationSystem::Probe()

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
        if (m_lastState == PlayState::Edit && state == PlayState::Playing) { m_bakePending = true; m_physicsStepped = false; }

        // A script changed the level mid-Play (RequestBake): bake again, and wait
        // for a physics step AFTER this frame, because the step physics reported
        // last frame came before the new bodies existed.
        const bool rebakeNow = m_rebakeRequested && state == PlayState::Playing;
        m_rebakeRequested = false;
        if (rebakeNow) { m_bakePending = true; m_physicsStepped = false; }

        // And not before physics has stepped once: its bodies are created when
        // Play starts (it runs after this system, in PostUpdate), and the bake
        // samples the ground heights from them. Without the wait, the first
        // bake's rays would find nothing and the level would bake flat. It is
        // a frame or two; with no physics system at all there is nothing to wait for.
        if (!m_physics) m_physics = m_systems.FindSystem<ECSPhysicsSystem>();
        if (!rebakeNow && state == PlayState::Playing && (!m_physics || m_physics->GetStepsLastFrame() > 0)) m_physicsStepped = true;

        // Not on the very frame Play starts, though, if the surface's world
        // matrix isn't worked out yet: the Game starts Playing on its first
        // frame, before HierarchyTransformSystem has run, and baking then read
        // an identity matrix - a 1 x 1 m grid at the origin, so no flow field.
        // (The Editor never hit this: its transforms exist long before Play.)
        // No surface at all bakes at once, so Bake's warning still comes.
        bool surfaceReady = true;
        world.CreateQuery<const FlowFieldSurface, const WorldTransform>()
            .ForEach([&](Entity, const FlowFieldSurface&, const WorldTransform& transform) {
                if (!transform.valid) surfaceReady = false;
            });

        if (m_bakePending && state == PlayState::Playing && surfaceReady && m_physicsStepped) {

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
            m_goalsAtBuild.clear();
            m_bakePending    = false;
            m_warnedParented = false;
        }
        m_lastState = state;

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
                DrawOutline(AreaOf(transform.matrix));
            });

        // No surface any more (removed, switched off, or a scene without one):
        // the grid describes something that isn't there, so stop showing it.
        if (!hasSurface) {
            m_field.Clear();     // first: it points at the grid being reset
            m_grid = NavGrid{};
            m_goalsAtBuild.clear();
        }

        // Only while Playing: Paused freezes the field and the agents along
        // with everything else, and in Edit there is no session to serve.
        // Field first, then agents, so every agent reads this frame's field.
        if (state == PlayState::Playing) {
            CollectGoals(world);
            if (m_grid.IsBaked()) UpdateField(surface);
            UpdateAgents(world);
        }

        if (m_grid.IsBaked())   DrawGrid();
        if (m_field.HasFlow())  DrawArrows();

    }   // ECSNavigationSystem::Update()

    // Every player's position this frame: the field's goals, and where agents
    // head when the field has no arrow for them. Gathered even with no grid,
    // so agents can still go straight at a player on a scene without a surface.
    void ECSNavigationSystem::CollectGoals(World& world) {

        m_goals.clear();
        m_goalEntities.clear();
        // IncludeDisabled: a scripted player (MechPlayer) switches its PlayerController off and
        // moves itself, but it is still the player. Its CharacterController is only off when
        // the whole entity was (Entity.SetActive(false)), and then it is out of the game.
        world.BuildQuery<const PlayerController, const WorldTransform>().IncludeDisabled().Build()
            .ForEach([&](Entity player, const PlayerController&, const WorldTransform& transform) {
                if (!transform.valid) return;
                if (world.HasComponent<CharacterController>(player) && !world.IsComponentActive<CharacterController>(player)) return;
                m_goals.push_back(Math::vec3(transform.matrix[3]));
                m_goalEntities.push_back(player);   // same index as its position
            });

    }   // ECSNavigationSystem::CollectGoals()

    // The player nearest `from` (flat distance), or kNullEntity if there is none.
    Entity ECSNavigationSystem::NearestGoalEntity(const Math::vec3& from) const {

        Entity nearest   = kNullEntity;
        float  nearestSq = 0.0f;
        for (std::size_t i = 0; i < m_goals.size(); ++i) {
            const float dx = m_goals[i].x - from.x;
            const float dz = m_goals[i].z - from.z;
            const float sq = dx * dx + dz * dz;
            if (nearest == kNullEntity || sq < nearestSq) { nearest = m_goalEntities[i]; nearestSq = sq; }
        }
        return nearest;

    }   // ECSNavigationSystem::NearestGoalEntity()

    /*!
        \brief
        Points the flow field at every player, rebuilding it only once one of
        them has moved m_rebuildDistance since the last rebuild.

        \details
        No player this frame (none in the scene, or the player died or
        despawned): nothing is rebuilt, so the last field stays as it was,
        frozen and still pointing at where the player was last. That is the
        "search" behaviour - enemies head for the last-known spot instead of
        stopping dead. With no field ever built there is simply nothing to
        follow, and enemies wander.
    */
    void ECSNavigationSystem::UpdateField(const FlowFieldSurface& surface) {

        if (m_goals.empty()) return;

        // Due: no field built for this bake yet, a player joined or left, or
        // any player walked far enough. Measured flat (XZ) - jumping on the
        // spot doesn't change which cell is the goal, so it shouldn't rebuild.
        bool due = m_goalsAtBuild.size() != m_goals.size();
        const float rebuildSq = surface.m_rebuildDistance * surface.m_rebuildDistance;
        for (std::size_t i = 0; !due && i < m_goals.size(); ++i) {
            const float dx = m_goals[i].x - m_goalsAtBuild[i].x;
            const float dz = m_goals[i].z - m_goalsAtBuild[i].z;
            due = dx * dx + dz * dz >= rebuildSq;
        }
        if (!due) return;

        // Recorded even if the rebuild fails (a player sealed inside walls), so
        // a failure isn't retried every frame - only after they move again.
        m_field.Rebuild(m_goals, surface.m_floodRadius);
        m_goalsAtBuild = m_goals;

    }   // ECSNavigationSystem::UpdateField()

    /*!
        \brief
        Moves every FlowFieldAgent one frame, by what it was told to do: Jean
        David's agent update for one state (Chase), with the crowd and
        collision.

        \details
        Two passes, so every agent reacts to where the others were at the
        START of the frame. In a single pass, agent 5 would see agent 4
        already moved and agent 6 not yet - the order of the list would leak
        into how the crowd spreads.

        Pass 1, per agent: the checks, m_startWith once, then a snapshot of
        its position and velocity, filed in the spatial hash.

        Pass 2, per agent:
          1. the velocity it WANTS from its movement, as a steering force
          2. the crowd: the overlap push (Unity's rule) and alignment
          3. avoidance: a swerve round whatever is ahead
          4. add them up, capped by m_acceleration, and steer
          5. move, plus the overlap push, sliding along anything still in the way
          6. turn to face where it is going, limited by m_angularSpeed
          7. stand on the ground
    */
    void ECSNavigationSystem::UpdateAgents(World& world) {

        // A zero or broken (NaN) frame time must move nothing: a NaN would
        // spread into every agent's position and never leave.
        const float dt = world.DeltaTime();
        if (!(dt > 0.0f)) return;

        // Physics is registered after this system, so it can't be handed over
        // at construction; find it once, the first time agents move.
        if (!m_physics) m_physics = m_systems.FindSystem<ECSPhysicsSystem>();

        // --- Pass 1: who is where ------------------------------------------ //

        m_agents.clear();
        m_hash.Clear();
        float largestRadius = 0.0f;     // so a crowd query always reaches every body that could touch
        world.CreateQuery<FlowFieldAgent, Transform>()
            .ForEach([&](Entity entity, FlowFieldAgent& agent, Transform& transform) {

                // A Transform is relative to its parent, so writing a world
                // position into it is only right for a root. Skip (and say
                // so once) rather than send a child flying.
                if (!(world.GetParent(entity) == kNullEntity)) {
                    if (!m_warnedParented) {
                        CRU_WARN("Navigation: the FlowFieldAgent on entity {} has a parent - agents must be root entities, so it won't move", entity.index);
                        m_warnedParented = true;
                    }
                    return;
                }

                // Two ways to be moved (see DYNAMIC AGENTS in UpdateAgents):
                //   kinematic - no RigidBody, or a kinematic one: the agent moves
                //               the Transform itself and stands it on the ground;
                //   dynamic   - a dynamic RigidBody: physics moves it (gravity,
                //               collisions, knockback, explosions) and the agent
                //               only steers its sideways velocity.
                // A dynamic agent keeps its OWN velocity from last frame - what it is
                // steering at - and each physics step tops the body up to it (step 5).
                // That way the ground's friction, which slows the body every step, is
                // made up every step too. Re-reading the body's velocity instead let
                // friction feed back into the steering: under about 2 m/s wanted (the
                // end of an Arrive) the steering's pull was weaker than friction's
                // drag, and maces stalled 3-7 m from the player, then lurched.
                // It takes the body's velocity instead when physics has clearly moved
                // it somewhere else - a blast, a knockback, a wall stopping it, a
                // landing - so a shove is what it steers back from, as before.
                // Once the RigidBody's impulse is empty, the queued top-up was used.
                const RigidBody* body = world.Read<RigidBody>(entity);
                const bool dynamic = body != nullptr && !body->isKinematic;
                Math::vec3 bodyVelocity(0.0f);
                if (dynamic) {
                    if (body->impulse == Math::vec3(0.0f)) agent.m_pendingSteer = Math::vec3(0.0f);
                    bodyVelocity = Math::vec3(body->velocity.x, 0.0f, body->velocity.z);
                    const Math::vec3 expected = bodyVelocity + agent.m_pendingSteer;   // where the body is headed once the queue lands
                    if (glm::length(expected - agent.m_velocity) > kShoveSpeed) agent.m_velocity = expected;
                }

                // 1. The Inspector's "start with", applied the first frame the
                // agent is seen in Play - which also covers one spawned later.
                // ArrivePlayer needs a player to arrive at: with none yet, it
                // stays unstarted and tries again next frame, so an enemy placed
                // before the player spawns still goes for them.
                if (!agent.m_started) {
                    switch (agent.m_startWith) {
                        case AgentStart::Nothing:     agent.m_started = true; break;
                        case AgentStart::FollowField: agent.FollowField(); agent.m_started = true; break;
                        case AgentStart::ArrivePlayer: {
                            const Entity player = NearestGoalEntity(transform.positions);
                            if (!(player == kNullEntity)) { agent.Arrive(player); agent.m_started = true; }
                            break;
                        }
                    }
                }

                // Pointers into the component storage are safe until this
                // function ends: nothing here adds or removes components, so
                // nothing moves in memory.
                const int index = static_cast<int>(m_agents.size());
                const float radius = std::max(agent.m_radius, 0.0f);
                m_agents.push_back({ entity, &agent, &transform, transform.positions, agent.m_velocity,
                                     radius, agent.m_avoidancePriority, agent.m_arrived, dynamic, bodyVelocity });
                m_hash.Insert(index, transform.positions);
                largestRadius = std::max(largestRadius, radius);

            });

        // The crowd, from this frame's neighbours (m_neighbours) and the
        // snapshots - never from an agent pass 2 has already moved.
        //
        // CrowdPush (Unity's NavMeshAgent rule, not boids separation): only
        // bodies that OVERLAP push - closer than the two radii added up - and
        // by how much they overlap, not at full speed. Agents standing side by
        // side don't push at all, so nothing shoves a crowd away from its
        // target. Of each touching pair, YieldShare says how much of the
        // overlap this agent clears. Returns that as metres to move.
        const auto crowdPush = [&](std::size_t i) {

            const AgentRef& self = m_agents[i];

            Math::vec3 push(0.0f);
            for (const int j : m_neighbours) {

                const AgentRef& other = m_agents[static_cast<std::size_t>(j)];
                const float     reach = self.radius + other.radius;

                Math::vec3  away(self.start.x - other.start.x, 0.0f, self.start.z - other.start.z);
                const float distanceSq = glm::dot(away, away);
                if (distanceSq >= reach * reach) continue;      // not touching: no push at all

                const float share = YieldShare(self.priority, self.arrived, other.priority, other.arrived);
                if (share == 0.0f) continue;                    // the other one gives way

                float distance = std::sqrt(distanceSq);
                if (distance < 0.001f) {
                    // Exactly on top of each other (spawned at one spot): there
                    // is no "away". Jean David picks a random direction; this
                    // picks one from the agent's number instead - different for
                    // each agent, the same every frame. The golden angle (2.4
                    // rad) spreads consecutive numbers evenly round the circle.
                    const float angle = static_cast<float>(i) * 2.39996f;
                    away     = Math::vec3(std::cos(angle), 0.0f, std::sin(angle));
                    distance = 0.0f;
                } else {
                    away /= distance;
                }
                push += away * ((reach - distance) * share);

            }
            return push;

        };

        // Alignment: the neighbours' average velocity, as the heading to match.
        const auto alignment = [&](const FlowFieldAgent& agent) {

            if (m_neighbours.empty()) return Math::vec3(0.0f);
            Math::vec3 sum(0.0f);
            for (const int j : m_neighbours) sum += m_agents[static_cast<std::size_t>(j)].velocity;
            const Math::vec3 average = sum / static_cast<float>(m_neighbours.size());
            if (glm::dot(average, average) < 1e-4f) return Math::vec3(0.0f);   // they're standing still: nothing to match
            return Steer(glm::normalize(average) * agent.m_speed, agent.m_velocity, agent.m_acceleration);

        };

        // --- Pass 2: steer and move ---------------------------------------- //

        const DebugStyle velocityStyle{ .color = { 0.3f, 1.0f, 0.4f, 1.0f }, .depthTest = false, .category = DebugCategory::Velocities };
        const DebugStyle pathStyle    { .color = { 0.9f, 0.9f, 0.9f, 1.0f }, .depthTest = false };   // agent -> MoveTo point / Arrive target (Gameplay)

        for (std::size_t i = 0; i < m_agents.size(); ++i) {

            FlowFieldAgent& agent     = *m_agents[i].agent;
            Transform&      transform = *m_agents[i].transform;
            Math::vec3&     position  = transform.positions;

            // 1. The velocity it wants. FollowField: the arrow under it,
            // or - where the field has none (outside the flood radius, the
            // player's own cell, no surface at all) - straight at the
            // nearest player, Jean David's fallback. None: zero, so it
            // slows to a stop instead of freezing mid-stride. Turned into
            // a steering force: the change of velocity that would get it
            // there, no bigger than m_acceleration.
            //
            // MoveTo and Arrive: straight at the point / the target entity,
            // slowing from m_slowingDistance to a stop at m_stoppingDistance.
            // Both face where they walk, as Unity's Arrive does when its graph
            // drives it (Jean David's Arrive faced the target; Gam3333's main
            // turned that off, so a golem stepping round the crowd turns with
            // its steps instead of strafing). FaceTarget() still overrides.
            //
            // Each also fills in how it's going: RemainingDistance and
            // HasArrived, which the brain and the attack code read.
            Math::vec3 direction(0.0f);     // the way it is trying to go: avoidance picks its side from this
            Math::vec3 desired(0.0f);
            Math::vec3 lookAt(0.0f);        // a point to face instead of its velocity (FaceTarget)
            bool       facesTarget = false;
            bool       hasGoal     = false; // something to measure the distance to
            float      remaining   = 0.0f;
            float      stopAt      = agent.m_stoppingDistance;   // MoveTo may bring its own

            switch (agent.m_movement) {

                case AgentMovement::None:
                    break;

                case AgentMovement::FollowField: {
                    direction = m_field.SampleDirection(position);
                    if (direction == Math::vec3(0.0f)) direction = TowardsNearest(position, m_goals);
                    desired = direction * agent.m_speed;

                    // Its distance to the nearest player, so HasArrived means
                    // "close enough to switch to Arrive or attack" here too.
                    for (const Math::vec3& player : m_goals) {
                        const float distance = glm::length(Math::vec3(player.x - position.x, 0.0f, player.z - position.z));
                        if (!hasGoal || distance < remaining) { remaining = distance; hasGoal = true; }
                    }
                    break;
                }

                case AgentMovement::MoveTo:
                    stopAt  = agent.MoveStoppingDistance();
                    desired = ArriveVelocity(position, agent.m_destination, agent, stopAt, agent.MoveSlowingDistance(), direction, remaining);
                    hasGoal = true;
                    DebugDraw::Line(position, agent.m_destination, pathStyle);
                    break;

                case AgentMovement::Arrive: {
                    // A target that has died or lost its WorldTransform: stand
                    // still, and don't claim to have arrived at it.
                    const WorldTransform* target = world.IsAlive(agent.m_target) ? world.Read<WorldTransform>(agent.m_target) : nullptr;
                    if (!target || !target->valid) break;

                    const Math::vec3 goal(target->matrix[3]);
                    desired = ArriveVelocity(position, goal, agent, stopAt, agent.m_slowingDistance, direction, remaining);
                    hasGoal = true;
                    DebugDraw::Line(position, goal, pathStyle);
                    break;
                }

            }

            // FaceTarget(entity): face that entity whatever the order is, like
            // Unity's updateRotation = false with the script turning it - Unity's
            // FaceTarget flag, set while positioning (the pressure walk, a
            // crossbow's firing spot, a retreat, aiming). A crossbow walking
            // sideways to its spot then reads as strafing, not as swinging round.
            if (world.IsAlive(agent.m_faceTarget)) {
                if (const WorldTransform* face = world.Read<WorldTransform>(agent.m_faceTarget); face && face->valid) {
                    lookAt      = Math::vec3(face->matrix[3]);
                    facesTarget = true;
                }
            }

            // Stopped: nothing left to reach, so "arrived" (Unity reads a path-less
            // agent's remainingDistance as 0 too). Otherwise within the stopping
            // distance plus Jean David's 0.5 m of slack - the crowd can hold an
            // agent just outside it, and it must still count as in range.
            agent.m_remainingDistance = remaining;
            agent.m_arrived = agent.m_movement == AgentMovement::None
                           || (hasGoal && remaining <= stopAt + kArriveSlack);

            Math::vec3 force = Steer(desired, agent.m_velocity, agent.m_acceleration);

            // Stopped: brake as hard as m_acceleration allows, Unity's Hold
            // (EnemyAgent: -velocity / dt, clamped to maxForce) - 15 m/s^2 stops
            // a golem from walking pace in under half a second. The ordinary
            // steer above only pulls by the speed left (it halves about every
            // 0.7 s), so a golem told to hold in range coasted on into the player
            // and shoved them. Ground friction used to hide that on dynamic golems.
            if (agent.m_movement == AgentMovement::None) {
                force = ClampMagnitude(-agent.m_velocity / std::max(dt, 1e-4f), agent.m_acceleration);
            }

            // 2. The crowd, whatever it was told, so even standing agents are
            // kept from overlapping. The query reaches at least far enough to
            // find every body that could touch this one (its radius plus the
            // largest), whatever m_neighbourRadius says. The overlap push is
            // not a force: it is applied to the move in step 5, so it can't
            // build up speed and carry the agent on past the gap.
            m_hash.Query(m_agents[i].start, std::max(agent.m_neighbourRadius, m_agents[i].radius + largestRadius),
                         static_cast<int>(i), m_neighbours);
            const Math::vec3 push = crowdPush(i);

            // Alignment only while following the field: it is what makes a horde
            // pour round a corner as one. Under Arrive, MoveTo or standing still
            // it did harm - it steers towards the neighbours' heading at 0.4 x full
            // speed, which Arrive's brake (zero wish to move at its stop) balances
            // at 2.4 / 1.4 = 1.7 m/s. Measured in Golem_test: every enemy that had
            // arrived slid on at a steady 1.70 m/s, and as the drifters aligned
            // with each other the whole group slid away from the player together.
            // (Unity's NavMeshAgent has no alignment at all.)
            if (agent.m_alignmentWeight != 0.0f && agent.m_movement == AgentMovement::FollowField)
                force += alignment(agent) * agent.m_alignmentWeight;

            // 3. Avoidance: look m_avoidanceDistance ahead along its heading.
            // Something there: push sideways round it - along the surface
            // (the tangent), picking the side towards where it is trying to
            // go, plus half a push straight off it - stronger the closer it
            // is. So it curves round a pillar instead of meeting it and
            // sliding. Below 0.1 m/s there is no heading to look along.
            EntityHit  hit;
            const float speedSq = glm::dot(agent.m_velocity, agent.m_velocity);
            if (agent.m_avoidanceWeight != 0.0f && speedSq >= 0.01f) {

                const Math::vec3 heading = agent.m_velocity / std::sqrt(speedSq);
                if (Probe(m_agents[i].entity, agent, position, heading, agent.m_avoidanceDistance, hit)) {

                    Math::vec3 normal(hit.normal.x, 0.0f, hit.normal.z);
                    normal = glm::dot(normal, normal) > 1e-6f ? glm::normalize(normal) : -heading;

                    Math::vec3 tangent = glm::cross(Math::vec3(0.0f, 1.0f, 0.0f), normal);
                    if (glm::dot(tangent, direction) < 0.0f) tangent = -tangent;

                    const float closeness = 1.0f - std::clamp(hit.distance / agent.m_avoidanceDistance, 0.0f, 1.0f);
                    const Math::vec3 swerve = glm::normalize(normal * 0.5f + tangent) * agent.m_speed;
                    force += Steer(swerve, agent.m_velocity, agent.m_acceleration) * closeness * agent.m_avoidanceWeight;

                }

            }

            // 4. Steer: the sum, capped again by m_acceleration, so however
            // the forces stack an agent is never shoved harder than it can
            // steer. That cap is also what turns the field's 8 blocky
            // directions into smooth curves.
            // A dynamic agent may be going faster than m_speed - knocked back or
            // blown away. Steering then brakes it at m_acceleration (it wants
            // m_speed at most), so the shove plays out and fades instead of
            // being cut off; the cap only stops steering ADDING speed past that.
            // The cap is measured from the BODY's speed (what physics last left
            // it), never from m_velocity: for a dynamic agent that also holds the
            // steering queued for the next physics step, so a cap read from it
            // grows with whatever was queued - see the impulse in step 5.
            const bool  dynamic  = m_agents[i].dynamic;
            const float speedCap = dynamic ? std::max(agent.m_speed, glm::length(m_agents[i].bodyVelocity)) : agent.m_speed;
            agent.m_velocity += ClampMagnitude(force, agent.m_acceleration) * dt;
            agent.m_velocity.y = 0.0f;
            agent.m_velocity  = ClampMagnitude(agent.m_velocity, speedCap);

            // Giving way: the part of its velocity that heads into the bodies it
            // is being moved out of is turned SIDEWAYS instead, so it walks round
            // the agent holding its spot - the way NavMeshAgents file round each
            // other - rather than leaning on it. Only dropping that part (like
            // sliding on a wall) was tried first: an agent heading straight at the
            // player through another one then had nothing left and just stood
            // behind it, out of range, with the far side of the player empty.
            // The side is fixed per agent (odd/even), so it doesn't dither left
            // and right; but if it is already moving sideways, it keeps that side.
            if (glm::dot(push, push) > 1e-8f) {
                const Math::vec3 out  = glm::normalize(push);
                const float      into = glm::dot(agent.m_velocity, out);
                if (into < 0.0f) {
                    agent.m_velocity -= out * into;
                    Math::vec3 side = glm::cross(Math::vec3(0.0f, 1.0f, 0.0f), out);
                    const float sideways = glm::dot(agent.m_velocity, side);
                    if (sideways < -0.05f || (sideways <= 0.05f && (i % 2) == 1)) side = -side;
                    agent.m_velocity += side * (-into);
                }
            }

            // 5. Move - the hard limit no force can overpower. The move is the
            // velocity's, plus a share of the crowd's overlap (kOverlapResolveRate
            // per second), that share never faster than the agent walks. Probe
            // along the move first. Something in the way: keep only the part of
            // the move (and of the velocity, so it stops pushing) that runs
            // ALONG the surface, and slide. If even the slide is blocked - a
            // corner - stay put this frame. So the crowd can't push an agent
            // through a wall either.
            const Math::vec3 crowdMove = ClampMagnitude(push * std::min(1.0f, kOverlapResolveRate * dt), agent.m_speed * dt);

            // DYNAMIC AGENTS: physics owns the position. The agent gets the sideways
            // (XZ) velocity it wants - plus the crowd's push as a speed - by ADDING
            // the difference as an impulse (mass x change of velocity), and leaves Y
            // to gravity. Setting RigidBody::velocity instead was tried first and
            // wiped out every other push that frame: an explosion "reached 8
            // entities" in the test and none of them moved. Added, a blast or a
            // knockback lands on top, and next frame steering brakes it at
            // m_acceleration. The Game draws many frames per physics step, so the
            // agent remembers the change it has queued (m_pendingSteer) and only
            // tops it up to what it now wants: every frame's steering counts, and
            // none is queued twice. (Skipping frames whose impulse was still waiting
            // was tried first: at hundreds of frames per second each step got only
            // one frame's 0.02 m/s, less than the ground's friction, and a mace sat
            // at its spawn point for seconds.) Walls, slopes, landing and other
            // bodies are Jolt's collisions, so no probe and no ground snap.
            // While it is off the ground (thrown by an explosion, falling off a
            // ledge) it doesn't steer at all: it flies as physics says, and takes
            // control back when it lands - like a NavMeshAgent with updatePosition
            // off, its Rigidbody switched to physics until it is grounded again.
            // Its facing is still the agent's: the prefab locks the body's rotation,
            // so collisions can't tip it over, and step 6 turns it.
            const bool airborne = dynamic && !OnGround(m_agents[i].entity, agent, position);
            if (dynamic) {
                RigidBody* body = world.GetComponent<RigidBody>(m_agents[i].entity);
                if (!airborne && body && body->mass > 0.0f) {
                    // Steering plus the crowd's push, capped TOGETHER at speedCap.
                    // Uncapped, the push stacked up: next frame m_velocity reads the
                    // body's velocity plus this queued change - push included - and
                    // the push was added on top again. With several frames drawn
                    // per physics step, overlapping golems (two spawned on one spot,
                    // one walking into another) queued 20-40 m/s and the step fired
                    // it all at once: a launch, that once threw the player off the map.
                    const Math::vec3 wanted = ClampMagnitude(agent.m_velocity + crowdMove / dt, speedCap);
                    const Math::vec3 needed(wanted.x - m_agents[i].bodyVelocity.x, 0.0f, wanted.z - m_agents[i].bodyVelocity.z);
                    body->impulse      += (needed - agent.m_pendingSteer) * body->mass;   // only the top-up; other kicks stay
                    agent.m_pendingSteer = needed;
                }
            }

            Math::vec3  motion   = agent.m_velocity * dt + crowdMove;
            const float distance = dynamic ? 0.0f : glm::length(motion);   // a dynamic agent's move is physics'
            if (distance > 1e-5f) {

                bool blocked = false;
                if (Probe(m_agents[i].entity, agent, position, motion / distance, distance + kCollisionSkin, hit)) {

                    Math::vec3 normal(hit.normal.x, 0.0f, hit.normal.z);
                    normal = glm::dot(normal, normal) > 1e-6f ? glm::normalize(normal) : -motion / distance;

                    // Unity's Vector3.ProjectOnPlane: take away the part pointing into the wall.
                    motion           -= normal * glm::dot(motion, normal);
                    agent.m_velocity -= normal * glm::dot(agent.m_velocity, normal);

                    const float slide = glm::length(motion);
                    blocked = slide < 1e-5f || Probe(m_agents[i].entity, agent, position, motion / slide, slide + kCollisionSkin, hit);

                }
                if (!blocked) position += motion;

            }

            // 6. Face where it is going - or its FaceTarget (the model is
            // assumed to face +Z, as in his prototype). Below 0.1 m/s, or within
            // 0.1 m of the target, the heading is noise, so it keeps its last
            // facing. Yaw only: it always stays upright.
            // Stopped, it doesn't turn at all unless told to face something -
            // Unity's Hold skips its Face() - so a golem holding in range or
            // mid-swing keeps the facing it stopped with, and a shove while it
            // stands doesn't spin it round to the shove.
            const Math::vec3 look = facesTarget ? Math::vec3(lookAt.x - position.x, 0.0f, lookAt.z - position.z)
                                                : agent.m_velocity;
            const bool holding = agent.m_movement == AgentMovement::None && !facesTarget;
            if (glm::dot(look, look) >= 0.01f && !airborne && !holding) {

                const Math::vec3 forward = transform.rotation * Math::vec3(0.0f, 0.0f, 1.0f);
                const float      current = std::atan2(forward.x, forward.z);
                const float      target  = std::atan2(look.x, look.z);

                // The shortest way round: wrap the difference into -pi..pi,
                // so turning from 170 to -170 degrees is 20 degrees, not 340.
                float difference = target - current;
                while (difference >  glm::pi<float>()) difference -= glm::two_pi<float>();
                while (difference < -glm::pi<float>()) difference += glm::two_pi<float>();

                const float maxTurn = glm::radians(agent.m_angularSpeed) * dt;
                const float turn    = std::clamp(difference, -maxTurn, maxTurn);
                transform.rotation  = glm::angleAxis(current + turn, Math::vec3(0.0f, 1.0f, 0.0f));

            }

            // 7. Stand on whatever is under it, feet m_baseOffset below its
            // position (StandOnGround). A dynamic agent has gravity for that.
            if (!dynamic) StandOnGround(m_agents[i].entity, agent, position, dt);

            // Its velocity, half a second's worth, under Gizmos > Velocities:
            // the push-apart and the curving are visible at a glance.
            DebugDraw::Arrow(position, position + agent.m_velocity * 0.5f, velocityStyle);

        }

    }   // ECSNavigationSystem::UpdateAgents()

    /*!
        \brief
        Is there walkable ground just under the agent's feet? For dynamic agents:
        off the ground (thrown, falling) they don't steer.

        \details
        A ray down from 0.3 m above its position to 0.3 m below its feet
        (m_baseOffset below the position), on the level's layers only
        (NonMoving, Moving), skipping the agent itself. 0.3 m of slack so
        walking over bumps and down gentle slopes still counts as grounded,
        while an explosion's lift clearly doesn't. Starting at the position, not
        at the feet, keeps it working when m_baseOffset is too big for the
        collider (a root at the feet, baseOffset left at 1): the ray then just
        reaches further. Starting at the computed feet, it began inside the
        floor for such a crossbow, and the agent thought it was airborne.
    */
    bool ECSNavigationSystem::OnGround(Entity self, const FlowFieldAgent& agent, const Math::vec3& position) const {

        constexpr float kSlack = 0.3f;
        if (!m_physics) return true;     // no physics: nothing can throw it, so treat it as standing

        // Other enemies count as ground here: an explosion can drop a golem on top of
        // another (seen in the test), and one that thought it was in the air there
        // would never steer, so it would stand on its friend forever. Steering, it
        // walks off.
        EntityFilter filter;
        filter.layers = LayerMask(PhysicsLayer::NonMoving, PhysicsLayer::Moving).With(PhysicsLayer::Enemy);
        filter.ignore = self;

        EntityHit hit;
        return m_physics->Raycast(Math::vec3(position.x, position.y + kSlack, position.z), Math::vec3(0.0f, -1.0f, 0.0f), hit,
                                  std::max(agent.m_baseOffset, 0.0f) + kSlack * 2.0f, filter)
            && hit.normal.y > kWalkableNormalY;

    }   // ECSNavigationSystem::OnGround()

    /*!
        \brief
        Puts an agent's feet on the surface under it: Gam3333's EnemyAgent.SnapToGround.

        \details
        A ray goes straight down from half a metre above its middle. Only the
        level counts as ground (NonMoving and Moving layers: floors, ramps,
        crate tops), never the agent itself, other enemies or players, and
        only a surface facing mostly up (kWalkableNormalY) - a wall it brushes
        is not a floor.
          - Ground within reach (down to kGroundSnapRange below its feet): ease
            the feet onto it at kGroundSnapSpeed, so walking up and down a ramp
            looks smooth instead of stair-stepping. An agent's first frame
            snaps straight onto it instead (Unity's Start), so a spawn point
            set a little too high doesn't make it slide down from the air.
          - Ground further below (off a ledge): fall, gaining kFallGravity m/s
            every second, and land on it - a real fall, not a teleport.
          - No ground at all under it (off the edge of the level): the old
            flat plane at the surface's height, if a surface is baked;
            otherwise its height is left alone.

        Before this, every agent stood on that flat plane: in Golem_test the
        Ground box's top is at y = 0.25 but its entity at y = 0, so every
        golem's feet were 25 cm inside the floor, and ramps were ignored.
    */
    void ECSNavigationSystem::StandOnGround(Entity self, FlowFieldAgent& agent, Math::vec3& position, float dt) const {

        // Unity's values: snapRange 2 m below the probe start, snapSpeed 12, fallGravity 25.
        constexpr float kProbeLift        = 0.5f;    // the ray starts this far above the agent's middle
        constexpr float kGroundSnapRange  = 1.0f;    // metres below the feet still counted as "standing on it"
        constexpr float kGroundSnapSpeed  = 12.0f;   // m/s the feet ease up or down onto it
        constexpr float kFallGravity      = 25.0f;   // m/s^2, the mech's own gravity
        constexpr float kFallSearchRange  = 50.0f;   // how far down to look for somewhere to land

        const Math::vec3 origin(position.x, position.y + kProbeLift, position.z);
        const Math::vec3 down(0.0f, -1.0f, 0.0f);
        const float      toFeet = kProbeLift + agent.m_baseOffset;

        EntityFilter filter;
        filter.layers = LayerMask(PhysicsLayer::NonMoving, PhysicsLayer::Moving);
        filter.ignore = self;

        EntityHit hit;
        const bool found = m_physics && m_physics->Raycast(origin, down, hit, toFeet + kFallSearchRange, filter)
                        && hit.normal.y > kWalkableNormalY;

        if (found && hit.distance <= toFeet + kGroundSnapRange) {

            const float target = hit.point.y + agent.m_baseOffset;
            if (!agent.m_grounded) {
                position.y = target;                                    // first frame: straight onto it
            } else {
                const float step = kGroundSnapSpeed * dt;               // Mathf.MoveTowards
                position.y = std::abs(target - position.y) <= step ? target : position.y + (target > position.y ? step : -step);
            }
            agent.m_fallSpeed = 0.0f;
            agent.m_grounded  = true;
            return;

        }

        if (found) {

            // Off a ledge: fall like simple gravity, and stop on the ground below.
            agent.m_fallSpeed += kFallGravity * dt;
            const float landing = hit.point.y + agent.m_baseOffset;
            position.y = std::max(position.y - agent.m_fallSpeed * dt, landing);
            if (position.y <= landing) agent.m_fallSpeed = 0.0f;
            agent.m_grounded = true;
            return;

        }

        // Nothing under it: the flat plane, as before, or leave it be.
        agent.m_fallSpeed = 0.0f;
        if (m_grid.IsBaked()) position.y = m_terrain->GroundHeight(position) + agent.m_baseOffset;

    }   // ECSNavigationSystem::StandOnGround()

    bool ECSNavigationSystem::Bake(World& world) {

        // 1. The surface: its settings, and its entity's position and scale as
        // the area it covers.
        FlowFieldSurface surface;
        SurfaceArea      area;
        int              surfaces = 0;
        world.CreateQuery<const FlowFieldSurface, const WorldTransform>()
            .ForEach([&](Entity, const FlowFieldSurface& found, const WorldTransform& transform) {
                if (surfaces++ == 0) {
                    surface = found;
                    area    = AreaOf(transform.matrix);
                }
            });

        // Any bake, good or bad, makes the old flow meaningless: the next
        // UpdateField rebuilds it straight away (empty m_goalsAtBuild).
        m_goalsAtBuild.clear();

        if (surfaces == 0) {
            CRU_WARN("Navigation: nothing to bake - add a FlowFieldSurface to an entity first");
            m_field.Clear();
            m_grid = NavGrid{};
            return false;
        }
        if (surfaces > 1) {
            CRU_WARN("Navigation: {} FlowFieldSurfaces in the scene - only the first one is used", surfaces);
        }

        // A freshly added surface sits on an entity of scale 1: a 1 x 1 m grid.
        // It still bakes (one cell is a valid grid), but say what to do about it.
        if (area.width < surface.m_cellSize * 2.0f || area.depth < surface.m_cellSize * 2.0f) {
            CRU_WARN("Navigation: the surface only covers {} x {} m - scale its entity so X and Z cover the level (scale = metres)",
                     area.width, area.depth);
        }

        // The flat plane at the surface's own height is the fallback ground -
        // so a designer sits the surface on the floor, the way Unity's
        // NavMeshSurface sits in the level. World y = 0 would be wrong for any
        // floor that isn't exactly there.
        m_flatTerrain = FlatNavTerrain(area.centre.y);
        m_terrain     = &m_flatTerrain;
        m_stepHeight  = surface.m_stepHeight;   // the agents' probes sweep the same height band the bake uses

        // 2. The obstacles: the colliders of entities tagged "Obstacle" - the
        // designer's call, as in Unity's NavGrid, not a guess from the layer
        // and RigidBody. Triggers block nothing in physics, so not here either.
        // Untagged colliders are invisible to navigation (Unity's rule).
        // The same pass finds the terrain: a HeightField collider whose
        // Terrain has been built. It is always ground, never an obstacle, and
        // its lowest and highest corners decide where the ground rays run.
        m_obstacles.clear();
        std::vector<Entity> terrains;
        float sampleTop    = -1e30f;
        float sampleBottom =  1e30f;
        world.CreateQuery<const Collider, const WorldTransform>()
            .ForEach([&](Entity entity, const Collider& collider, const WorldTransform& transform) {
                if (collider.m_isTrigger || !transform.valid) return;
                if (collider.m_type == ShapeType::HeightField) {
                    const Terrain* terrain = world.Read<Terrain>(entity);
                    const auto     grid    = terrain ? terrain->Heights() : nullptr;
                    if (!grid || grid->heights.empty()) return;   // not generated yet: nothing to stand on
                    const auto [low, high] = std::minmax_element(grid->heights.begin(), grid->heights.end());
                    const float y0 = (transform.matrix * Math::vec4(0.0f, *low,  0.0f, 1.0f)).y;
                    const float y1 = (transform.matrix * Math::vec4(0.0f, *high, 0.0f, 1.0f)).y;
                    sampleTop    = std::max(sampleTop,    std::max(y0, y1) + 1.0f);
                    sampleBottom = std::min(sampleBottom, std::min(y0, y1) - 1.0f);
                    terrains.push_back(entity);
                    return;
                }
                if (world.CompareTag(entity, kObstacleTag)) m_obstacles.push_back(WorldBounds(collider, transform.matrix));
            });

        NavGridSettings settings;
        settings.origin            = area.centre;
        settings.width             = area.width;
        settings.depth             = area.depth;
        settings.cellSize          = surface.m_cellSize;
        settings.bodyRadiusPadding = surface.m_bodyRadiusPadding;
        settings.stepHeight        = surface.m_stepHeight;
        settings.shoulderHeight    = surface.m_shoulderHeight;

        // 3. The terrain's heights, sampled from physics - only while Playing,
        // because physics only has bodies then (it is SimulationOnly), and
        // only if there is a terrain. Otherwise the ground stays the flat
        // plane at the surface's height (the Edit preview, hand-built scenes).
        if (!m_physics) m_physics = m_systems.FindSystem<ECSPhysicsSystem>();
        if (!terrains.empty() && m_playMode.State() == PlayState::Playing && m_physics && m_physics->IsInSession()
            && SampleTerrain(settings, surface.m_stepHeight, surface.m_maxWalkableSlope, sampleTop, sampleBottom, terrains)) {
            m_terrain = &m_heightTerrain;
        }
        CRU_INFO("Navigation: {} collider(s) tagged \"{}\" baked as obstacles", m_obstacles.size(), kObstacleTag);

        // 4. The bake itself. NavGrid logs the result, or why it failed. Then
        // the flow field's cost field, which depends on nothing but the grid,
        // so it is built once here rather than on every rebuild.
        if (!m_grid.Bake(settings, m_obstacles, *m_terrain)) {
            m_field.Clear();
            return false;
        }
        m_field.BuildCostField(m_grid, surface.m_wallAdjacencyCost);
        ++m_bakeCount;
        return true;

    }   // ECSNavigationSystem::Bake()

    /*!
        \brief
        Fills m_heightTerrain from physics: one ray straight down through
        every cell's centre, and the point where it meets the terrain is
        that cell's ground. Every other collider the ray passes - tagged or
        untagged - is ignored: tagged obstacles are blocked by the grid bake
        on its own, and untagged ones are invisible to navigation (Unity's
        rule). Until the generator hands its grid over directly (F2), this
        is how the terrain's heights and cliffs reach navigation.

        \details
        A cell is a wall when:
          - nothing is under it (a hole, or past the edge of the floor);
          - its surface is steeper than maxSlopeDegrees (the surface's
            m_maxWalkableSlope, 35 by default; the level generator sets the
            same limit on the player);
          - it is a LEDGE: higher than a neighbour by more than the slope
            between them explains, plus the step height - a sharp terrace
            step the slope test alone misses. A ledge cell's height is then
            set to the ground below it.

        The terrain is a heightfield, so it has no overhangs: one height per
        cell is exact. Overhangs and bridges come with the layered grid.

        \return
        False if nothing was hit at all (no colliders under the surface): the
        caller then keeps the flat ground.

        \param top, bottom
        Where the rays start and end: above the terrain's highest corner and
        below its lowest (Bake works them out).

        \param terrains
        The terrain entities; a hit on anything else is ignored.
    */
    bool ECSNavigationSystem::SampleTerrain(const NavGridSettings& settings, float stepHeight, float maxSlopeDegrees,
                                            float top, float bottom, const std::vector<Entity>& terrains) {

        const float slopeLimit = std::clamp(maxSlopeDegrees, 1.0f, 89.0f);   // a vertical limit would make every wall walkable
        constexpr long long kMaxSamples  = 1000000;   // one ray each; past this, bigger cells are the fix (NavGrid says so too)

        if (!m_physics || !(settings.cellSize > 0.0f)) return false;

        const long long colsLong = static_cast<long long>(std::ceil(settings.width / settings.cellSize));
        const long long rowsLong = static_cast<long long>(std::ceil(settings.depth / settings.cellSize));
        if (colsLong <= 0 || rowsLong <= 0 || colsLong * rowsLong > kMaxSamples) return false;
        const int   cols = static_cast<int>(colsLong);
        const int   rows = static_cast<int>(rowsLong);
        const float cell = settings.cellSize;
        const float minX = settings.origin.x - settings.width * 0.5f;   // the same corner NavGrid uses, so cells match
        const float minZ = settings.origin.z - settings.depth * 0.5f;

        // 1. One ray per cell centre.
        struct Sample { float height = 0.0f; float normalY = 1.0f; bool hit = false; };
        std::vector<Sample> samples(static_cast<std::size_t>(cols) * static_cast<std::size_t>(rows));

        EntityFilter filter;
        filter.layers = LayerMask(PhysicsLayer::NonMoving);   // the arena only: not crates, characters or enemies
        const Math::vec3 down(0.0f, -1.0f, 0.0f);
        const auto isTerrain = [&](const Entity& entity) {
            return std::any_of(terrains.begin(), terrains.end(), [&](const Entity& terrain) {
                return terrain.index == entity.index && terrain.generation == entity.generation;
            });
        };
        std::vector<EntityHit> found;   // every hit down one column, reused for every cell
        int hits = 0;
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                const Math::vec3 origin(minX + (static_cast<float>(col) + 0.5f) * cell, top,
                                        minZ + (static_cast<float>(row) + 0.5f) * cell);
                // Everything down the column, then the nearest hit on the
                // terrain: whatever stands on it (tagged or not) is skipped.
                m_physics->RaycastAll(origin, down, top - bottom, found, filter);
                const EntityHit* ground = nullptr;
                for (const EntityHit& hit : found) {
                    if (isTerrain(hit.entity) && (!ground || hit.distance < ground->distance)) ground = &hit;
                }
                if (!ground) continue;
                Sample& sample = samples[static_cast<std::size_t>(row) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(col)];
                sample = { ground->point.y, ground->normal.y, true };
                ++hits;
            }
        }
        if (hits == 0) return false;

        // 2. Steep cells, then ledges between each pair of side-by-side cells
        // (each pair once: the right and the forward neighbour).
        const float maxSlopeTan = std::tan(glm::radians(slopeLimit));
        const float minNormalY  = std::cos(glm::radians(slopeLimit));

        // How steep a surface is, as rise per metre, from its normal; capped
        // at the walkable limit so a wall's normal can't excuse a huge jump.
        const auto riseRate = [&](const Sample& s) {
            if (s.normalY <= minNormalY) return maxSlopeTan;
            return std::min(std::sqrt(std::max(0.0f, 1.0f - s.normalY * s.normalY)) / s.normalY, maxSlopeTan);
        };

        std::vector<std::uint8_t> wall(samples.size(), 0);
        std::vector<float>        height(samples.size(), settings.origin.y);
        int steep = 0, ledges = 0, empty = 0;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            if (!samples[i].hit)                     { wall[i] = 1; ++empty; continue; }
            height[i] = samples[i].height;
            if (samples[i].normalY < minNormalY)     { wall[i] = 1; ++steep; }
        }

        std::vector<float> lowered = height;   // ledge cells sink to the ground beside them (read the unlowered heights while comparing)
        const auto compare = [&](std::size_t a, std::size_t b) {
            if (!samples[a].hit || !samples[b].hit) return;
            const float allowed = cell * std::max(riseRate(samples[a]), riseRate(samples[b])) + stepHeight;
            const float rise    = height[b] - height[a];
            if (std::abs(rise) <= allowed) return;
            const std::size_t high = rise > 0.0f ? b : a;
            const std::size_t low  = rise > 0.0f ? a : b;
            if (wall[high] == 0) ++ledges;
            wall[high]    = 1;
            lowered[high] = std::min(lowered[high], height[low]);
        };
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                const std::size_t i = static_cast<std::size_t>(row) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(col);
                if (col + 1 < cols) compare(i, i + 1);
                if (row + 1 < rows) compare(i, i + static_cast<std::size_t>(cols));
            }
        }

        // 3. Into the terrain.
        m_heightTerrain.Reset(minX, minZ, cols, rows, cell, settings.origin.y);
        float lowest = lowered[0], highest = lowered[0];
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                const std::size_t i = static_cast<std::size_t>(row) * static_cast<std::size_t>(cols) + static_cast<std::size_t>(col);
                m_heightTerrain.SetCell(col, row, lowered[i], wall[i] != 0);
                lowest  = std::min(lowest, lowered[i]);
                highest = std::max(highest, lowered[i]);
            }
        }

        CRU_INFO("Navigation: sampled {} x {} ground heights from {:.2f} to {:.2f} m - {} too steep, {} ledges, {} with nothing under them",
                 cols, rows, lowest, highest, steep, ledges, empty);
        return true;

    }   // ECSNavigationSystem::SampleTerrain()

    /*!
        \brief
        The baked grid: its cell lines in blue (not grey, which the editor's
        own world grid already uses), and each blocked cell as a red square,
        just above the ground. The outline is drawn separately, live, by
        Update.

        \details
        Drawn on top of everything (depthTest off), like Unity's NavMesh
        overlay: a floor slightly higher than the surface, or the obstacles
        themselves, would otherwise hide the very cells the designer wants to
        check.
    */
    void ECSNavigationSystem::DrawGrid() const {

        // Gizmos > Nav grid toggles all of this (and the outline).
        const DebugStyle cells  { .color = { 0.15f, 0.4f, 0.95f, 1.0f }, .depthTest = false, .category = DebugCategory::NavGrid };
        const DebugStyle blocked{ .color = { 1.0f, 0.25f, 0.2f, 1.0f },  .depthTest = false, .category = DebugCategory::NavGrid };

        const float cellSize = m_grid.CellSize();
        const float half     = cellSize * 0.5f;

        // The grid's corners: from the first cell's outer corner to the last cell's.
        const Math::vec3 first = m_grid.CellToWorldCenter({ 0, 0 });
        const Math::vec3 last  = m_grid.CellToWorldCenter({ m_grid.Cols() - 1, m_grid.Rows() - 1 });
        const float      y     = m_terrain->GroundHeight(first) + kDrawLift;
        const float      minX  = first.x - half;
        const float      maxX  = last.x + half;
        const float      minZ  = first.z - half;
        const float      maxZ  = last.z + half;

        // Cell lines: one long line per column edge and per row edge, not four
        // per cell - 250 x 250 cells is 502 lines this way instead of 250,000.
        for (int col = 1; col < m_grid.Cols(); ++col) {
            const float x = minX + static_cast<float>(col) * cellSize;
            DebugDraw::Line({ x, y, minZ }, { x, y, maxZ }, cells);
        }
        for (int row = 1; row < m_grid.Rows(); ++row) {
            const float z = minZ + static_cast<float>(row) * cellSize;
            DebugDraw::Line({ minX, y, z }, { maxX, y, z }, cells);
        }

        // Blocked cells, drawn a little smaller than the cell (80%) so a row of
        // them reads as separate tiles rather than one solid smear.
        const float inset = cellSize * 0.4f;
        for (int row = 0; row < m_grid.Rows(); ++row) {
            for (int col = 0; col < m_grid.Cols(); ++col) {

                if (!m_grid.IsBlocked({ col, row })) continue;

                Math::vec3 centre = m_grid.CellToWorldCenter({ col, row });
                centre.y = m_terrain->GroundHeight(centre) + kDrawLift;
                const Math::vec3 p0(centre.x - inset, centre.y, centre.z - inset);
                const Math::vec3 p1(centre.x + inset, centre.y, centre.z - inset);
                const Math::vec3 p2(centre.x + inset, centre.y, centre.z + inset);
                const Math::vec3 p3(centre.x - inset, centre.y, centre.z + inset);
                DebugDraw::Line(p0, p1, blocked);
                DebugDraw::Line(p1, p2, blocked);
                DebugDraw::Line(p2, p3, blocked);
                DebugDraw::Line(p3, p0, blocked);

            }
        }

    }   // ECSNavigationSystem::DrawGrid()

    /*!
        \brief
        One yellow arrow per cell that has a direction, centred on the cell
        and pointing the way an enemy standing there would go.

        \details
        Only the flooded cells have one, so the arrows also show the flood
        radius: a circle round the player, with nothing beyond it. Drawn a
        little higher than the grid lines so they sit on top of them.
    */
    void ECSNavigationSystem::DrawArrows() const {

        // Gizmos > Flow field arrows toggles these.
        const DebugStyle arrow{ .color = { 1.0f, 0.85f, 0.2f, 1.0f }, .depthTest = false, .category = DebugCategory::FlowField };

        // Each arrow spans 70% of a cell, so neighbouring arrows never touch.
        const float halfLength = m_grid.CellSize() * 0.35f;

        for (int row = 0; row < m_grid.Rows(); ++row) {
            for (int col = 0; col < m_grid.Cols(); ++col) {

                const Math::vec3 direction = m_field.DirectionAt({ col, row });
                if (direction.x == 0.0f && direction.z == 0.0f) continue;

                Math::vec3 centre = m_grid.CellToWorldCenter({ col, row });
                centre.y = m_terrain->GroundHeight(centre) + kDrawLift * 2.0f;
                DebugDraw::Arrow(centre - direction * halfLength, centre + direction * halfLength, arrow);

            }
        }

    }   // ECSNavigationSystem::DrawArrows()

}   // NAMESPACE CRUCIBLE
