/*************************************************************************
* \file:         NavigationComponents.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          31 October 2026
* \version       1.0
* \brief
*                FlowFieldSurface: where enemies can walk (Unity's
*                NavMeshSurface). FlowFieldAgent: one enemy's legs (Unity's
*                NavMeshAgent).
*
* \details
*                FlowFieldSurface: one per scene. Its entity's position is
*                the grid's centre and its scale is the grid's size, and its
*                fields are the bake settings. Bake from its Inspector to
*                preview the blocked cells; pressing Play bakes again on its
*                own (ECS/ECSNavigationSystem.h).
*
*                FlowFieldAgent: does nothing until told, then
*                ECSNavigationSystem moves it every frame.
*
* \history
*    -v1.0       first version: FlowFieldSurface, then FlowFieldAgent
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>

// Crucible engine
#include "ECS/Components/ComponentKind.h"
#include "ECS/Core/ECSCore.h"   // Entity, for Arrive's target
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

        // The flow field, rebuilt in Play as the player moves. These two apply
        // live; the wall cost needs a Bake (or Play) to take effect.
        float m_floodRadius       = 100.0f;   // metres round the player that get arrows; 0 = the whole surface. Beyond it enemies head straight in
        float m_rebuildDistance   = 3.0f;     // metres the player must move before the field is rebuilt
        float m_wallAdjacencyCost = 4.0f;     // extra cost of a cell touching a wall, so routes keep clear of walls instead of hugging them

    };  // FLOWFIELDSURFACE STRUCT

    CRUCIBLE_COMPONENT(FlowFieldSurface, FlowFieldSurface)

    // What a FlowFieldAgent does when Play starts, before anything tells it otherwise.
    enum class AgentStart : std::uint8_t {

        Nothing,        // stand still until a behaviour calls FollowField() (Unity's NavMeshAgent default)
        FollowField,    // start following the flow field straight away: a simple horde enemy with no brain
        ArrivePlayer    // start with Arrive() on the nearest player: straight at them, stopping at m_stoppingDistance

    };  // AGENTSTART ENUM

    // What a FlowFieldAgent is doing right now.
    enum class AgentMovement : std::uint8_t {

        None,           // slow to a stop and stand still
        FollowField,    // follow the flow field's arrows; head straight at the player where it has none
        MoveTo,         // straight to m_destination, slowing to stop at m_stoppingDistance (patrol, going home)
        Arrive          // straight at m_target as it moves, slowing to stop at m_stoppingDistance, facing it (his Arrive)

    };  // AGENTMOVEMENT ENUM

    /*
        One enemy's legs: Unity's NavMeshAgent, moving by the flow field
        instead of a navmesh path. The field names are Unity's, so a designer
        coming from Unity knows them; the comments give Jean David's names
        from his porting reference.

        It never decides anything. It stands still until something tells it
        what to do, exactly like a NavMeshAgent before SetDestination:

            agent.FollowField();    // like SetDestination(player), using the shared field
            agent.MoveTo(point);    // like SetDestination(point) - but a straight line: no pathfinding yet
            agent.Arrive(target);   // like SetDestination(target) every frame, facing it
            agent.Stop();           // like ResetPath(): slow down and stand still
            agent.HasArrived();     // like remainingDistance <= stoppingDistance

        and ECSNavigationSystem does the moving every frame while Playing.
        Today "something" is m_startWith; later it is a BehaviorGraphAgent's
        states and tree nodes, and C# scripts - these stay plain functions
        with simple arguments so a script binding can wrap them one to one.

        Moves its own Transform (kinematic), so it must be a root entity, and
        must not have a dynamic RigidBody (physics would fight it). It stands
        on the FlowFieldSurface's ground, m_baseOffset above it.
    */
    struct FlowFieldAgent {

        // SETTINGS ---------------------------------------------------------- //

        float       m_speed        = 6.0f;      // top speed, m/s (his maxSpeed)
        float       m_acceleration = 15.0f;     // how fast it changes velocity, m/s^2 (his maxForce). Low = wide, drifting turns
        float       m_angularSpeed = 240.0f;    // how fast it turns to face where it goes, degrees/s (his turnSpeed)
        float       m_baseOffset   = 1.0f;      // metres from the entity's position down to its feet (his pivotHeight). 1 for a 2 m capsule
        AgentStart  m_startWith    = AgentStart::Nothing;

        // ARRIVING: MoveTo and Arrive (Jean David's Arrive, Unity's stopping distance).
        // Full speed until m_slowingDistance away, then slower in a straight line
        // down to a stop at m_stoppingDistance - so it ends at attack range
        // instead of pushing into its target.
        float       m_stoppingDistance = 2.0f;    // metres from the target where it stops (his attackRange) - Unity's stoppingDistance
        float       m_slowingDistance  = 12.0f;   // metres from the target where it starts slowing down (his arriveRadius)

        // CROWD: how it behaves near other agents. Applies whatever it was told
        // to do, so even standing agents nudge apart. A weight of 0 turns that
        // push off. (Jean David's values; cohesion is left out, as he had it
        // off - it pulls agents together, fighting them spreading round the player.)
        float       m_neighbourRadius  = 10.0f;   // metres: which other agents it reacts to at all
        float       m_separationRadius = 3.5f;    // metres: closer than this and it pushes away - the gap the crowd keeps
        float       m_separationWeight = 1.5f;    // how hard that push is, against following the field (1)
        float       m_alignmentWeight  = 0.4f;    // how much it matches its neighbours' heading, so a group flows instead of jostling

        // COLLISION: walls and crates (static colliders and Moving-layer ones).
        // Sliding along them always happens; avoidance is the early swerve that
        // makes sliding rare. A weight of 0 turns avoidance off.
        float       m_radius            = 0.6f;   // body radius, metres (Unity's NavMeshAgent.radius) - match the surface's padding
        float       m_avoidanceDistance = 3.0f;   // metres it looks ahead for something to swerve round (his probeDistance)
        float       m_avoidanceWeight   = 2.2f;   // how hard it swerves, against following the field (1)

        // TELLING IT WHAT TO DO ---------------------------------------------- //

        void FollowField()                    { m_movement = AgentMovement::FollowField; }
        void MoveTo(const Math::vec3& point)  { m_movement = AgentMovement::MoveTo; m_destination = point; }
        void Arrive(Entity target)            { m_movement = AgentMovement::Arrive; m_target = target; }
        void Stop()                           { m_movement = AgentMovement::None; }

        // ASKING HOW IT'S GOING (written by ECSNavigationSystem every frame) - //

        // Within m_stoppingDistance (plus Jean David's 0.5 m slack, since the
        // crowd can hold it just outside) of its destination, target or nearest
        // player. Always true while stopped: there is nothing left to reach -
        // Unity's remainingDistance reads 0 with no path, too. This is the
        // "in attack range" flag his attack code reads.
        bool  HasArrived() const         { return m_arrived; }

        // Flat metres to the destination, target or nearest player; 0 while
        // stopped (Unity's remainingDistance).
        float RemainingDistance() const  { return m_remainingDistance; }

        // RUNTIME STATE: not saved, not in the Inspector --------------------- //

        AgentMovement m_movement          = AgentMovement::None;
        Math::vec3    m_destination       = Math::vec3(0.0f);   // for MoveTo
        Entity        m_target{};                               // for Arrive; one that dies or loses its Transform makes it stand still
        Math::vec3    m_velocity          = Math::vec3(0.0f);   // flat (y = 0), m/s
        float         m_remainingDistance = 0.0f;
        bool          m_arrived           = true;
        bool          m_started           = false;              // m_startWith applied yet (once per Play, or once when spawned mid-Play)

    };  // FLOWFIELDAGENT STRUCT

    CRUCIBLE_COMPONENT(FlowFieldAgent, FlowFieldAgent)

}   // NAMESPACE CRUCIBLE
