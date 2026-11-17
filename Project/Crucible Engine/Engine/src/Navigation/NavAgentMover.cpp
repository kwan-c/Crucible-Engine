/*************************************************************************
* \file:         NavAgentMover.cpp
* \author:       Jean David Vidal Cruzado (main author; design and Unity original),
*                Hugo Low Ren Hao (initial engine port)
* \par:          Email: jeandavidvidal.c@digipen.edu,
*                Email: low.h@digipen.edu
* \date          17 November 2026
* \version       1.0
* \brief
*                Moves the FlowFieldAgents: steering, the crowd, collision,
*                facing and standing on the ground.
*
* \details
*                The agent half of ECSNavigationSystem until build step 3
*                (its history before the split is in that file's v1.1).
*
* \history
*    -v1.0       first version (moved out of ECSNavigationSystem, no
*                behaviour change)
*
**************************************************************************/
#include "NavAgentMover.h"

// STL
#include <algorithm>    // std::clamp, std::max, std::min
#include <cmath>        // std::atan2, std::sqrt, std::cos, std::sin, std::abs

// Crucible engine
#include "ECS/Components/Components.h"        // Transform, WorldTransform
#include "Diagnostics/Logger.h"
#include "ECS/ECSPhysicsSystem.h"             // SphereCast, Raycast, for the probes
#include "Graphics/DebugDraw.h"               // the velocity and path lines
#include "Navigation/FlowField.h"
#include "Navigation/NavGoalTracker.h"
#include "Navigation/NavGrid.h"
#include "Navigation/NavTerrain.h"
#include "Navigation/NavigationComponents.h"  // FlowFieldAgent
#include "Physics/EntityQueries.h"            // EntityHit, EntityFilter
#include "Physics/PhysicsComponents.h"        // RigidBody
#include "ECS/Systems/ECSSystemManager.h"     // FindSystem
#include "ECS/Systems/Query.h"
#include "ECS/World.h"

namespace Crucible {

    namespace {

        // Extra length on a sliding-collision probe (Jean David's skin), so an
        // agent stops just short of a wall instead of touching it - touching
        // would make the next frame's probe start inside the wall.
        constexpr float kCollisionSkin = 0.05f;

        // A hit whose surface faces this much upward (about 45 degrees or
        // flatter) is ground to walk on - a ramp, a step - not a wall.
        constexpr float kWalkableNormalY = 0.7f;

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
    bool NavAgentMover::Probe(Entity self, const FlowFieldAgent& agent, const Math::vec3& position, const Math::vec3& direction,
                              float distance, EntityHit& hit) const {

        if (!m_physics) return false;     // no physics registered: no collision, rather than no movement

        const float feet = position.y - agent.m_baseOffset;
        const Math::vec3 origin(position.x, feet + m_stepHeight + agent.m_radius, position.z);

        EntityFilter filter;
        filter.layers = LayerMask(PhysicsLayer::NonMoving, PhysicsLayer::Moving);
        filter.ignore = self;

        if (!m_physics->SphereCast(origin, agent.m_radius, direction, hit, distance, filter)) return false;
        return hit.normal.y <= kWalkableNormalY;

    }   // NavAgentMover::Probe()

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
    void NavAgentMover::Move(World& world, const FlowField& field, const NavGoalTracker& goals, const NavGrid& grid, const NavTerrain& ground) {

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

                // Two ways to be moved (see DYNAMIC AGENTS below):
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
                            const Entity player = goals.NearestEntity(transform.positions);
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
                    direction = field.SampleDirection(position);
                    if (direction == Math::vec3(0.0f)) direction = TowardsNearest(position, goals.Goals());
                    desired = direction * agent.m_speed;

                    // Its distance to the nearest player, so HasArrived means
                    // "close enough to switch to Arrive or attack" here too.
                    for (const Math::vec3& player : goals.Goals()) {
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
            if (!dynamic) StandOnGround(m_agents[i].entity, agent, position, dt, grid, ground);

            // Its velocity, half a second's worth, under Gizmos > Velocities:
            // the push-apart and the curving are visible at a glance.
            DebugDraw::Arrow(position, position + agent.m_velocity * 0.5f, velocityStyle);

        }

    }   // NavAgentMover::Move()

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
    bool NavAgentMover::OnGround(Entity self, const FlowFieldAgent& agent, const Math::vec3& position) const {

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

    }   // NavAgentMover::OnGround()

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
    void NavAgentMover::StandOnGround(Entity self, FlowFieldAgent& agent, Math::vec3& position, float dt,
                                      const NavGrid& grid, const NavTerrain& ground) const {

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
        if (grid.IsBaked()) position.y = ground.GroundHeight(position) + agent.m_baseOffset;

    }   // NavAgentMover::StandOnGround()

}   // NAMESPACE CRUCIBLE
