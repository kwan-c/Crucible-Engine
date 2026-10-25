#include "Animation/AnimatorDocument.h"
#include "Resources/AnimatorControllerIO.h"
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace Crucible {
    namespace {
        bool ValidPoint(AnimatorGraphPoint p) {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::abs(p.x) <= 1e6f && std::abs(p.y) <= 1e6f;
        }
        std::string UniqueName(const AnimatorController& c) {
            for (size_t index = 1; ; ++index) {
                auto name = "State " + std::to_string(index);
                if (std::none_of(c.states.begin(), c.states.end(), [&](const auto& s) { return s.name == name; })) return name;
            }
        }
    }
    bool AnimatorDocument::SameRuntime(const AnimatorController& a, const AnimatorController& b) {
        return SameAnimatorControllerDefinition(a, b);
    }
    bool AnimatorDocument::Same(const Snapshot& a, const Snapshot& b) {
        return SameRuntime(a.controller, b.controller) && a.positions == b.positions &&
            a.anyState == b.anyState && a.previewModel == b.previewModel;
    }
    void AnimatorDocument::New() {
        m_data = {};
        m_data.controller.states.push_back({"State 1"});
        m_data.positions.push_back({0, 0});
        m_saved = m_data; m_undo.clear(); m_redo.clear(); m_before.reset();
    }
    bool AnimatorDocument::Load(const std::string& json, std::string& error) {
        auto decoded = DecodeAnimatorController({reinterpret_cast<const unsigned char*>(json.data()), json.size()});
        if (!decoded.ok) { error = decoded.error; return false; }
        Snapshot data;
        data.controller = std::move(decoded.controller);
        for (size_t i = 0; i < data.controller.states.size(); ++i)
            data.positions.push_back({float(i % 3) * 280, float(i / 3) * 150});
        rapidjson::Document doc; doc.Parse(json.data(), json.size());
        try {
            if (doc.HasMember("editor")) {
                const auto& e = doc["editor"];
                if (!e.IsObject() || !e.HasMember("version") || !e["version"].IsUint() || e["version"].GetUint() != 1)
                    throw std::runtime_error("Unsupported Animator editor metadata version");
                const auto point = [](const rapidjson::Value& v) {
                    if (!v.IsArray() || v.Size() != 2 || !v[0].IsNumber() || !v[1].IsNumber())
                        throw std::runtime_error("Invalid graph position");
                    const double x = v[0].GetDouble(), y = v[1].GetDouble();
                    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 1e6 || std::abs(y) > 1e6)
                        throw std::runtime_error("Graph position exceeds finite +/-1,000,000 range");
                    return AnimatorGraphPoint{float(x), float(y)};
                };
                if (e.HasMember("positions")) {
                    const auto& positions = e["positions"];
                    if (!positions.IsArray() || positions.Size() != data.positions.size()) throw std::runtime_error("Graph position count must match states");
                    for (size_t i = 0; i < data.positions.size(); ++i) data.positions[i] = point(positions[static_cast<rapidjson::SizeType>(i)]);
                }
                if (e.HasMember("anyState")) data.anyState = point(e["anyState"]);
                if (e.HasMember("previewModel")) {
                    const auto& id = e["previewModel"];
                    if (!id.IsString()) throw std::runtime_error("Preview model GUID must be hexadecimal text");
                    auto end = id.GetString() + id.GetStringLength();
                    auto result = std::from_chars(id.GetString(), end, data.previewModel, 16);
                    if (result.ec != std::errc{} || result.ptr != end) throw std::runtime_error("Invalid preview model GUID");
                }
            }
        } catch (const std::exception& e) { error = e.what(); return false; }
        m_data = std::move(data); MarkSaved(); m_undo.clear(); m_redo.clear(); m_before.reset(); error.clear();
        return true;
    }
    bool AnimatorDocument::Serialize(std::string& json, std::string& error, std::optional<uint64_t> sourceModel) const {
        try {
            if (m_data.positions.size() != m_data.controller.states.size() || !ValidPoint(m_data.anyState) ||
                std::any_of(m_data.positions.begin(), m_data.positions.end(), [](auto p) { return !ValidPoint(p); }))
                throw std::runtime_error("Invalid graph positions");
            const auto runtime = EncodeAnimatorController(m_data.controller);
            rapidjson::Document doc; doc.Parse(runtime.data(), runtime.size());
            auto& allocator = doc.GetAllocator();
            rapidjson::Value editor(rapidjson::kObjectType), positions(rapidjson::kArrayType);
            const auto point = [&](AnimatorGraphPoint p) {
                rapidjson::Value value(rapidjson::kArrayType);
                value.PushBack(p.x, allocator).PushBack(p.y, allocator); return value;
            };
            for (auto p : m_data.positions) positions.PushBack(point(p), allocator);
            editor.AddMember("version", 1, allocator);
            editor.AddMember("positions", positions, allocator);
            editor.AddMember("anyState", point(m_data.anyState), allocator);
            char guid[17]; std::snprintf(guid, sizeof(guid), "%016llX", static_cast<unsigned long long>(sourceModel.value_or(m_data.previewModel)));
            editor.AddMember("previewModel", rapidjson::Value(guid, allocator), allocator);
            doc.AddMember("editor", editor, allocator);
            rapidjson::StringBuffer buffer; rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer); doc.Accept(writer);
            if (buffer.GetSize() > MaxAnimatorControllerBytes) throw std::runtime_error("Controller plus layout exceeds 4 MiB");
            json.assign(buffer.GetString(), buffer.GetSize()); error.clear(); return true;
        } catch (const std::exception& e) { error = e.what(); return false; }
    }
    void AnimatorDocument::MarkSaved(std::optional<uint64_t> sourceModel) {
        EndEdit();
        // Remember automatically inferred context only after a successful write,
        // without adding a separate undo step or dirtying the draft on selection.
        if (sourceModel) {
            m_data.previewModel = *sourceModel;
            if (m_before) m_before->previewModel = *sourceModel;
        }
        m_saved = m_data;
    }
    void AnimatorDocument::DiscardChanges() {
        m_data = m_saved;
        m_before.reset(); m_undo.clear(); m_redo.clear();
    }
    bool AnimatorDocument::Dirty() const { return !Same(m_data, m_saved); }
    bool AnimatorDocument::RuntimeMatchesSaved() const { return SameRuntime(m_data.controller, m_saved.controller); }
    void AnimatorDocument::BeginEdit() { if (!m_before) m_before = m_data; }
    void AnimatorDocument::EndEdit(bool keepOpen) {
        if (keepOpen || !m_before) return;
        if (!Same(*m_before, m_data)) {
            if (m_undo.size() == 20) m_undo.erase(m_undo.begin());
            m_undo.push_back(std::move(*m_before)); m_redo.clear();
            m_before = m_data;
        }
    }
    bool AnimatorDocument::CanUndo() const { return !m_undo.empty() || (m_before && !Same(*m_before, m_data)); }
    void AnimatorDocument::Undo() {
        EndEdit(); if (m_undo.empty()) return;
        m_redo.push_back(std::move(m_data)); m_data = std::move(m_undo.back()); m_undo.pop_back(); m_before = m_data;
    }
    void AnimatorDocument::Redo() {
        EndEdit(); if (m_redo.empty()) return;
        m_undo.push_back(std::move(m_data)); m_data = std::move(m_redo.back()); m_redo.pop_back(); m_before = m_data;
    }
    uint32_t AnimatorDocument::AddState(AnimatorGraphPoint position) {
        if (m_data.controller.states.size() >= 4096) return InvalidModelIndex;
        BeginEdit(); auto index = static_cast<uint32_t>(m_data.controller.states.size());
        m_data.controller.states.push_back({UniqueName(m_data.controller)}); m_data.positions.push_back(position); return index;
    }
    uint32_t AnimatorDocument::DuplicateState(uint32_t state) {
        if (state >= m_data.controller.states.size()) return InvalidModelIndex;
        const auto source = m_data.controller.states[state]; auto p = m_data.positions[state]; p.x += 35; p.y += 35;
        auto index = AddState(p); if (index != InvalidModelIndex) { auto name = m_data.controller.states[index].name; m_data.controller.states[index] = source; m_data.controller.states[index].name = name; } return index;
    }
    uint32_t AnimatorDocument::AddClipState(const ModelResource& model, uint32_t clip, AnimatorGraphPoint position) {
        if (clip >= model.animations.size() || !ValidPoint(position)) return InvalidModelIndex;
        const auto index = AddState(position);
        if (index == InvalidModelIndex) return index;
        auto& states = m_data.controller.states;
        const auto base = model.animations[clip].name.empty() ? "Clip " + std::to_string(clip) : model.animations[clip].name;
        auto name = base;
        for (size_t suffix = 2; std::any_of(states.begin(), states.end(), [&](const auto& state) { return &state != &states[index] && state.name == name; }); ++suffix)
            name = base + " " + std::to_string(suffix);
        states[index].name = std::move(name); states[index].clip = clip;
        return index;
    }
    size_t AnimatorDocument::AddMissingClips(const ModelResource& model, AnimatorGraphPoint origin) {
        if (!ValidPoint(origin)) return 0;
        std::vector<bool> present(model.animations.size(), false);
        for (const auto& state : m_data.controller.states)
            if (state.clip < present.size()) present[state.clip] = true;
        size_t added = 0;
        for (uint32_t clip = 0; clip < model.animations.size(); ++clip) {
            if (present[clip]) continue;
            AnimatorGraphPoint position{origin.x + float(added % 4) * 280, origin.y + float(added / 4) * 145};
            if (AddClipState(model, clip, position) == InvalidModelIndex) break;
            ++added;
        }
        return added;
    }
    void AnimatorDocument::RemoveState(uint32_t state) {
        auto& c = m_data.controller;
        if (state >= c.states.size() || c.states.size() == 1) return;
        BeginEdit(); c.states.erase(c.states.begin() + state); m_data.positions.erase(m_data.positions.begin() + state);
        if (c.entryState == state) c.entryState = 0; else if (c.entryState > state) --c.entryState;
        std::erase_if(c.transitions, [&](const auto& t) { return t.from == state || t.to == state; });
        for (auto& t : c.transitions) { if (t.from != InvalidModelIndex && t.from > state) --t.from; if (t.to > state) --t.to; }
    }
    void AnimatorDocument::RemoveParameter(uint32_t parameter) {
        auto& c = m_data.controller;
        if (parameter >= c.parameters.size()) return;
        BeginEdit(); c.parameters.erase(c.parameters.begin() + parameter);
        for (auto& t : c.transitions) {
            std::erase_if(t.conditions, [&](const auto& condition) { return condition.parameter == parameter; });
            for (auto& condition : t.conditions) if (condition.parameter > parameter) --condition.parameter;
        }
    }
    void AnimatorDocument::ChangeParameterType(uint32_t parameter, AnimatorParameterType type) {
        auto& c = m_data.controller;
        if (parameter >= c.parameters.size() || type > AnimatorParameterType::Trigger || type == c.parameters[parameter].type) return;
        BeginEdit(); c.parameters[parameter].type = type; c.parameters[parameter].initial = {};
        for (auto& t : c.transitions) for (auto& condition : t.conditions) if (condition.parameter == parameter) {
            condition.mode = type == AnimatorParameterType::Bool || type == AnimatorParameterType::Trigger ? AnimatorConditionMode::If : AnimatorConditionMode::Greater;
            condition.threshold = {};
        }
    }
    uint32_t AnimatorDocument::AddTransition(uint32_t from, uint32_t to) {
        auto& c = m_data.controller;
        if ((from != InvalidModelIndex && from >= c.states.size()) || to >= c.states.size() || c.transitions.size() >= 16384) return InvalidModelIndex;
        BeginEdit(); AnimatorTransition t; t.from = from; t.to = to; t.canTransitionToSelf = from == to;
        c.transitions.push_back(t); return static_cast<uint32_t>(c.transitions.size() - 1);
    }
}
