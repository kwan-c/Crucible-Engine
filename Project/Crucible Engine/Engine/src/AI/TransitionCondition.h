/*************************************************************************
* \file:         TransitionCondition.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                TransitionCondition: one check on a state graph's arrow
*                ("target within 8 m", "10 s in this state"). StateInfo:
*                what a check can know about the state the enemy is in.
*
* \details
*                An arrow switches state when ALL its conditions pass; the
*                first such arrow out of the current state wins (Unity's
*                Animator works the same way).
*
*                Conditions are registered like nodes - a description with
*                a stable id, fields through RTTR - so the editor lists them
*                and files save them the same way, and C# conditions can
*                join later. Unlike nodes they hold no progress (a check
*                only looks), so every enemy shares the graph's ones: no
*                per-enemy copy needed.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <memory>

// Crucible engine
#include "AI/BehaviorNode.h"    // BehaviorContext, BehaviorStatus
#include "AI/NodeRegistry.h"

namespace Crucible {

    // The state the enemy is in, as the arrows out of it see it.
    struct StateInfo {

        float          timeInState  = 0.0f;                    // seconds since it entered
        bool           finished     = false;                   // its tree or node has finished at least once since then
        BehaviorStatus finishedWith = BehaviorStatus::Success; // how it last finished (if it has)

    };  // STATEINFO STRUCT

    class TransitionCondition {

    public:

        virtual ~TransitionCondition() = default;

        // Does this check pass right now? `const`: a check looks, never changes
        // anything - which is what lets every enemy share one.
        virtual bool Check(const BehaviorContext& context, const StateInfo& state) const = 0;

        virtual const NodeDescription& Description() const = 0;

        // A copy with the same settings (the editor's duplicate and paste).
        virtual std::unique_ptr<TransitionCondition> Clone() const = 0;

    protected:

        TransitionCondition() = default;
        TransitionCondition(const TransitionCondition&) = default;
        TransitionCondition& operator=(const TransitionCondition&) = delete;

    };  // TRANSITIONCONDITION CLASS

    using ConditionRegistry  = TypeRegistry<TransitionCondition>;
    using ConditionTypeEntry = TypeEntry<TransitionCondition>;

    template <> ENGINE_API ConditionRegistry& ConditionRegistry::Instance();   // the one copy, in Engine.dll (NodeRegistry.cpp)

}   // NAMESPACE CRUCIBLE

// Gives a condition class its Clone() and Description() - the node macro's twin.
#define CRUCIBLE_TRANSITION_CONDITION(Type)                                                                      \
    public:                                                                                                      \
        std::unique_ptr<TransitionCondition> Clone() const override { return std::make_unique<Type>(*this); }    \
        const NodeDescription& Description() const override { return kDescription; }

// Registers a condition type when the engine loads. In its .cpp, once, inside namespace Crucible.
#define CRUCIBLE_REGISTER_TRANSITION_CONDITION(Type) \
    namespace { [[maybe_unused]] const bool kRegistered_##Type = ::Crucible::ConditionRegistry::Instance().Register<Type>(__FILE__); }
