#include "Resources/AnimatorControllerIO.h"
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <cmath>
#include <stdexcept>
#include <algorithm>

namespace Crucible {
    namespace {
        bool SameNumber(float a, float b) { return a == b || (std::isnan(a) && std::isnan(b)); }
        bool SameValue(const AnimatorValue& a, const AnimatorValue& b) {
            return SameNumber(a.number, b.number) && a.integer == b.integer && a.boolean == b.boolean;
        }
        template<class T, class F> bool SameArray(const std::vector<T>& a, const std::vector<T>& b, F equal) {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), equal);
        }
        using Value = rapidjson::Value;
        constexpr const char* types[] = {"Float", "Integer", "Bool", "Trigger"};
        constexpr const char* modes[] = {"If", "IfNot", "Greater", "Less", "Equals", "NotEqual"};
        const Value& Required(const Value& object, const char* name) {
            if (!object.IsObject() || !object.HasMember(name)) throw std::runtime_error(std::string("Missing field: ") + name);
            return object[name];
        }
        uint32_t UInt(const Value& value) {
            if (!value.IsUint()) throw std::runtime_error("Expected an unsigned 32-bit integer");
            return value.GetUint();
        }
        std::string Text(const Value& value) {
            if (!value.IsString()) throw std::runtime_error("Expected a string");
            return {value.GetString(), value.GetStringLength()};
        }
        bool Boolean(const Value& value) {
            if (!value.IsBool()) throw std::runtime_error("Expected a boolean");
            return value.GetBool();
        }
        float Number(const Value& value) {
            if (!value.IsNumber() || !std::isfinite(value.GetDouble()) || std::abs(value.GetDouble()) > std::numeric_limits<float>::max())
                throw std::runtime_error("Expected a finite float");
            return static_cast<float>(value.GetDouble());
        }
        const Value& Array(const Value& object, const char* name, size_t limit) {
            const auto& value = Required(object, name);
            if (!value.IsArray() || value.Size() > limit) throw std::runtime_error(std::string("Invalid/oversized array: ") + name);
            return value;
        }
        template<size_t N> uint8_t Enum(const Value& value, const char* const (&names)[N]) {
            const auto text = Text(value);
            for (uint8_t i = 0; i < N; ++i) if (text == names[i]) return i;
            throw std::runtime_error("Unknown enum: " + text);
        }
        AnimatorValue ReadValue(const Value& value, AnimatorParameterType type) {
            AnimatorValue out;
            switch (type) {
            case AnimatorParameterType::Float: out.number = Number(value); break;
            case AnimatorParameterType::Integer:
                if (!value.IsInt()) throw std::runtime_error("Expected a signed 32-bit integer");
                out.integer = value.GetInt(); break;
            default: out.boolean = Boolean(value); break;
            }
            return out;
        }
    }

    bool SameAnimatorControllerDefinition(const AnimatorController& a, const AnimatorController& b) {
        return a.entryState == b.entryState &&
            SameArray(a.parameters, b.parameters, [](const auto& x, const auto& y) { return x.name == y.name && x.type == y.type && SameValue(x.initial, y.initial); }) &&
            SameArray(a.states, b.states, [](const auto& x, const auto& y) { return x.name == y.name && x.clip == y.clip && SameNumber(x.speed, y.speed) && x.loop == y.loop; }) &&
            SameArray(a.transitions, b.transitions, [](const auto& x, const auto& y) {
                return x.from == y.from && x.to == y.to && SameNumber(x.duration, y.duration) && SameNumber(x.offset, y.offset) &&
                    x.hasExitTime == y.hasExitTime && SameNumber(x.exitTime, y.exitTime) && x.canTransitionToSelf == y.canTransitionToSelf &&
                    SameArray(x.conditions, y.conditions, [](const auto& p, const auto& q) { return p.parameter == q.parameter && p.mode == q.mode && SameValue(p.threshold, q.threshold); });
            });
    }

    AnimatorControllerResult DecodeAnimatorController(std::span<const unsigned char> bytes) {
        AnimatorControllerResult result;
        try {
            if (bytes.empty() || bytes.size() > MaxAnimatorControllerBytes) throw std::runtime_error("Controller file is empty or exceeds 4 MiB");
            rapidjson::Document doc;
            doc.Parse<rapidjson::kParseValidateEncodingFlag>(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            if (doc.HasParseError()) throw std::runtime_error(std::string("Invalid controller JSON: ") + rapidjson::GetParseError_En(doc.GetParseError()));
            if (UInt(Required(doc, "version")) != 1) throw std::runtime_error("Unsupported controller version");
            AnimatorController c;
            c.entryState = UInt(Required(doc, "entryState"));
            for (const auto& p : Array(doc, "parameters", 4096).GetArray()) {
                AnimatorParameter parameter;
                parameter.name = Text(Required(p, "name"));
                parameter.type = static_cast<AnimatorParameterType>(Enum(Required(p, "type"), types));
                if (p.HasMember("default")) parameter.initial = ReadValue(p["default"], parameter.type);
                c.parameters.push_back(std::move(parameter));
            }
            for (const auto& s : Array(doc, "states", 4096).GetArray()) {
                AnimatorState state;
                state.name = Text(Required(s, "name")); state.clip = UInt(Required(s, "clip"));
                if (s.HasMember("speed")) state.speed = Number(s["speed"]);
                if (s.HasMember("loop")) state.loop = Boolean(s["loop"]);
                c.states.push_back(std::move(state));
            }
            for (const auto& t : Array(doc, "transitions", 16384).GetArray()) {
                AnimatorTransition transition;
                const auto& from = Required(t, "from");
                if (from.IsString()) {
                    if (Text(from) != "Any") throw std::runtime_error("Transition from string must be Any");
                } else transition.from = UInt(from);
                transition.to = UInt(Required(t, "to"));
                if (t.HasMember("duration")) transition.duration = Number(t["duration"]);
                if (t.HasMember("offset")) transition.offset = Number(t["offset"]);
                if (t.HasMember("hasExitTime")) transition.hasExitTime = Boolean(t["hasExitTime"]);
                if (t.HasMember("exitTime")) transition.exitTime = Number(t["exitTime"]);
                if (t.HasMember("canTransitionToSelf")) transition.canTransitionToSelf = Boolean(t["canTransitionToSelf"]);
                for (const auto& entry : Array(t, "conditions", 64).GetArray()) {
                    AnimatorCondition condition;
                    condition.parameter = UInt(Required(entry, "parameter"));
                    if (condition.parameter >= c.parameters.size()) throw std::runtime_error("Condition parameter out of range");
                    condition.mode = static_cast<AnimatorConditionMode>(Enum(Required(entry, "mode"), modes));
                    const auto type = c.parameters[condition.parameter].type;
                    if (type == AnimatorParameterType::Float || type == AnimatorParameterType::Integer)
                        condition.threshold = ReadValue(Required(entry, "value"), type);
                    transition.conditions.push_back(condition);
                }
                c.transitions.push_back(std::move(transition));
            }
            if (!ValidateAnimatorController(c, result.error)) return result;
            result.controller = std::move(c); result.ok = true;
        } catch (const std::exception& e) { result.error = e.what(); }
        return result;
    }

    std::string EncodeAnimatorController(const AnimatorController& c) {
        std::string reason;
        if (!ValidateAnimatorController(c, reason)) throw std::runtime_error(reason);
        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> w(buffer);
        const auto value = [&](const AnimatorValue& v, AnimatorParameterType type) {
            switch (type) {
            case AnimatorParameterType::Float: w.Double(v.number); break;
            case AnimatorParameterType::Integer: w.Int(v.integer); break;
            default: w.Bool(v.boolean); break;
            }
        };
        w.StartObject(); w.Key("version"); w.Uint(1); w.Key("entryState"); w.Uint(c.entryState);
        w.Key("parameters"); w.StartArray();
        for (const auto& p : c.parameters) {
            w.StartObject(); w.Key("name"); w.String(p.name.c_str(), static_cast<rapidjson::SizeType>(p.name.size()));
            w.Key("type"); w.String(types[static_cast<uint8_t>(p.type)]);
            w.Key("default"); value(p.initial, p.type); w.EndObject();
        }
        w.EndArray(); w.Key("states"); w.StartArray();
        for (const auto& s : c.states) {
            w.StartObject(); w.Key("name"); w.String(s.name.c_str(), static_cast<rapidjson::SizeType>(s.name.size()));
            w.Key("clip"); w.Uint(s.clip); w.Key("speed"); w.Double(s.speed); w.Key("loop"); w.Bool(s.loop); w.EndObject();
        }
        w.EndArray(); w.Key("transitions"); w.StartArray();
        for (const auto& t : c.transitions) {
            w.StartObject(); w.Key("from"); if (t.from == InvalidModelIndex) w.String("Any"); else w.Uint(t.from);
            w.Key("to"); w.Uint(t.to); w.Key("duration"); w.Double(t.duration); w.Key("offset"); w.Double(t.offset);
            w.Key("hasExitTime"); w.Bool(t.hasExitTime); w.Key("exitTime"); w.Double(t.exitTime);
            w.Key("canTransitionToSelf"); w.Bool(t.canTransitionToSelf); w.Key("conditions"); w.StartArray();
            for (const auto& condition : t.conditions) {
                w.StartObject(); w.Key("parameter"); w.Uint(condition.parameter); w.Key("mode"); w.String(modes[static_cast<uint8_t>(condition.mode)]);
                const auto type = c.parameters[condition.parameter].type;
                if (type == AnimatorParameterType::Float || type == AnimatorParameterType::Integer) {
                    w.Key("value"); value(condition.threshold, type);
                }
                w.EndObject();
            }
            w.EndArray(); w.EndObject();
        }
        w.EndArray(); w.EndObject();
        if (buffer.GetSize() > MaxAnimatorControllerBytes) throw std::runtime_error("Encoded controller exceeds 4 MiB");
        return {buffer.GetString(), buffer.GetSize()};
    }
}
