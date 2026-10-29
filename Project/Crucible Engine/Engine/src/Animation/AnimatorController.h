#pragma once
#include "Animation/ModelPose.h"
#include <string_view>

namespace Crucible {
    enum class AnimatorParameterType : uint8_t { Float, Integer, Bool, Trigger };
    enum class AnimatorConditionMode : uint8_t { If, IfNot, Greater, Less, Equals, NotEqual };

    struct AnimatorValue {
        float number = 0.0f;
        int32_t integer = 0;
        bool boolean = false;
    };
    struct AnimatorParameter {
        std::string name;
        AnimatorParameterType type = AnimatorParameterType::Float;
        AnimatorValue initial;
    };
    struct AnimatorState {
        std::string name;
        uint32_t clip = InvalidModelIndex; // rest pose is a valid state
        float speed = 1.0f;
        bool loop = true;
    };
    struct AnimatorCondition {
        uint32_t parameter = InvalidModelIndex;
        AnimatorConditionMode mode = AnimatorConditionMode::If;
        AnimatorValue threshold;
    };
    struct AnimatorTransition {
        uint32_t from = InvalidModelIndex; // Any State
        uint32_t to = InvalidModelIndex;
        float duration = 0.2f;             // seconds at playback speed 1
        float offset = 0.0f;               // destination normalized start offset
        bool hasExitTime = false;
        float exitTime = 1.0f;             // entry offset + normalized cycles travelled
        bool canTransitionToSelf = false;
        std::vector<AnimatorCondition> conditions; // AND; array order sets priority
    };
    // Shared immutable definition, independent of ECS, renderer and editor.
    struct AnimatorController {
        uint32_t entryState = 0;
        std::vector<AnimatorParameter> parameters;
        std::vector<AnimatorState> states;
        std::vector<AnimatorTransition> transitions;
    };
    ENGINE_API bool ValidateAnimatorController(const AnimatorController&, std::string& error,
                                               const ModelResource* model = nullptr);

    // Mutable value-owned instance. Keep the definition/model alive and immutable
    // until Reset. Copies have independent parameters, clocks and blend buffers.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251) // Engine and clients use the same STL/toolchain, as ResourceManager does.
#endif
    class ENGINE_API AnimatorInstance {
    public:
        bool Reset(const AnimatorController&, const ModelResource&, std::string* error = nullptr,
                   float playbackSpeed = 1.0f);
        // advance=false samples the held state without transitions, clock changes
        // or trigger consumption. Negative speed reverses clips, not fade time.
        bool Update(double deltaSeconds, float playbackSpeed, bool advance, ModelPose& output);
        bool CrossFade(uint32_t state, float duration, float offset = 0.0f,
                       float playbackSpeed = 1.0f);
        bool SetFloat(std::string_view name, float value);
        bool SetInteger(std::string_view name, int32_t value);
        bool SetBool(std::string_view name, bool value);
        bool SetTrigger(std::string_view name);
        bool ResetTrigger(std::string_view name);
        const AnimatorValue* Parameter(uint32_t index) const;
        bool Initialized() const { return m_definition != nullptr; }
        uint32_t CurrentState() const { return m_current; }
        uint32_t NextState() const { return m_next; }
        float Time() const { return m_clock.time; }
        double NormalizedProgress() const { return m_clock.progress; }
        float TransitionWeight() const;
    private:
        struct Clock { float time = 0.0f; double progress = 0.0; };
        Clock EntryClock(uint32_t state, float offset, float speed) const;
        void StepClock(uint32_t state, Clock&, double elapsed, float speed) const;
        bool Conditions(const AnimatorTransition&) const;
        bool ExitReady(const AnimatorTransition&, double previous, bool firstUpdate) const;
        AnimatorValue* Find(std::string_view, AnimatorParameterType);
        const AnimatorController* m_definition = nullptr;
        const ModelResource* m_model = nullptr;
        std::vector<AnimatorValue> m_parameters;
        uint32_t m_current = InvalidModelIndex, m_next = InvalidModelIndex;
        Clock m_clock, m_nextClock;
        double m_fadeTime = 0.0;
        float m_fadeDuration = 0.0f;
        bool m_firstUpdate = true, m_frozen = false;
        std::vector<ModelLocalTransform> m_source, m_target, m_lastLocal;
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}
