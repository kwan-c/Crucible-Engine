/*************************************************************************
* \file:         StateGraphIO.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The .stategraph JSON format: states (each with its tree),
*                arrows (each with its conditions), entry state, variables.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "StateGraphIO.h"

// rapidjson
#include <rapidjson/error/en.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

// Crucible engine
#include "AI/BehaviorGraphIO.h"         // BehaviorJson: trees, variables, vec2 - shared with .behaviorgraph
#include "Reflection/Serialization.h"   // ToJsonValue / FromJsonValue: condition fields through RTTR

namespace Crucible {

    namespace {

        const rapidjson::Value* Member(const rapidjson::Value& in, const char* name) {

            if (!in.IsObject()) return nullptr;
            const auto found = in.FindMember(name);
            return found == in.MemberEnd() ? nullptr : &found->value;

        }   // Member()

        bool ReadState(const rapidjson::Value& in, GraphState& state, std::string& error) {

            const rapidjson::Value* name = Member(in, "name");
            if (!name || !name->IsString()) { error = "a state has no \"name\""; return false; }
            state.m_name = name->GetString();

            if (const rapidjson::Value* single = Member(in, "singleNode"); single && single->IsBool()) state.m_singleNode = single->GetBool();
            if (const rapidjson::Value* editor = Member(in, "editor")) state.m_editorPosition = BehaviorJson::ReadVec2(*editor);

            // No tree is allowed: an empty state just waits for its arrows.
            if (const rapidjson::Value* tree = Member(in, "tree")) {
                if (!BehaviorJson::ReadTree(*tree, state.m_behavior, error)) {
                    error = "state \"" + state.m_name + "\": " + error;
                    return false;
                }
            }
            return true;

        }   // ReadState()

        bool ReadTransition(const rapidjson::Value& in, int stateCount, StateTransition& transition, std::string& error) {

            const rapidjson::Value* from = Member(in, "from");
            const rapidjson::Value* to   = Member(in, "to");
            if (!from || !from->IsInt() || !to || !to->IsInt()) { error = "an arrow has no \"from\" or \"to\""; return false; }

            transition.m_from = from->GetInt();
            transition.m_to   = to->GetInt();
            if (transition.m_from < 0 || transition.m_from >= stateCount || transition.m_to < 0 || transition.m_to >= stateCount) {
                error = "an arrow points at a state that doesn't exist (" + std::to_string(transition.m_from) + " -> " + std::to_string(transition.m_to) + ")";
                return false;
            }

            const rapidjson::Value* conditions = Member(in, "conditions");
            if (!conditions) return true;   // no conditions: switches at once
            if (!conditions->IsArray()) { error = "an arrow's \"conditions\" is not a list"; return false; }

            for (const rapidjson::Value& entry : conditions->GetArray()) {

                const rapidjson::Value* type = Member(entry, "type");
                if (!type || !type->IsString()) { error = "a condition has no \"type\""; return false; }

                const ConditionTypeEntry* registered = ConditionRegistry::Instance().Find(type->GetString());
                if (!registered) { error = std::string("unknown condition type \"") + type->GetString() + "\""; return false; }

                std::unique_ptr<TransitionCondition> condition = registered->create();
                if (const rapidjson::Value* fields = Member(entry, "fields")) {
                    if (!Reflection::FromJsonValue(*fields, registered->reflect(*condition))) {
                        error = std::string("the fields of a \"") + type->GetString() + "\" condition could not be read";
                        return false;
                    }
                }
                transition.m_conditions.push_back(std::move(condition));

            }
            return true;

        }   // ReadTransition()

    }   // ANONYMOUS NAMESPACE

    StateGraphResult DecodeStateGraph(std::span<const unsigned char> bytes) {

        StateGraphResult result;
        if (bytes.size() > kMaxBehaviorGraphBytes) { result.error = "the file is too big to be a state graph"; return result; }

        rapidjson::Document document;
        document.Parse<rapidjson::kParseValidateEncodingFlag>(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (document.HasParseError()) {
            result.error = std::string("invalid JSON: ") + rapidjson::GetParseError_En(document.GetParseError());
            return result;
        }

        const rapidjson::Value* version = Member(document, "version");
        if (!version || !version->IsInt() || version->GetInt() != 1) { result.error = "unsupported version (expected 1)"; return result; }

        auto graph = std::make_unique<StateGraph>();

        if (const rapidjson::Value* variables = Member(document, "variables")) {
            if (!BehaviorJson::ReadVariables(*variables, graph->m_variables, result.error)) return result;
        }

        // States first: arrows refer to them by position.
        const rapidjson::Value* states = Member(document, "states");
        if (!states || !states->IsArray()) { result.error = "the file has no \"states\" list"; return result; }
        for (const rapidjson::Value& entry : states->GetArray()) {
            GraphState state;
            if (!ReadState(entry, state, result.error)) return result;
            graph->m_states.push_back(std::move(state));
        }

        const int stateCount = static_cast<int>(graph->m_states.size());
        if (const rapidjson::Value* transitions = Member(document, "transitions")) {
            if (!transitions->IsArray()) { result.error = "\"transitions\" is not a list"; return result; }
            for (const rapidjson::Value& entry : transitions->GetArray()) {
                StateTransition transition;
                if (!ReadTransition(entry, stateCount, transition, result.error)) return result;
                graph->m_transitions.push_back(std::move(transition));
            }
        }

        if (const rapidjson::Value* entry = Member(document, "entry"); entry && entry->IsInt()) graph->m_entryState = entry->GetInt();
        if (stateCount > 0 && (graph->m_entryState < 0 || graph->m_entryState >= stateCount)) {
            result.error = "the entry state " + std::to_string(graph->m_entryState) + " doesn't exist";
            return result;
        }

        result.ok    = true;
        result.graph = std::move(graph);
        return result;

    }   // DecodeStateGraph()

    std::string EncodeStateGraph(const StateGraph& graph) {

        rapidjson::Document document(rapidjson::kObjectType);
        auto& allocator = document.GetAllocator();

        rapidjson::Value variables;
        BehaviorJson::WriteVariables(graph.m_variables, variables, allocator);

        rapidjson::Value states(rapidjson::kArrayType);
        for (const GraphState& state : graph.m_states) {

            rapidjson::Value entry(rapidjson::kObjectType);
            rapidjson::Value tree;
            BehaviorJson::WriteTree(state.m_behavior, tree, allocator);

            entry.AddMember("name", rapidjson::Value(state.m_name.c_str(), allocator), allocator);
            entry.AddMember("singleNode", state.m_singleNode, allocator);
            entry.AddMember("editor", BehaviorJson::WriteVec2(state.m_editorPosition, allocator), allocator);
            entry.AddMember("tree", tree, allocator);
            states.PushBack(entry, allocator);

        }

        rapidjson::Value transitions(rapidjson::kArrayType);
        for (const StateTransition& transition : graph.m_transitions) {

            rapidjson::Value conditions(rapidjson::kArrayType);
            for (const auto& condition : transition.m_conditions) {

                if (!condition) continue;
                const NodeDescription& description = condition->Description();
                rapidjson::Value entry(rapidjson::kObjectType);
                entry.AddMember("type", rapidjson::Value(description.id.data(), static_cast<rapidjson::SizeType>(description.id.size()), allocator), allocator);

                // reflect takes a non-const object only because rttr::instance
                // also writes; nothing is changed here, so the cast is safe.
                if (const ConditionTypeEntry* registered = ConditionRegistry::Instance().Find(description.id)) {
                    rapidjson::Value fields;
                    Reflection::ToJsonValue(registered->reflect(const_cast<TransitionCondition&>(*condition)), fields, allocator);
                    entry.AddMember("fields", fields, allocator);
                }
                conditions.PushBack(entry, allocator);

            }

            rapidjson::Value entry(rapidjson::kObjectType);
            entry.AddMember("from", transition.m_from, allocator);
            entry.AddMember("to", transition.m_to, allocator);
            entry.AddMember("conditions", conditions, allocator);
            transitions.PushBack(entry, allocator);

        }

        document.AddMember("version", 1, allocator);
        document.AddMember("entry", graph.m_entryState, allocator);
        document.AddMember("variables", variables, allocator);
        document.AddMember("states", states, allocator);
        document.AddMember("transitions", transitions, allocator);

        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        document.Accept(writer);
        return buffer.GetString();

    }   // EncodeStateGraph()

}   // NAMESPACE CRUCIBLE
