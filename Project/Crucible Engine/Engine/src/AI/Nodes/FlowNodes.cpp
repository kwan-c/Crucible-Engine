/*************************************************************************
* \file:         FlowNodes.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                On Start, Sequence and Try In Order, their registration,
*                and RTTR registration of their fields.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "FlowNodes.h"

// Crucible engine
#include "Reflection/ReflectionRegistration.h"

namespace Crucible {

    // --- On Start ---------------------------------------------------------- //

    BehaviorStatus StartNode::OnUpdate(float deltaTime) {

        if (!Child()) return BehaviorStatus::Success;     // an empty graph: nothing to do, done

        const BehaviorStatus status = Child()->Tick(deltaTime);
        if (status == BehaviorStatus::Running) return BehaviorStatus::Running;

        // The child finished. Without Repeat, so has the graph. With it, report
        // Running: the child is idle now, and its next Tick - NEXT frame -
        // starts it again. Restarting it here instead would let a child that
        // finishes instantly (a lone Log) loop forever inside one frame.
        return m_repeat ? BehaviorStatus::Running : status;

    }   // StartNode::OnUpdate()

    // --- Sequence ---------------------------------------------------------- //

    BehaviorStatus SequenceNode::OnStart() {

        m_current = 0;
        return ChildCount() == 0 ? BehaviorStatus::Success : BehaviorStatus::Running;   // nothing to fail: done

    }   // SequenceNode::OnStart()

    BehaviorStatus SequenceNode::OnUpdate(float deltaTime) {

        // As many children as finish this frame run this frame: a child that
        // succeeds instantly hands straight on to the next. Only the first
        // gets the frame's time - the ones after it start at this moment, so
        // a Wait after an instant Log doesn't count the frame twice.
        float step = deltaTime;
        while (m_current < ChildCount()) {

            const BehaviorStatus status = ChildAt(m_current)->Tick(step);
            step = 0.0f;

            if (status == BehaviorStatus::Running) return BehaviorStatus::Running;
            if (status == BehaviorStatus::Failure) return BehaviorStatus::Failure;
            ++m_current;   // that one succeeded: on to the next

        }
        return BehaviorStatus::Success;

    }   // SequenceNode::OnUpdate()

    // --- Try In Order ------------------------------------------------------ //

    BehaviorStatus TryInOrderNode::OnStart() {

        m_current = 0;
        return ChildCount() == 0 ? BehaviorStatus::Failure : BehaviorStatus::Running;   // nothing to succeed: fail

    }   // TryInOrderNode::OnStart()

    BehaviorStatus TryInOrderNode::OnUpdate(float deltaTime) {

        // Sequence's mirror image: Success stops it, Failure moves on.
        float step = deltaTime;
        while (m_current < ChildCount()) {

            const BehaviorStatus status = ChildAt(m_current)->Tick(step);
            step = 0.0f;

            if (status == BehaviorStatus::Running) return BehaviorStatus::Running;
            if (status == BehaviorStatus::Success) return BehaviorStatus::Success;
            ++m_current;   // that one failed: try the next

        }
        return BehaviorStatus::Failure;

    }   // TryInOrderNode::OnUpdate()

    CRUCIBLE_REGISTER_BEHAVIOR_NODE(StartNode)
    CRUCIBLE_REGISTER_BEHAVIOR_NODE(SequenceNode)
    CRUCIBLE_REGISTER_BEHAVIOR_NODE(TryInOrderNode)

}   // NAMESPACE CRUCIBLE

// The fields saving and the editor will see. Sequence and Try In Order have
// none (their children are the graph's structure, saved separately).
RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::class_<StartNode>("StartNode")
        .property("repeat", &StartNode::m_repeat);

    rttr::registration::class_<SequenceNode>("SequenceNode");
    rttr::registration::class_<TryInOrderNode>("TryInOrderNode");
}
