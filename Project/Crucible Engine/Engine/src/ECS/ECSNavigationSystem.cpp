/*************************************************************************
* \file:         ECSNavigationSystem.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          31 October 2026
* \version       1.0
* \brief
*                Bakes the NavGrid from the scene, and draws it.
*
* \details
*                The ECS-to-grid translation lives here, so NavGrid itself
*                never sees a component: this file turns colliders into
*                NavObstacle boxes and the FlowFieldSurface into
*                NavGridSettings.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "ECSNavigationSystem.h"

// STL
#include <algorithm>    // std::clamp, std::max
#include <cmath>        // std::atan2, std::sqrt

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
        // maxForce. Every behaviour (follow the field, separation, alignment)
        // speaks in these, so they can simply be weighted and added up.
        Math::vec3 Steer(const Math::vec3& desired, const Math::vec3& velocity, float maxForce) {

            return ClampMagnitude(desired - velocity, maxForce);

        }   // Steer()

        // Slack on HasArrived beyond the stopping distance (Jean David's
        // "attackRange + 0.5"): the crowd can hold an agent just outside it.
        constexpr float kArriveSlack = 0.5f;

        /*!
            \brief
            Jean David's Arrive: the velocity that heads straight for `target`
            at full speed, then slows in a straight line from the agent's
            m_slowingDistance to a stop at its m_stoppingDistance. Shared by
            MoveTo (a point) and Arrive (an entity's position).

            \param outDirection
            The flat unit direction to the target (zero if standing on it).

            \param outDistance
            The flat distance to it, for RemainingDistance.
        */
        Math::vec3 ArriveVelocity(const Math::vec3& from, const Math::vec3& target, const FlowFieldAgent& agent,
                                  Math::vec3& outDirection, float& outDistance) {

            const Math::vec3 to(target.x - from.x, 0.0f, target.z - from.z);
            outDistance = glm::length(to);
            if (outDistance < 1e-4f) { outDirection = Math::vec3(0.0f); return Math::vec3(0.0f); }
            outDirection = to / outDistance;

            // 1 outside the slowing distance, 0 at the stopping distance, a
            // straight line in between. max(0.01) so slowing <= stopping can't
            // divide by zero (it then just stops dead at the stopping distance).
            const float t = std::clamp((outDistance - agent.m_stoppingDistance)
                                       / std::max(0.01f, agent.m_slowingDistance - agent.m_stoppingDistance), 0.0f, 1.0f);
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
        : m_playMode(playMode), m_systems(systems) {}

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
        obstacle, and other enemies are separation's job. Filtering them out
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
        if (m_lastState == PlayState::Edit && state == PlayState::Playing) {

            Bake(world);

            // Say why there will be no arrows, once, rather than leaving the
            // designer to guess.
            bool hasPlayer = false;
            world.CreateQuery<const PlayerController>().ForEach([&](Entity, const PlayerController&) { hasPlayer = true; });
            if (!hasPlayer) CRU_WARN("Navigation: no PlayerController in the scene - the flow field has no goal");

        }

        // Stop: the arrows pointed at a player in a session that is over, and
        // Stop puts the scene back as it was - so forget the flow (not the grid).
        if (m_lastState != PlayState::Edit && state == PlayState::Edit) {
            m_field.ClearFlow();
            m_goalsAtBuild.clear();
            m_warnedParented = false;
            m_warnedDynamic  = false;
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
        world.CreateQuery<const PlayerController, const WorldTransform>()
            .ForEach([&](Entity player, const PlayerController&, const WorldTransform& transform) {
                if (!transform.valid) return;
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
          2. the crowd: separation and alignment forces from its neighbours
          3. avoidance: a swerve round whatever is ahead
          4. add them up, capped by m_acceleration, and steer
          5. move, sliding along anything still in the way
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

                // A dynamic RigidBody means physics moves this entity too: its
                // gravity drops it, this sets it back on the ground, and it bobs.
                // Kinematic is the right setup (physics follows the Transform,
                // bullets still hit it). Still moved, so the problem is visible
                // AND explained, once per session.
                if (const RigidBody* body = world.Read<RigidBody>(entity); body && !body->isKinematic && !m_warnedDynamic) {
                    CRU_WARN("Navigation: the FlowFieldAgent on entity {} has a non-kinematic RigidBody - tick isKinematic, "
                             "or physics and the agent will fight over its position", entity.index);
                    m_warnedDynamic = true;
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
                m_agents.push_back({ entity, &agent, &transform, transform.positions, agent.m_velocity });
                m_hash.Insert(index, transform.positions);

            });

        // The crowd forces, from this frame's neighbours (m_neighbours) and the
        // snapshots - never from an agent pass 2 has already moved.
        //
        // Separation: away from every neighbour closer than m_separationRadius,
        // each weighted 1/distance, so the nearest one counts most. Then aimed
        // at full speed: the direction says which way, the weight how hard.
        const auto separation = [&](std::size_t i, const FlowFieldAgent& agent) {

            const Math::vec3& from     = m_agents[i].start;
            const float       radiusSq = agent.m_separationRadius * agent.m_separationRadius;

            Math::vec3 sum(0.0f);
            for (const int j : m_neighbours) {

                Math::vec3  away(from.x - m_agents[static_cast<std::size_t>(j)].start.x, 0.0f,
                                 from.z - m_agents[static_cast<std::size_t>(j)].start.z);
                const float distanceSq = glm::dot(away, away);
                if (distanceSq >= radiusSq) continue;

                float distance = std::sqrt(distanceSq);
                if (distance < 0.001f) {
                    // Exactly on top of each other (spawned at one spot): there
                    // is no "away". Jean David picks a random direction; this
                    // picks one from the agent's number instead - different for
                    // each agent, the same every frame. The golden angle (2.4
                    // rad) spreads consecutive numbers evenly round the circle.
                    const float angle = static_cast<float>(i) * 2.39996f;
                    away     = Math::vec3(std::cos(angle), 0.0f, std::sin(angle));
                    distance = 0.001f;
                } else {
                    away /= distance;
                }
                sum += away / distance;

            }
            if (glm::dot(sum, sum) < 1e-8f) return Math::vec3(0.0f);   // nobody close, or pushes that cancel out
            return Steer(glm::normalize(sum) * agent.m_speed, agent.m_velocity, agent.m_acceleration);

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
            // Arrive also faces its target, not where it walks (his FaceTarget),
            // so an enemy backing off or sidestepping still looks at you.
            //
            // Each also fills in how it's going: RemainingDistance and
            // HasArrived, which the brain and the attack code read.
            Math::vec3 direction(0.0f);     // the way it is trying to go: avoidance picks its side from this
            Math::vec3 desired(0.0f);
            Math::vec3 lookAt(0.0f);        // a point to face instead of its velocity (Arrive only)
            bool       facesTarget = false;
            bool       hasGoal     = false; // something to measure the distance to
            float      remaining   = 0.0f;

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
                    desired = ArriveVelocity(position, agent.m_destination, agent, direction, remaining);
                    hasGoal = true;
                    DebugDraw::Line(position, agent.m_destination, pathStyle);
                    break;

                case AgentMovement::Arrive: {
                    // A target that has died or lost its WorldTransform: stand
                    // still, and don't claim to have arrived at it.
                    const WorldTransform* target = world.IsAlive(agent.m_target) ? world.Read<WorldTransform>(agent.m_target) : nullptr;
                    if (!target || !target->valid) break;

                    lookAt      = Math::vec3(target->matrix[3]);
                    facesTarget = true;
                    desired     = ArriveVelocity(position, lookAt, agent, direction, remaining);
                    hasGoal     = true;
                    DebugDraw::Line(position, lookAt, pathStyle);
                    break;
                }

            }

            // Stopped: nothing left to reach, so "arrived" (Unity reads a path-less
            // agent's remainingDistance as 0 too). Otherwise within the stopping
            // distance plus Jean David's 0.5 m of slack - the crowd can hold an
            // agent just outside it, and it must still count as in range.
            agent.m_remainingDistance = remaining;
            agent.m_arrived = agent.m_movement == AgentMovement::None
                           || (hasGoal && remaining <= agent.m_stoppingDistance + kArriveSlack);

            Math::vec3 force = Steer(desired, agent.m_velocity, agent.m_acceleration);

            // 2. The crowd, on top - whatever it was told, so even standing
            // agents nudge apart. Skipped entirely when both weights are 0.
            if (agent.m_separationWeight != 0.0f || agent.m_alignmentWeight != 0.0f) {
                m_hash.Query(m_agents[i].start, agent.m_neighbourRadius, static_cast<int>(i), m_neighbours);
                if (agent.m_separationWeight != 0.0f) force += separation(i, agent) * agent.m_separationWeight;
                if (agent.m_alignmentWeight  != 0.0f) force += alignment(agent)     * agent.m_alignmentWeight;
            }

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
            agent.m_velocity += ClampMagnitude(force, agent.m_acceleration) * dt;
            agent.m_velocity.y = 0.0f;
            agent.m_velocity  = ClampMagnitude(agent.m_velocity, agent.m_speed);

            // 5. Move - the hard limit no force can overpower. Probe along the
            // move first. Something in the way: keep only the part of the move
            // (and of the velocity, so it stops pushing) that runs ALONG the
            // surface, and slide. If even the slide is blocked - a corner -
            // stay put this frame.
            Math::vec3  motion   = agent.m_velocity * dt;
            const float distance = glm::length(motion);
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

            // 6. Face where it is going - or, under Arrive, its target (the
            // model is assumed to face +Z, as in his prototype). Below 0.1 m/s,
            // or within 0.1 m of the target, the heading is noise, so it keeps
            // its last facing. Yaw only: it always stays upright.
            const Math::vec3 look = facesTarget ? Math::vec3(lookAt.x - position.x, 0.0f, lookAt.z - position.z)
                                                : agent.m_velocity;
            if (glm::dot(look, look) >= 0.01f) {

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

            // 7. Stand on the ground: its feet m_baseOffset below its
            // position. Set exactly for now; Jean David's prototype eases
            // towards the ground at 12 m/s instead, which can come back if
            // spawning off the ground or real terrain needs it.
            // With no baked surface there is no known ground, so its height
            // is left alone rather than dropped to some made-up y = 0.
            if (m_grid.IsBaked()) position.y = m_terrain.GroundHeight(position) + agent.m_baseOffset;

            // Its velocity, half a second's worth, under Gizmos > Velocities:
            // the push-apart and the curving are visible at a glance.
            DebugDraw::Arrow(position, position + agent.m_velocity * 0.5f, velocityStyle);

        }

    }   // ECSNavigationSystem::UpdateAgents()

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

        // Until a terrain system exists the ground is flat, at the surface's
        // own height - so a designer sits the surface on the floor, the way
        // Unity's NavMeshSurface sits in the level. World y = 0 would be wrong
        // for any floor that isn't exactly there.
        m_terrain    = FlatNavTerrain(area.centre.y);
        m_stepHeight = surface.m_stepHeight;   // the agents' probes sweep the same height band the bake uses

        // 2. The obstacles: static, solid colliders. "Static" is Unity's rule:
        // a collider with no RigidBody. Anything with one moves (crates, even a
        // kinematic door), so it is steered around later, not baked in - and
        // checking for the RigidBody too catches a moving body someone left on
        // the NonMoving layer. Triggers block nothing in physics either.
        m_obstacles.clear();
        world.CreateQuery<const Collider, const WorldTransform>()
            .ForEach([&](Entity entity, const Collider& collider, const WorldTransform& transform) {
                if (collider.m_isTrigger || collider.m_layer != PhysicsLayer::NonMoving || !transform.valid) return;
                if (world.HasComponent<RigidBody>(entity)) return;
                m_obstacles.push_back(WorldBounds(collider, transform.matrix));
            });

        // 3. The bake itself. NavGrid logs the result, or why it failed. Then
        // the flow field's cost field, which depends on nothing but the grid,
        // so it is built once here rather than on every rebuild.
        NavGridSettings settings;
        settings.origin            = area.centre;
        settings.width             = area.width;
        settings.depth             = area.depth;
        settings.cellSize          = surface.m_cellSize;
        settings.bodyRadiusPadding = surface.m_bodyRadiusPadding;
        settings.stepHeight        = surface.m_stepHeight;
        settings.shoulderHeight    = surface.m_shoulderHeight;
        if (!m_grid.Bake(settings, m_obstacles, m_terrain)) {
            m_field.Clear();
            return false;
        }
        m_field.BuildCostField(m_grid, surface.m_wallAdjacencyCost);
        return true;

    }   // ECSNavigationSystem::Bake()

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
        const float      y     = m_terrain.GroundHeight(first) + kDrawLift;
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
                centre.y = m_terrain.GroundHeight(centre) + kDrawLift;
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
                centre.y = m_terrain.GroundHeight(centre) + kDrawLift * 2.0f;
                DebugDraw::Arrow(centre - direction * halfLength, centre + direction * halfLength, arrow);

            }
        }

    }   // ECSNavigationSystem::DrawArrows()

}   // NAMESPACE CRUCIBLE
