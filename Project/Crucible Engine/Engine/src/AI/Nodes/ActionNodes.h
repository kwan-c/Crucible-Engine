/*************************************************************************
* \file:         ActionNodes.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The built-in leaf nodes: Wait and Log, and the four that
*                drive the enemy's FlowFieldAgent - Move To, Arrive, Follow
*                Field and Stop.
*
* \details
*                The movement nodes only give the agent orders and watch
*                HasArrived(); ECSNavigationSystem does the actual moving.
*                Each stops its agent in OnEnd, so a node that is cut short
*                (its state left, its Sequence failed) never leaves the
*                enemy walking somewhere nobody asked for any more.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <string>

// Crucible engine
#include "AI/BehaviorNode.h"
#include "AI/NodeRegistry.h"
#include "AI/NodeValue.h"
#include "Math/Math.h"

namespace Crucible {

    struct FlowFieldAgent;

    // Waits m_seconds, then succeeds. "Stand at the patrol point for 2 s."
    class WaitNode final : public Action {

        CRUCIBLE_BEHAVIOR_NODE(WaitNode)

    public:

        static constexpr NodeDescription kDescription{
            "wait", "Wait", "Actions", "Waits for a number of seconds, then succeeds." };

        NodeValue<float> m_seconds = 1.0f;

    protected:

        BehaviorStatus OnStart() override;
        BehaviorStatus OnUpdate(float deltaTime) override;

    private:

        float m_elapsed = 0.0f;

    };  // WAITNODE CLASS

    // Writes m_message to the log, then succeeds. For checking a graph runs the way you think.
    class LogNode final : public Action {

        CRUCIBLE_BEHAVIOR_NODE(LogNode)

    public:

        static constexpr NodeDescription kDescription{
            "log", "Log", "Debug", "Writes a message to the log, then succeeds." };

        std::string m_message = "Hello from a behaviour graph";

    protected:

        BehaviorStatus OnStart() override;
        BehaviorStatus OnUpdate(float) override { return BehaviorStatus::Success; }

    };  // LOGNODE CLASS

    // The base of the nodes that drive the enemy's FlowFieldAgent: finds it,
    // and says once (per node) if the enemy hasn't got one.
    class AgentAction : public Action {

    protected:

        AgentAction() = default;
        AgentAction(const AgentAction&) = default;   // copies are made from the asset, which never runs, so m_warned starts false

        // The enemy's agent this frame, or null (logged once). Looked up every
        // time rather than kept: a component's address can change when other
        // entities are added or removed.
        FlowFieldAgent* Agent();

        // OnEnd for all of them: stop the agent, however the run ended.
        void OnEnd() override;

    private:

        bool m_warned = false;

    };  // AGENTACTION CLASS

    // Walks straight to m_destination; succeeds on arrival (within the agent's
    // stopping distance). The destination may be a blackboard variable that
    // moves - it follows it. No pathfinding: keep a clear line, or patrol
    // points close together.
    class MoveToNode final : public AgentAction {

        CRUCIBLE_BEHAVIOR_NODE(MoveToNode)

    public:

        static constexpr NodeDescription kDescription{
            "move_to", "Move To", "Navigation", "Walks straight to a point; succeeds on arrival." };

        NodeValue<Math::vec3> m_destination;

    protected:

        BehaviorStatus OnStart() override;
        BehaviorStatus OnUpdate(float deltaTime) override;

    private:

        Math::vec3 m_issued{ 0.0f };   // the point last given to the agent

    };  // MOVETONODE CLASS

    // Goes straight at m_target as it moves, slowing to stop in range and
    // facing it; succeeds once in range. Fails if the target is missing or dies.
    class ArriveNode final : public AgentAction {

        CRUCIBLE_BEHAVIOR_NODE(ArriveNode)

    public:

        static constexpr NodeDescription kDescription{
            "arrive", "Arrive", "Navigation", "Goes straight at a target, stopping in range and facing it; succeeds in range." };

        NodeValue<Entity> m_target = NodeValue<Entity>::Variable("Target");   // starts linked to "Target"

    protected:

        BehaviorStatus OnStart() override;
        BehaviorStatus OnUpdate(float deltaTime) override;

    private:

        Entity m_issued{};   // the target last given to the agent

    };  // ARRIVENODE CLASS

    // Follows the flow field towards the players; succeeds once close to one
    // (the agent's stopping distance). Where the field has no arrow, the agent
    // heads straight at the nearest player.
    class FollowFieldNode final : public AgentAction {

        CRUCIBLE_BEHAVIOR_NODE(FollowFieldNode)

    public:

        static constexpr NodeDescription kDescription{
            "follow_field", "Follow Field", "Navigation", "Follows the flow field towards the players; succeeds when close to one." };

    protected:

        BehaviorStatus OnStart() override;
        BehaviorStatus OnUpdate(float deltaTime) override;

    };  // FOLLOWFIELDNODE CLASS

    // Tells the agent to slow down and stand still, then succeeds at once.
    class StopNode final : public AgentAction {

        CRUCIBLE_BEHAVIOR_NODE(StopNode)

    public:

        static constexpr NodeDescription kDescription{
            "stop", "Stop", "Navigation", "Tells the enemy to stand still; succeeds at once." };

    protected:

        BehaviorStatus OnStart() override;
        BehaviorStatus OnUpdate(float) override { return BehaviorStatus::Success; }

    };  // STOPNODE CLASS

}   // NAMESPACE CRUCIBLE
