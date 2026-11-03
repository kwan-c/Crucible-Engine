/*************************************************************************
* \file:         BuiltinConditions.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The built-in arrow checks: Target Within / Target Beyond a
*                distance, Time In State, State Finished, Blackboard Bool.
*
* \details
*                Enough for the Idle -> Patrol -> Chase -> Search loop with
*                Jean David's distances (detect within 40 m, lose beyond
*                60 m). "Can see target" joins when perception exists.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>
#include <string>

// Crucible engine
#include "AI/NodeValue.h"
#include "AI/TransitionCondition.h"

namespace Crucible {

    // Passes when the target is this close or closer (flat distance, height ignored).
    // Jean David's "detect": Patrol -> Chase within 40 m.
    class TargetWithinCondition final : public TransitionCondition {

        CRUCIBLE_TRANSITION_CONDITION(TargetWithinCondition)

    public:

        static constexpr NodeDescription kDescription{
            "target_within", "Target Within", "Distance", "Passes when the target is this close or closer." };

        NodeValue<Entity> m_target   = NodeValue<Entity>::Variable("Target");
        NodeValue<float>  m_distance = 8.0f;

        bool Check(const BehaviorContext& context, const StateInfo& state) const override;

    };  // TARGETWITHINCONDITION CLASS

    // Passes when the target is further than this - or missing, or dead (it's
    // certainly not in range). Jean David's "lose": Chase -> Search beyond 60 m.
    class TargetBeyondCondition final : public TransitionCondition {

        CRUCIBLE_TRANSITION_CONDITION(TargetBeyondCondition)

    public:

        static constexpr NodeDescription kDescription{
            "target_beyond", "Target Beyond", "Distance", "Passes when the target is further than this, or gone." };

        NodeValue<Entity> m_target   = NodeValue<Entity>::Variable("Target");
        NodeValue<float>  m_distance = 12.0f;

        bool Check(const BehaviorContext& context, const StateInfo& state) const override;

    };  // TARGETBEYONDCONDITION CLASS

    // Passes once the enemy has been in this state this long. "Search for 10 s."
    class TimeInStateCondition final : public TransitionCondition {

        CRUCIBLE_TRANSITION_CONDITION(TimeInStateCondition)

    public:

        static constexpr NodeDescription kDescription{
            "time_in_state", "Time In State", "State", "Passes once the enemy has been in this state this many seconds." };

        NodeValue<float> m_seconds = 10.0f;

        bool Check(const BehaviorContext& context, const StateInfo& state) const override;

    };  // TIMEINSTATECONDITION CLASS

    // Which way a state must have finished for State Finished to pass.
    enum class FinishedWith : std::uint8_t {

        Either,     // succeeded or failed, it doesn't matter
        Success,
        Failure

    };  // FINISHEDWITH ENUM

    // Passes once the state's tree (or single node) has finished - the "value
    // the behaviour graph returns" that tells the state machine to move on.
    class StateFinishedCondition final : public TransitionCondition {

        CRUCIBLE_TRANSITION_CONDITION(StateFinishedCondition)

    public:

        static constexpr NodeDescription kDescription{
            "state_finished", "State Finished", "State", "Passes once the state's behaviour has finished (with Success, Failure, or either)." };

        FinishedWith m_with = FinishedWith::Either;

        bool Check(const BehaviorContext& context, const StateInfo& state) const override;

    };  // STATEFINISHEDCONDITION CLASS

    // Passes when a bool blackboard variable has this value (missing counts as false).
    class BlackboardBoolCondition final : public TransitionCondition {

        CRUCIBLE_TRANSITION_CONDITION(BlackboardBoolCondition)

    public:

        static constexpr NodeDescription kDescription{
            "blackboard_bool", "Blackboard Bool", "Blackboard", "Passes when a bool variable has the given value." };

        std::string m_variable;
        bool        m_value = true;

        bool Check(const BehaviorContext& context, const StateInfo& state) const override;

    };  // BLACKBOARDBOOLCONDITION CLASS

}   // NAMESPACE CRUCIBLE
