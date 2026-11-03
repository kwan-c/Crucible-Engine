/*************************************************************************
* \file:         BehaviorGraphIO.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The .behaviorgraph JSON format: writing a tree as a flat
*                node list, and rebuilding the tree from one with every
*                check a hand-edited file needs.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "BehaviorGraphIO.h"

// STL
#include <array>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// rapidjson
#include <rapidjson/error/en.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

// Crucible engine
#include "AI/NodeRegistry.h"
#include "Reflection/Serialization.h"   // ToJsonValue / FromJsonValue: fields through RTTR

namespace Crucible {

    namespace {

        // The blackboard kinds by name, in BlackboardValue's variant order -
        // the name is what the file stores, so a reorder there can't corrupt files.
        constexpr std::array<std::string_view, 6> kKindNames{ "Entity", "Vector3", "Float", "Int", "Bool", "EntityList" };

        rapidjson::Value Text(std::string_view text, BehaviorJson::Allocator& allocator) {

            // Copied into the document: a string_view's memory isn't the document's to keep.
            return rapidjson::Value(text.data(), static_cast<rapidjson::SizeType>(text.size()), allocator);

        }   // Text()

        // A member of an object, or null if `in` isn't an object or hasn't got it.
        const rapidjson::Value* Member(const rapidjson::Value& in, const char* name) {

            if (!in.IsObject()) return nullptr;
            const auto found = in.FindMember(name);
            return found == in.MemberEnd() ? nullptr : &found->value;

        }   // Member()

        /*
            Rebuilds a tree from the file's flat node list. Every node is made
            first; then the links are checked (each child exists, has one parent,
            and isn't a loop) and the tree is put together from the parentless
            nodes down. A file can be edited by hand, so nothing is trusted.
        */
        class TreeBuilder {

        public:

            explicit TreeBuilder(std::string& error) : m_error(error) {}

            bool Read(const rapidjson::Value& in, BehaviorGraph& tree) {

                const rapidjson::Value* nodes = Member(in, "nodes");
                const rapidjson::Value* root  = Member(in, "root");
                if (!nodes || !nodes->IsArray()) return Fail("the tree has no \"nodes\" list");
                if (!root || !root->IsInt())     return Fail("the tree has no \"root\" id");

                for (const rapidjson::Value& node : nodes->GetArray()) {
                    if (!MakeNode(node)) return false;
                }
                if (!CheckLinks()) return false;

                // The root, then every other parentless node as a detached
                // subtree (editor work in progress, never run).
                const int rootId = root->GetInt();
                if (rootId >= 0) {
                    if (!m_made.contains(rootId))   return Fail("the root id " + std::to_string(rootId) + " is not a node");
                    if (m_parentOf.contains(rootId)) return Fail("the root has a parent");
                    std::unique_ptr<BehaviorNode> built = Build(rootId);
                    if (!built) return false;
                    tree.SetRoot(std::move(built));
                }
                for (const int id : m_order) {
                    if (id == rootId || m_parentOf.contains(id) || m_built.contains(id)) continue;
                    std::unique_ptr<BehaviorNode> built = Build(id);
                    if (!built) return false;
                    tree.Detached().push_back(std::move(built));
                }

                // Anything still unbuilt was only reachable from itself: a loop.
                if (m_built.size() != m_order.size()) return Fail("some nodes form a loop (a tree can't loop back on itself)");
                return true;

            }   // Read()

        private:

            bool Fail(std::string message) { m_error = std::move(message); return false; }

            bool MakeNode(const rapidjson::Value& in) {

                const rapidjson::Value* id   = Member(in, "id");
                const rapidjson::Value* type = Member(in, "type");
                if (!id || !id->IsInt())        return Fail("a node has no \"id\"");
                if (!type || !type->IsString()) return Fail("node " + std::to_string(id->GetInt()) + " has no \"type\"");
                if (m_made.contains(id->GetInt())) return Fail("two nodes share the id " + std::to_string(id->GetInt()));

                const NodeTypeEntry* entry = NodeRegistry::Instance().Find(type->GetString());
                if (!entry) return Fail(std::string("unknown node type \"") + type->GetString() + "\"");

                std::unique_ptr<BehaviorNode> node = entry->create();
                if (const rapidjson::Value* editor = Member(in, "editor")) node->m_editorPosition = BehaviorJson::ReadVec2(*editor);
                if (const rapidjson::Value* fields = Member(in, "fields")) {
                    if (!Reflection::FromJsonValue(*fields, entry->reflect(*node))) {
                        return Fail("the fields of node " + std::to_string(id->GetInt()) + " (" + type->GetString() + ") could not be read");
                    }
                }

                // Children are only noted here; they may come later in the list.
                std::vector<int>& children = m_childrenOf[id->GetInt()];
                if (const rapidjson::Value* list = Member(in, "children")) {
                    if (!list->IsArray()) return Fail("node " + std::to_string(id->GetInt()) + "'s \"children\" is not a list");
                    for (const rapidjson::Value& child : list->GetArray()) {
                        if (!child.IsInt()) return Fail("node " + std::to_string(id->GetInt()) + " has a child that isn't an id");
                        children.push_back(child.GetInt());
                    }
                }

                m_order.push_back(id->GetInt());
                m_made.emplace(id->GetInt(), std::move(node));
                return true;

            }   // MakeNode()

            bool CheckLinks() {

                for (const int parent : m_order) {
                    for (const int child : m_childrenOf[parent]) {
                        if (!m_made.contains(child))       return Fail("node " + std::to_string(parent) + " has a missing child " + std::to_string(child));
                        if (child == parent)               return Fail("node " + std::to_string(parent) + " is its own child");
                        if (!m_parentOf.emplace(child, parent).second) {
                            return Fail("node " + std::to_string(child) + " has two parents (a tree node has one)");
                        }
                    }
                }
                return true;

            }   // CheckLinks()

            // Takes node `id` out of m_made and attaches its children, recursively.
            std::unique_ptr<BehaviorNode> Build(int id) {

                std::unique_ptr<BehaviorNode> node = std::move(m_made[id]);
                m_built.insert(id);
                for (const int child : m_childrenOf[id]) {
                    std::unique_ptr<BehaviorNode> built = Build(child);
                    if (!built) return nullptr;
                    if (!node->AddChild(std::move(built))) {
                        Fail("node " + std::to_string(id) + " (" + std::string(node->Description().name) + ") can't have that many children");
                        return nullptr;
                    }
                }
                return node;

            }   // Build()

            std::string&                                           m_error;
            std::vector<int>                                       m_order;        // ids in file order
            std::unordered_map<int, std::unique_ptr<BehaviorNode>> m_made;
            std::unordered_map<int, std::vector<int>>              m_childrenOf;
            std::unordered_map<int, int>                           m_parentOf;     // child id -> parent id
            std::unordered_set<int>                                m_built;

        };  // TREEBUILDER CLASS

    }   // ANONYMOUS NAMESPACE

    namespace BehaviorJson {

        rapidjson::Value WriteVec2(const Math::vec2& v, Allocator& allocator) {

            rapidjson::Value out(rapidjson::kArrayType);
            out.PushBack(v.x, allocator).PushBack(v.y, allocator);
            return out;

        }   // BehaviorJson::WriteVec2()

        Math::vec2 ReadVec2(const rapidjson::Value& in) {

            if (!in.IsArray() || in.Size() != 2 || !in[0].IsNumber() || !in[1].IsNumber()) return Math::vec2(0.0f);
            return { in[0].GetFloat(), in[1].GetFloat() };

        }   // BehaviorJson::ReadVec2()

        void WriteTree(const BehaviorGraph& tree, rapidjson::Value& out, Allocator& allocator) {

            // Number every node: the root's tree first, depth-first so parents
            // come before their children, then each detached subtree.
            std::vector<const BehaviorNode*>                 order;
            std::unordered_map<const BehaviorNode*, int>     ids;
            const auto collect = [&](const auto& self, const BehaviorNode* node) -> void {
                ids[node] = static_cast<int>(order.size());
                order.push_back(node);
                for (std::size_t i = 0; i < node->ChildCount(); ++i) self(self, node->ChildAt(i));
            };
            if (tree.Root()) collect(collect, tree.Root());
            for (const auto& detached : tree.Detached()) collect(collect, detached.get());

            rapidjson::Value nodes(rapidjson::kArrayType);
            for (const BehaviorNode* node : order) {

                rapidjson::Value entry(rapidjson::kObjectType);
                entry.AddMember("id", ids[node], allocator);
                entry.AddMember("type", Text(node->Description().id, allocator), allocator);
                entry.AddMember("editor", WriteVec2(node->m_editorPosition, allocator), allocator);

                // The fields through RTTR. reflect takes a non-const node only
                // because rttr::instance is also used for writing; nothing is
                // changed here, so the const_cast is safe.
                if (const NodeTypeEntry* type = NodeRegistry::Instance().Find(node->Description().id)) {
                    rapidjson::Value fields;
                    Reflection::ToJsonValue(type->reflect(const_cast<BehaviorNode&>(*node)), fields, allocator);
                    entry.AddMember("fields", fields, allocator);
                }

                rapidjson::Value children(rapidjson::kArrayType);
                for (std::size_t i = 0; i < node->ChildCount(); ++i) children.PushBack(ids[node->ChildAt(i)], allocator);
                entry.AddMember("children", children, allocator);

                nodes.PushBack(entry, allocator);

            }

            out.SetObject();
            out.AddMember("root", tree.Root() ? 0 : -1, allocator);   // the root is always numbered first
            out.AddMember("nodes", nodes, allocator);

        }   // BehaviorJson::WriteTree()

        bool ReadTree(const rapidjson::Value& in, BehaviorGraph& tree, std::string& error) {

            TreeBuilder builder(error);
            return builder.Read(in, tree);

        }   // BehaviorJson::ReadTree()

        void WriteVariables(const Blackboard& variables, rapidjson::Value& out, Allocator& allocator) {

            out.SetArray();
            for (const BlackboardVariable& variable : variables.Variables()) {

                rapidjson::Value entry(rapidjson::kObjectType);
                entry.AddMember("name", Text(variable.name, allocator), allocator);
                entry.AddMember("type", Text(kKindNames[variable.value.index()], allocator), allocator);

                // Entities (and lists of them) differ from scene to scene, so a
                // graph can't store one: only their name and kind are saved, and
                // each enemy's own values come from its overrides at Play.
                if (const Math::vec3* v = std::get_if<Math::vec3>(&variable.value)) {
                    rapidjson::Value value(rapidjson::kArrayType);
                    value.PushBack(v->x, allocator).PushBack(v->y, allocator).PushBack(v->z, allocator);
                    entry.AddMember("value", value, allocator);
                }
                else if (const float* f = std::get_if<float>(&variable.value)) entry.AddMember("value", *f, allocator);
                else if (const int*   i = std::get_if<int>(&variable.value))   entry.AddMember("value", *i, allocator);
                else if (const bool*  b = std::get_if<bool>(&variable.value))  entry.AddMember("value", *b, allocator);

                out.PushBack(entry, allocator);

            }

        }   // BehaviorJson::WriteVariables()

        bool ReadVariables(const rapidjson::Value& in, Blackboard& variables, std::string& error) {

            if (!in.IsArray()) { error = "\"variables\" is not a list"; return false; }

            for (const rapidjson::Value& entry : in.GetArray()) {

                const rapidjson::Value* name  = Member(entry, "name");
                const rapidjson::Value* type  = Member(entry, "type");
                const rapidjson::Value* value = Member(entry, "value");
                if (!name || !name->IsString() || !type || !type->IsString()) { error = "a variable has no \"name\" or \"type\""; return false; }

                const std::string_view kind = type->GetString();
                if      (kind == "Entity")     variables.Set(name->GetString(), Entity{});
                else if (kind == "EntityList") variables.Set(name->GetString(), std::vector<Entity>{});
                else if (kind == "Vector3") {
                    Math::vec3 v(0.0f);
                    if (value && value->IsArray() && value->Size() == 3 && (*value)[0].IsNumber() && (*value)[1].IsNumber() && (*value)[2].IsNumber()) {
                        v = { (*value)[0].GetFloat(), (*value)[1].GetFloat(), (*value)[2].GetFloat() };
                    }
                    variables.Set(name->GetString(), v);
                }
                else if (kind == "Float") variables.Set(name->GetString(), value && value->IsNumber() ? value->GetFloat() : 0.0f);
                else if (kind == "Int")   variables.Set(name->GetString(), value && value->IsInt() ? value->GetInt() : 0);
                else if (kind == "Bool")  variables.Set(name->GetString(), value && value->IsBool() ? value->GetBool() : false);
                else { error = "variable \"" + std::string(name->GetString()) + "\" has an unknown type \"" + std::string(kind) + "\""; return false; }

            }
            return true;

        }   // BehaviorJson::ReadVariables()

    }   // NAMESPACE BEHAVIORJSON

    BehaviorGraphResult DecodeBehaviorGraph(std::span<const unsigned char> bytes) {

        BehaviorGraphResult result;
        if (bytes.size() > kMaxBehaviorGraphBytes) { result.error = "the file is too big to be a behaviour graph"; return result; }

        rapidjson::Document document;
        document.Parse<rapidjson::kParseValidateEncodingFlag>(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (document.HasParseError()) {
            result.error = std::string("invalid JSON: ") + rapidjson::GetParseError_En(document.GetParseError());
            return result;
        }

        const rapidjson::Value* version = Member(document, "version");
        if (!version || !version->IsInt() || version->GetInt() != 1) { result.error = "unsupported version (expected 1)"; return result; }

        auto graph = std::make_unique<BehaviorGraph>();
        if (const rapidjson::Value* variables = Member(document, "variables")) {
            if (!BehaviorJson::ReadVariables(*variables, graph->Variables(), result.error)) return result;
        }
        const rapidjson::Value* tree = Member(document, "tree");
        if (!tree) { result.error = "the file has no \"tree\""; return result; }
        if (!BehaviorJson::ReadTree(*tree, *graph, result.error)) return result;

        result.ok    = true;
        result.graph = std::move(graph);
        return result;

    }   // DecodeBehaviorGraph()

    std::string EncodeBehaviorGraph(const BehaviorGraph& graph) {

        rapidjson::Document document(rapidjson::kObjectType);
        auto& allocator = document.GetAllocator();

        rapidjson::Value variables, tree;
        BehaviorJson::WriteVariables(graph.Variables(), variables, allocator);
        BehaviorJson::WriteTree(graph, tree, allocator);

        document.AddMember("version", 1, allocator);
        document.AddMember("variables", variables, allocator);
        document.AddMember("tree", tree, allocator);

        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        document.Accept(writer);
        return buffer.GetString();

    }   // EncodeBehaviorGraph()

}   // NAMESPACE CRUCIBLE
