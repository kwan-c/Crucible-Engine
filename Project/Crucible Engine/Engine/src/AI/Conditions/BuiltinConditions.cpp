/*************************************************************************
* \file:         BuiltinConditions.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The built-in arrow checks, their registration, and RTTR
*                registration of their fields.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "BuiltinConditions.h"

// STL
#include <optional>

// Crucible engine
#include "ECS/Components/Components.h"   // WorldTransform
#include "ECS/World.h"
#include "Reflection/ReflectionRegistration.h"

namespace Crucible {

    namespace {

        // The flat (XZ) distance from the enemy to `target`, or nothing if the
        // target is unset, dead, or has no world position yet.
        std::optional<float> FlatDistanceTo(const BehaviorContext& context, Entity target) {

            if (target == kNullEntity || !context.world->IsAlive(target)) return std::nullopt;
            const WorldTransform* self  = context.world->Read<WorldTransform>(context.self);
            const WorldTransform* other = context.world->Read<WorldTransform>(target);
            if (!self || !other || !self->valid || !other->valid) return std::nullopt;

            const Math::vec3 a(self->matrix[3]);
            const Math::vec3 b(other->matrix[3]);
            return glm::length(Math::vec2(b.x - a.x, b.z - a.z));

        }   // FlatDistanceTo()

    }   // ANONYMOUS NAMESPACE

    bool TargetWithinCondition::Check(const BehaviorContext& context, const StateInfo&) const {

        const std::optional<float> distance = FlatDistanceTo(context, m_target.Get(context));
        return distance && *distance <= m_distance.Get(context);

    }   // TargetWithinCondition::Check()

    bool TargetBeyondCondition::Check(const BehaviorContext& context, const StateInfo&) const {

        // No target at all is as far away as it gets: a chase with nobody to
        // chase should end, not carry on forever.
        const std::optional<float> distance = FlatDistanceTo(context, m_target.Get(context));
        return !distance || *distance > m_distance.Get(context);

    }   // TargetBeyondCondition::Check()

    bool TimeInStateCondition::Check(const BehaviorContext& context, const StateInfo& state) const {

        return state.timeInState >= m_seconds.Get(context);

    }   // TimeInStateCondition::Check()

    bool StateFinishedCondition::Check(const BehaviorContext&, const StateInfo& state) const {

        if (!state.finished) return false;
        switch (m_with) {
            case FinishedWith::Either:  return true;
            case FinishedWith::Success: return state.finishedWith == BehaviorStatus::Success;
            case FinishedWith::Failure: return state.finishedWith == BehaviorStatus::Failure;
        }
        return false;

    }   // StateFinishedCondition::Check()

    bool BlackboardBoolCondition::Check(const BehaviorContext& context, const StateInfo&) const {

        const bool* value = context.blackboard ? context.blackboard->Get<bool>(m_variable) : nullptr;
        return (value ? *value : false) == m_value;

    }   // BlackboardBoolCondition::Check()

    CRUCIBLE_REGISTER_TRANSITION_CONDITION(TargetWithinCondition)
    CRUCIBLE_REGISTER_TRANSITION_CONDITION(TargetBeyondCondition)
    CRUCIBLE_REGISTER_TRANSITION_CONDITION(TimeInStateCondition)
    CRUCIBLE_REGISTER_TRANSITION_CONDITION(StateFinishedCondition)
    CRUCIBLE_REGISTER_TRANSITION_CONDITION(BlackboardBoolCondition)

}   // NAMESPACE CRUCIBLE

// Property and enum value names are a file format: never rename one.
RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::enumeration<FinishedWith>("FinishedWith")
    (
        rttr::value("Either",  FinishedWith::Either),
        rttr::value("Success", FinishedWith::Success),
        rttr::value("Failure", FinishedWith::Failure)
    );

    rttr::registration::class_<TargetWithinCondition>("TargetWithinCondition")
        .property("target",   &TargetWithinCondition::m_target)
        .property("distance", &TargetWithinCondition::m_distance);

    rttr::registration::class_<TargetBeyondCondition>("TargetBeyondCondition")
        .property("target",   &TargetBeyondCondition::m_target)
        .property("distance", &TargetBeyondCondition::m_distance);

    rttr::registration::class_<TimeInStateCondition>("TimeInStateCondition")
        .property("seconds", &TimeInStateCondition::m_seconds);

    rttr::registration::class_<StateFinishedCondition>("StateFinishedCondition")
        .property("with", &StateFinishedCondition::m_with);

    rttr::registration::class_<BlackboardBoolCondition>("BlackboardBoolCondition")
        .property("variable", &BlackboardBoolCondition::m_variable)
        .property("value",    &BlackboardBoolCondition::m_value);
}
