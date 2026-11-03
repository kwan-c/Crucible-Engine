/*************************************************************************
* \file:         ActionNodes.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                Wait, Log, Move To, Arrive, Follow Field and Stop, their
*                registration, and RTTR registration of their fields.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "ActionNodes.h"

// Crucible engine
#include "Diagnostics/Logger.h"
#include "ECS/Components/Components.h"           // the components World's templates need
#include "ECS/World.h"
#include "Navigation/NavigationComponents.h"     // FlowFieldAgent
#include "Reflection/ReflectionRegistration.h"

namespace Crucible {

    // --- Wait -------------------------------------------------------------- //

    BehaviorStatus WaitNode::OnStart() {

        m_elapsed = 0.0f;
        return BehaviorStatus::Running;

    }   // WaitNode::OnStart()

    BehaviorStatus WaitNode::OnUpdate(float deltaTime) {

        // The length is read every frame, so a blackboard variable that changes
        // mid-wait (a timer the brain shortens) takes effect at once.
        m_elapsed += deltaTime;
        return m_elapsed >= m_seconds.Get(Context()) ? BehaviorStatus::Success : BehaviorStatus::Running;

    }   // WaitNode::OnUpdate()

    // --- Log --------------------------------------------------------------- //

    BehaviorStatus LogNode::OnStart() {

        CRU_INFO("Behavior (entity {}): {}", Context().self.index, m_message);
        return BehaviorStatus::Success;     // nothing left to do: finish without an update

    }   // LogNode::OnStart()

    // --- Agent actions (shared) ------------------------------------------- //

    FlowFieldAgent* AgentAction::Agent() {

        FlowFieldAgent* agent = Context().world->GetComponent<FlowFieldAgent>(Context().self);
        if (!agent && !m_warned) {
            CRU_WARN("Behavior: '{}' on entity {} needs a FlowFieldAgent to move it - add one", Description().name, Context().self.index);
            m_warned = true;
        }
        return agent;

    }   // AgentAction::Agent()

    void AgentAction::OnEnd() {

        // Finished or cut short, the order is over: don't leave the enemy
        // walking somewhere nobody wants it to go any more. (Looked up quietly:
        // a missing agent was already reported by Agent().)
        if (FlowFieldAgent* agent = Context().world->GetComponent<FlowFieldAgent>(Context().self)) agent->Stop();

    }   // AgentAction::OnEnd()

    // --- Move To ----------------------------------------------------------- //

    BehaviorStatus MoveToNode::OnStart() {

        FlowFieldAgent* agent = Agent();
        if (!agent) return BehaviorStatus::Failure;

        m_issued = m_destination.Get(Context());
        agent->MoveTo(m_issued);
        return BehaviorStatus::Running;

    }   // MoveToNode::OnStart()

    BehaviorStatus MoveToNode::OnUpdate(float) {

        FlowFieldAgent* agent = Agent();
        if (!agent) return BehaviorStatus::Failure;

        // Re-issue only when the destination really changed (a moving
        // variable). Re-issuing every frame would reset "arrived" every frame,
        // and the node would never finish.
        const Math::vec3 destination = m_destination.Get(Context());
        if (destination != m_issued) {
            m_issued = destination;
            agent->MoveTo(destination);
        }
        return agent->HasArrived() ? BehaviorStatus::Success : BehaviorStatus::Running;

    }   // MoveToNode::OnUpdate()

    // --- Arrive ------------------------------------------------------------ //

    BehaviorStatus ArriveNode::OnStart() {

        FlowFieldAgent* agent = Agent();
        if (!agent) return BehaviorStatus::Failure;

        m_issued = m_target.Get(Context());
        if (m_issued == kNullEntity || !Context().world->IsAlive(m_issued)) return BehaviorStatus::Failure;   // nobody to go at

        agent->Arrive(m_issued);
        return BehaviorStatus::Running;

    }   // ArriveNode::OnStart()

    BehaviorStatus ArriveNode::OnUpdate(float) {

        FlowFieldAgent* agent = Agent();
        if (!agent) return BehaviorStatus::Failure;

        const Entity target = m_target.Get(Context());
        if (target == kNullEntity || !Context().world->IsAlive(target)) return BehaviorStatus::Failure;   // gone mid-chase

        // Same rule as Move To: only a NEW target is re-issued.
        if (!(target == m_issued)) {
            m_issued = target;
            agent->Arrive(target);
        }
        return agent->HasArrived() ? BehaviorStatus::Success : BehaviorStatus::Running;

    }   // ArriveNode::OnUpdate()

    // --- Follow Field ------------------------------------------------------ //

    BehaviorStatus FollowFieldNode::OnStart() {

        FlowFieldAgent* agent = Agent();
        if (!agent) return BehaviorStatus::Failure;

        agent->FollowField();
        return BehaviorStatus::Running;

    }   // FollowFieldNode::OnStart()

    BehaviorStatus FollowFieldNode::OnUpdate(float) {

        FlowFieldAgent* agent = Agent();
        if (!agent) return BehaviorStatus::Failure;
        return agent->HasArrived() ? BehaviorStatus::Success : BehaviorStatus::Running;

    }   // FollowFieldNode::OnUpdate()

    // --- Stop -------------------------------------------------------------- //

    BehaviorStatus StopNode::OnStart() {

        FlowFieldAgent* agent = Agent();
        if (!agent) return BehaviorStatus::Failure;

        agent->Stop();
        return BehaviorStatus::Success;

    }   // StopNode::OnStart()

    CRUCIBLE_REGISTER_BEHAVIOR_NODE(WaitNode)
    CRUCIBLE_REGISTER_BEHAVIOR_NODE(LogNode)
    CRUCIBLE_REGISTER_BEHAVIOR_NODE(MoveToNode)
    CRUCIBLE_REGISTER_BEHAVIOR_NODE(ArriveNode)
    CRUCIBLE_REGISTER_BEHAVIOR_NODE(FollowFieldNode)
    CRUCIBLE_REGISTER_BEHAVIOR_NODE(StopNode)

}   // NAMESPACE CRUCIBLE

// The fields saving and the editor will see - the "Self navigates to [X][Y][Z]"
// parts of each node. Property names are a file format: never rename one.
RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::class_<WaitNode>("WaitNode")
        .property("seconds", &WaitNode::m_seconds);

    rttr::registration::class_<LogNode>("LogNode")
        .property("message", &LogNode::m_message);

    rttr::registration::class_<MoveToNode>("MoveToNode")
        .property("destination", &MoveToNode::m_destination);

    rttr::registration::class_<ArriveNode>("ArriveNode")
        .property("target", &ArriveNode::m_target);

    rttr::registration::class_<FollowFieldNode>("FollowFieldNode");
    rttr::registration::class_<StopNode>("StopNode");
}
