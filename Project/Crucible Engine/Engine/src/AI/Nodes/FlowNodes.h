/*************************************************************************
* \file:         FlowNodes.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The built-in nodes that decide WHICH children run: On
*                Start (the root), Sequence and Try In Order.
*
* \details
*                Unity Behavior's names, from its Branch Options: Sequence
*                runs children in order until one fails; Try In Order
*                (a "selector" elsewhere) until one succeeds. The rest of
*                that list (Random, Run In Parallel...) can join later.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <cstddef>

// Crucible engine
#include "AI/BehaviorNode.h"
#include "AI/NodeRegistry.h"

namespace Crucible {

    // The root of every behaviour graph: runs its one child, and with Repeat
    // on, starts it again whenever it finishes - how an enemy loops forever.
    class StartNode final : public Modifier {

        CRUCIBLE_BEHAVIOR_NODE(StartNode)

    public:

        static constexpr NodeDescription kDescription{
            "start", "On Start", "Events", "The root of a behaviour graph. Runs its child; with Repeat, again and again." };

        // On by default, unlike Unity's: an enemy's behaviour almost always loops.
        bool m_repeat = true;

    protected:

        BehaviorStatus OnUpdate(float deltaTime) override;

    };  // STARTNODE CLASS

    // Runs its children one after another. Fails as soon as one fails;
    // succeeds when they all have. "Go to A, then wait, then go to B."
    class SequenceNode final : public Composite {

        CRUCIBLE_BEHAVIOR_NODE(SequenceNode)

    public:

        static constexpr NodeDescription kDescription{
            "sequence", "Sequence", "Flow", "Executes branches in order until one fails or all succeed." };

    protected:

        BehaviorStatus OnStart() override;
        BehaviorStatus OnUpdate(float deltaTime) override;

    private:

        std::size_t m_current = 0;   // the child running now

    };  // SEQUENCENODE CLASS

    // Tries its children one after another. Succeeds as soon as one succeeds;
    // fails when they all have. "Attack if in range, else chase, else wander."
    class TryInOrderNode final : public Composite {

        CRUCIBLE_BEHAVIOR_NODE(TryInOrderNode)

    public:

        static constexpr NodeDescription kDescription{
            "try_in_order", "Try In Order", "Flow", "Executes branches in order until one succeeds." };

    protected:

        BehaviorStatus OnStart() override;
        BehaviorStatus OnUpdate(float deltaTime) override;

    private:

        std::size_t m_current = 0;

    };  // TRYINORDERNODE CLASS

}   // NAMESPACE CRUCIBLE
