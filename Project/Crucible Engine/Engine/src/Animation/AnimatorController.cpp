#include "Animation/AnimatorController.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Crucible {
    bool ValidateAnimatorController(const AnimatorController& c, std::string& error, const ModelResource* model) {
        const auto fail = [&](const std::string& text) { error = text; return false; };
        error.clear();
        if (c.states.empty() || c.states.size() > 4096 || c.entryState >= c.states.size())
            return fail("Controller needs 1..4096 states and a valid entryState");
        if (c.parameters.size() > 4096 || c.transitions.size() > 16384)
            return fail("Controller parameter/transition limit exceeded");
        std::unordered_set<std::string> names;
        for (const auto& p : c.parameters) {
            if (p.name.empty() || !names.insert(p.name).second) return fail("Parameter names must be nonempty and unique");
            if (p.type > AnimatorParameterType::Trigger || !std::isfinite(p.initial.number)) return fail("Invalid parameter type/default");
            if (p.type == AnimatorParameterType::Trigger && p.initial.boolean) return fail("Triggers must default to false");
        }
        names.clear();
        for (const auto& s : c.states) {
            if (s.name.empty() || !names.insert(s.name).second) return fail("State names must be nonempty and unique");
            if (!std::isfinite(s.speed)) return fail("State speed must be finite");
            if (model && s.clip != InvalidModelIndex && s.clip >= model->animations.size())
                return fail("State '" + s.name + "' references a clip outside this model");
        }
        for (const auto& t : c.transitions) {
            if ((t.from != InvalidModelIndex && t.from >= c.states.size()) || t.to >= c.states.size())
                return fail("Transition state index out of range");
            if (!std::isfinite(t.duration) || t.duration < 0 || !std::isfinite(t.offset) || t.offset < 0 || t.offset > 1 ||
                !std::isfinite(t.exitTime) || t.exitTime < 0 || t.conditions.size() > 64)
                return fail("Invalid transition duration, offset, exitTime or condition count");
            if (t.from == t.to && !t.canTransitionToSelf) return fail("Self transition requires canTransitionToSelf");
            for (const auto& condition : t.conditions) {
                if (condition.parameter >= c.parameters.size() || condition.mode > AnimatorConditionMode::NotEqual ||
                    !std::isfinite(condition.threshold.number)) return fail("Invalid condition parameter/mode/threshold");
                const auto type = c.parameters[condition.parameter].type;
                const bool booleanMode = condition.mode == AnimatorConditionMode::If || condition.mode == AnimatorConditionMode::IfNot;
                if (booleanMode != (type == AnimatorParameterType::Bool || type == AnimatorParameterType::Trigger))
                    return fail("Boolean/trigger conditions require If or IfNot; numeric conditions require comparison modes");
                if (type == AnimatorParameterType::Trigger && condition.mode != AnimatorConditionMode::If)
                    return fail("Trigger conditions support If only");
            }
        }
        return true;
    }

    bool AnimatorInstance::Reset(const AnimatorController& definition, const ModelResource& model,
                                 std::string* error, float speed) {
        *this = AnimatorInstance{};
        std::string reason;
        if (!ValidateAnimatorController(definition, reason, &model)) { if (error) *error = reason; return false; }
        if (error) error->clear();
        m_definition = &definition; m_model = &model;
        for (const auto& p : definition.parameters) m_parameters.push_back(p.initial);
        m_current = definition.entryState;
        m_clock = EntryClock(m_current, 0.0f, speed);
        return true;
    }

    AnimatorInstance::Clock AnimatorInstance::EntryClock(uint32_t index, float offset, float speed) const {
        const auto& state = m_definition->states[index];
        const float duration = state.clip < m_model->animations.size() ? m_model->animations[state.clip].duration : 0.0f;
        return {duration * (double(state.speed) * speed < 0 ? 1.0f - offset : offset), double(offset)};
    }

    void AnimatorInstance::StepClock(uint32_t index, Clock& clock, double elapsed, float speed) const {
        const auto& state = m_definition->states[index];
        const float duration = state.clip < m_model->animations.size() ? m_model->animations[state.clip].duration : 0.0f;
        const double rate = double(state.speed) * speed;
        if (duration <= 0) { clock.time = 0; clock.progress = 1.0; return; }
        const double travel = elapsed * std::abs(rate);
        if (!std::isfinite(travel)) return;
        double distance = travel;
        if (!state.loop) distance = std::min(distance, rate < 0 ? double(clock.time) : double(duration - clock.time));
        // Separate the unwrapped exit clock from the wrapped sample clock.
        // Avoid narrowing a finite combined rate that exceeds float range.
        clock.time = StepModelPlayback(clock.time, elapsed * std::abs(double(speed)),
                                       speed < 0 ? -state.speed : state.speed, duration, state.loop).time;
        const double progress = clock.progress + distance / duration;
        clock.progress = std::isfinite(progress) ? progress : std::numeric_limits<double>::max();
        // The sample clock clamps in float precision; accumulated double progress
        // can otherwise stop just below one and never satisfy a 100% exit time.
        // Preserve any extra travel accumulated by direction changes mid-clip.
        if (!state.loop && ((rate > 0 && clock.time >= duration) || (rate < 0 && clock.time <= 0)))
            clock.progress = std::max(clock.progress, 1.0);
    }

    bool AnimatorInstance::Conditions(const AnimatorTransition& transition) const {
        for (const auto& c : transition.conditions) {
            const auto& value = m_parameters[c.parameter];
            const auto type = m_definition->parameters[c.parameter].type;
            if (c.mode == AnimatorConditionMode::If) { if (!value.boolean) return false; continue; }
            if (c.mode == AnimatorConditionMode::IfNot) { if (value.boolean) return false; continue; }
            const double a = type == AnimatorParameterType::Integer ? double(value.integer) : value.number;
            const double b = type == AnimatorParameterType::Integer ? double(c.threshold.integer) : c.threshold.number;
            switch (c.mode) {
            case AnimatorConditionMode::Greater: if (!(a > b)) return false; break;
            case AnimatorConditionMode::Less: if (!(a < b)) return false; break;
            case AnimatorConditionMode::Equals: if (a != b) return false; break;
            case AnimatorConditionMode::NotEqual: if (a == b) return false; break;
            default: return false;
            }
        }
        return true;
    }

    bool AnimatorInstance::ExitReady(const AnimatorTransition& t, double previous, bool firstUpdate) const {
        if (!t.hasExitTime) return true;
        const auto& state = m_definition->states[m_current];
        if (!state.loop || t.exitTime >= 1.0f) return m_clock.progress >= t.exitTime;
        // Loop exits below one are checked at that phase on every cycle, even
        // when a long frame crosses several complete cycles.
        return (firstUpdate && t.exitTime == 0) ||
            std::floor(m_clock.progress - t.exitTime) > std::floor(previous - t.exitTime);
    }

    float AnimatorInstance::TransitionWeight() const {
        return m_next == InvalidModelIndex ? 0.0f :
            static_cast<float>(std::clamp(m_fadeTime / m_fadeDuration, 0.0, 1.0));
    }

    bool AnimatorInstance::CrossFade(uint32_t state, float duration, float offset, float speed) {
        if (!m_definition || state >= m_definition->states.size() || !std::isfinite(duration) || duration < 0 ||
            !std::isfinite(offset) || offset < 0 || offset > 1 || !std::isfinite(speed)) return false;
        const auto nextClock = EntryClock(state, offset, speed);
        if (duration == 0) {
            m_current = state; m_clock = nextClock; m_next = InvalidModelIndex;
            m_frozen = false; m_firstUpdate = true;
        } else {
            // Explicit gameplay interruption begins from the last displayed
            // blended local pose. This prevents a jump back to a source clip.
            if (m_next != InvalidModelIndex && !m_lastLocal.empty()) { m_source = m_lastLocal; m_frozen = true; }
            else m_frozen = false;
            m_next = state; m_nextClock = nextClock; m_fadeDuration = duration; m_fadeTime = 0;
        }
        return true;
    }

    bool AnimatorInstance::Update(double elapsed, float speed, bool advance, ModelPose& output) {
        if (!m_definition) { output = {}; return false; }
        if (!std::isfinite(elapsed) || elapsed < 0 || !std::isfinite(speed) ||
            !std::isfinite(elapsed * std::abs(double(speed)))) advance = false;
        const double previous = m_clock.progress;
        const bool wasTransitioning = m_next != InvalidModelIndex;
        if (advance) {
            if (!m_frozen) StepClock(m_current, m_clock, elapsed, speed);
            if (wasTransitioning) {
                StepClock(m_next, m_nextClock, elapsed, speed);
                m_fadeTime += elapsed * std::abs(double(speed));
                if (m_fadeTime >= m_fadeDuration) {
                    m_current = m_next; m_clock = m_nextClock; m_next = InvalidModelIndex;
                    m_frozen = false; m_firstUpdate = true;
                }
            } else {
                const bool firstUpdate = m_firstUpdate;
                m_firstUpdate = false;
                for (const auto& transition : m_definition->transitions) {
                    if (transition.from != InvalidModelIndex && transition.from != m_current) continue;
                    if (transition.to == m_current && !transition.canTransitionToSelf) continue;
                    if (!ExitReady(transition, previous, firstUpdate) || !Conditions(transition)) continue;
                    CrossFade(transition.to, transition.duration, transition.offset, speed);
                    for (const auto& c : transition.conditions)
                        if (m_definition->parameters[c.parameter].type == AnimatorParameterType::Trigger)
                            m_parameters[c.parameter].boolean = false;
                    break; // deterministic priority; at most one automatic transition per update
                }
            }
        }
        const auto sample = [&](uint32_t state, const Clock& clock, auto& local) {
            return SampleModelLocalPose(*m_model, m_definition->states[state].clip, clock.time, local);
        };
        if (m_next == InvalidModelIndex) {
            if (!sample(m_current, m_clock, output.local)) { output = {}; return false; }
        } else {
            if ((!m_frozen && !sample(m_current, m_clock, m_source)) || !sample(m_next, m_nextClock, m_target)) {
                output = {}; return false;
            }
            output.local.resize(m_model->nodes.size());
            const float alpha = TransitionWeight();
            for (size_t i = 0; i < output.local.size(); ++i) {
                auto& local = output.local[i];
                const auto& a = m_source[i]; const auto& b = m_target[i];
                local = a; // exact authored matrices survive unchanged
                if (local.hasMatrix) continue;
                local.translation = glm::mix(a.translation, b.translation, alpha);
                local.scale = glm::mix(a.scale, b.scale, alpha);
                local.rotation = glm::normalize(glm::slerp(glm::normalize(a.rotation), glm::normalize(b.rotation), alpha));
            }
        }
        m_lastLocal = output.local;
        return BuildModelPose(*m_model, output);
    }

    AnimatorValue* AnimatorInstance::Find(std::string_view name, AnimatorParameterType type) {
        if (!m_definition) return nullptr;
        for (size_t i = 0; i < m_definition->parameters.size(); ++i)
            if (m_definition->parameters[i].name == name && m_definition->parameters[i].type == type) return &m_parameters[i];
        return nullptr;
    }
    const AnimatorValue* AnimatorInstance::Parameter(uint32_t index) const {
        return index < m_parameters.size() ? &m_parameters[index] : nullptr;
    }
    bool AnimatorInstance::SetFloat(std::string_view name, float value) {
        auto* p = Find(name, AnimatorParameterType::Float); if (!p || !std::isfinite(value)) return false;
        p->number = value; return true;
    }
    bool AnimatorInstance::SetInteger(std::string_view name, int32_t value) {
        auto* p = Find(name, AnimatorParameterType::Integer); if (!p) return false; p->integer = value; return true;
    }
    bool AnimatorInstance::SetBool(std::string_view name, bool value) {
        auto* p = Find(name, AnimatorParameterType::Bool); if (!p) return false; p->boolean = value; return true;
    }
    bool AnimatorInstance::SetTrigger(std::string_view name) {
        auto* p = Find(name, AnimatorParameterType::Trigger); if (!p) return false; p->boolean = true; return true;
    }
    bool AnimatorInstance::ResetTrigger(std::string_view name) {
        auto* p = Find(name, AnimatorParameterType::Trigger); if (!p) return false; p->boolean = false; return true;
    }
}
