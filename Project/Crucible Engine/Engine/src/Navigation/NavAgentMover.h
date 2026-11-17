/*************************************************************************
* \file:         NavAgentMover.h
* \author:       Jean David Vidal Cruzado (main author; design and Unity original),
*                Hugo Low Ren Hao (initial engine port)
* \par:          Email: jeandavidvidal.c@digipen.edu,
*                Email: low.h@digipen.edu
* \date          17 November 2026
* \version       1.0
* \brief
*                NavAgentMover: moves every FlowFieldAgent one frame, by what
*                it was told to do (FollowField, MoveTo, Arrive, Stop) - the
*                way Unity moves a NavMeshAgent after SetDestination.
*
* \details
*                Split out of ECSNavigationSystem (build step 3). Unlike the
*                rest of the Navigation folder it reads and writes the World
*                directly (FlowFieldAgent, Transform, RigidBody, WorldTransform)
*                and asks physics for its probes: copying every agent into
*                plain data and back would be more code, not moved code.
*
*                Unity's crowd rules (bodies that overlap are moved apart,
*                and the less important agent gives way), alignment from its
*                neighbours (found through an AgentSpatialHash), and
*                collision: a body-sized sphere cast that swerves agents round
*                walls early and slides them along walls they still reach.
*                Kinematic agents stand on the ground; dynamic ones (a
*                dynamic RigidBody) are moved by physics and only steered.
*
*                Not exported (no ENGINE_API), like NavGrid: only the engine
*                uses it.
*
* \history
*    -v1.0       first version (moved out of ECSNavigationSystem's
*                UpdateAgents, Probe, OnGround and StandOnGround, no
*                behaviour change)
*
**************************************************************************/
#pragma once

// STL
#include <vector>

// Crucible engine
#include "ECS/Core/ECSCore.h"   // Entity
#include "Math/Math.h"
#include "Navigation/AgentSpatialHash.h"

namespace Crucible {

    class ECSPhysicsSystem;
    class ECSSystemManager;
    class FlowField;
    class NavGoalTracker;
    class NavGrid;
    class NavTerrain;
    class World;
    struct EntityHit;
    struct FlowFieldAgent;
    struct Transform;

    class NavAgentMover {

    public:

        // systems: where physics is found, for the probes (looked up on first
        // use, because physics is registered after navigation). Core owns it
        // and it outlives this mover.
        explicit NavAgentMover(ECSSystemManager& systems) : m_systems(systems) {}

        // One mover per navigation system: it holds the system manager by reference.
        NavAgentMover(const NavAgentMover&)            = delete;
        NavAgentMover& operator=(const NavAgentMover&) = delete;

        /*!
            \brief
            Moves every FlowFieldAgent one frame (see the .cpp for the steps).

            \param field
            The flow field FollowField reads.

            \param goals
            This frame's players: FollowField's fallback and distance, and
            ArrivePlayer's start.

            \param grid, ground
            For the last resort of StandOnGround: with nothing under an
            agent, the ground's height, if the grid is baked.
        */
        void Move(World& world, const FlowField& field, const NavGoalTracker& goals, const NavGrid& grid, const NavTerrain& ground);

        // The surface's step height, from the last bake: where the probes'
        // sweep starts, so they cover the same height band the bake uses.
        void SetStepHeight(float stepHeight) { m_stepHeight = stepHeight; }

        // Stop: say "agents must be roots" again in the next session.
        void ResetWarnings() { m_warnedParented = false; }

    private:

        void StandOnGround(Entity self, FlowFieldAgent& agent, Math::vec3& position, float dt,
                           const NavGrid& grid, const NavTerrain& ground) const;
        bool OnGround(Entity self, const FlowFieldAgent& agent, const Math::vec3& position) const;
        bool Probe(Entity self, const FlowFieldAgent& agent, const Math::vec3& position, const Math::vec3& direction,
                   float distance, EntityHit& hit) const;

        ECSSystemManager&  m_systems;
        ECSPhysicsSystem*  m_physics        = nullptr;   // found in m_systems on first use; null = no collision
        float              m_stepHeight     = 0.3f;      // see SetStepHeight
        bool               m_warnedParented = false;     // "agents must be roots" said once per session, not every frame

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

        std::vector<AgentRef> m_agents;       // refilled every frame (memory reused)
        AgentSpatialHash      m_hash;         // the agents by 8 m square, refilled every frame
        std::vector<int>      m_neighbours;   // one agent's neighbours, reused for every query

    };  // NAVAGENTMOVER CLASS

}   // NAMESPACE CRUCIBLE
