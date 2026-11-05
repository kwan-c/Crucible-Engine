/*************************************************************************
* \file:         BehaviorNode.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          5 November 2026
* \version       2.0
* \brief
*                BehaviorNode: one node of a behaviour tree (or one arrow
*                condition) as DATA - its type id, its field values and its
*                children. What the editor edits and the files store.
*
* \details
*                What a node DOES is written in C# (Crucible.AI.Node, and the
*                built-ins in ManagedScripts/Runtime/AI/Nodes). At Play,
*                NodeHostBridge turns this data into C# objects for each
*                enemy, and C# runs them. So there is one class here for
*                every kind of node: "wait" and "sequence" differ only in
*                their type id, fields and children.
*
*                Field values are kept as TEXT, in the format the C# side
*                reads (NodeHost.cs): "1.5", "true", "0,0,5". That keeps a
*                node's values intact even when its C# class isn't known
*                (C# not running, a script renamed) - nothing is lost by
*                loading and saving such a graph.
*
*                Header-only on purpose: the Editor (another module) uses all
*                of it without anything having to be exported.
*
* \history
*    -v1.0       C++ node classes with Tick / OnStart / OnUpdate / OnEnd
*    -v2.0       plain data; the behaviour moved to C#
*
**************************************************************************/
#pragma once

// STL
#include <algorithm>    // std::stable_sort, std::find_if
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Crucible engine
#include "AI/NodeCatalog.h"
#include "Math/Math.h"          // vec2, for the editor position

namespace Crucible {

    // How a node did this frame. Same numbers as C#'s Node.Status: keep them in step.
    enum class BehaviorStatus : std::uint8_t {

        Running = 0,    // not finished: tick me again next frame
        Success = 1,    // finished, and it worked
        Failure = 2     // finished, and it didn't (or couldn't)

    };  // BEHAVIORSTATUS ENUM

    // How a field's value is written in the file. Only matters for saving: the
    // text itself is what C# reads.
    enum class FieldKind : std::uint8_t { None, Number, Integer, Bool, Text, Vector3 };

    // The kind a field of this catalog type ("float", "Vector3"...) is saved as.
    inline FieldKind FieldKindOf(std::string_view catalogType) {

        if (catalogType == "float")   return FieldKind::Number;
        if (catalogType == "int")     return FieldKind::Integer;
        if (catalogType == "bool")    return FieldKind::Bool;
        if (catalogType == "Vector3") return FieldKind::Vector3;
        if (catalogType == "string" || catalogType == "enum") return FieldKind::Text;
        if (catalogType == "BehaviorGraph") return FieldKind::Text;   // an asset id as text: too big for a JSON number to keep exactly
        return FieldKind::None;   // Entity: only ever a link, never a stored value

    }   // FieldKindOf()

    // One field's value on one node.
    struct NodeField {

        std::string name;               // the file key: "seconds"
        std::string text;               // its own value, as text
        std::string link;               // a graph variable it reads instead; "" = its own value
        FieldKind   kind     = FieldKind::None;
        bool        linkable = false;   // saved as { "value", "variable" } (a BlackboardVariable in C#)

    };  // NODEFIELD STRUCT

    // A node's name, category and description, for the editor.
    struct NodeDescription {

        std::string_view id;
        std::string_view name;
        std::string_view category;
        std::string_view description;

    };  // NODEDESCRIPTION STRUCT

    class BehaviorNode {

    public:

        explicit BehaviorNode(std::string type = {}) : m_type(std::move(type)) {}

        BehaviorNode(const BehaviorNode&)            = delete;   // Clone() copies the children too
        BehaviorNode& operator=(const BehaviorNode&) = delete;

        // --- What it is ------------------------------------------------------ //

        const std::string& TypeId() const { return m_type; }

        // Its C# type's description, or null if C# doesn't (yet) know the id.
        const NodeTypeInfo* Info() const { return NodeCatalog::Instance().Find(m_type); }

        // Name, category and description - the id itself for an unknown type.
        // The views point into the catalog: use them right away.
        NodeDescription Description() const {
            if (const NodeTypeInfo* info = Info()) return { info->id, info->name, info->category, info->description };
            return { m_type, m_type, "Unknown", "No C# node has this id - its script was renamed or deleted, or C# isn't running." };
        }

        // 0 (action, condition), 1 (modifier) or no limit (composite). An
        // unknown type takes any number, so a graph's shape survives until its
        // script is back.
        std::size_t MaxChildren() const {
            const NodeTypeInfo* info = Info();
            return info ? info->MaxChildren() : std::numeric_limits<std::size_t>::max();
        }

        // --- Children --------------------------------------------------------- //

        std::size_t   ChildCount() const { return m_children.size(); }
        BehaviorNode* ChildAt(std::size_t index) const { return index < m_children.size() ? m_children[index].get() : nullptr; }

        // Adds a child at the end, if this kind of node can take another (the editor).
        bool AddChild(std::unique_ptr<BehaviorNode> child) {
            if (!child || m_children.size() >= MaxChildren()) return false;
            m_children.push_back(std::move(child));
            return true;
        }

        // Adds a child whatever the catalog says: loading a file, which may
        // be read before C# has said what any type is.
        void AppendChild(std::unique_ptr<BehaviorNode> child) { if (child) m_children.push_back(std::move(child)); }

        // Takes `child` back out; null if it isn't one of this node's children.
        std::unique_ptr<BehaviorNode> DetachChild(BehaviorNode* child) {
            const auto found = std::find_if(m_children.begin(), m_children.end(), [&](const auto& owned) { return owned.get() == child; });
            if (found == m_children.end()) return nullptr;
            std::unique_ptr<BehaviorNode> taken = std::move(*found);
            m_children.erase(found);
            return taken;
        }

        // Left-to-right order of their editor position: the order a composite
        // runs them in, as in Unity Behavior. Only composites have an order.
        void SortChildrenByEditorX() {
            const NodeTypeInfo* info = Info();
            if (!info || info->kind != "composite") return;
            std::stable_sort(m_children.begin(), m_children.end(),
                [](const auto& a, const auto& b) { return a->m_editorPosition.x < b->m_editorPosition.x; });
        }

        // A deep copy: type, fields, position and copies of all the children.
        std::unique_ptr<BehaviorNode> Clone() const {
            auto copy = std::make_unique<BehaviorNode>(m_type);
            copy->m_fields         = m_fields;
            copy->m_editorPosition = m_editorPosition;
            for (const auto& child : m_children) copy->m_children.push_back(child->Clone());
            return copy;
        }

        // --- Fields ----------------------------------------------------------- //

        std::vector<NodeField>&       Fields()       { return m_fields; }
        const std::vector<NodeField>& Fields() const { return m_fields; }

        NodeField* FindField(std::string_view fieldName) {
            for (NodeField& field : m_fields) if (field.name == fieldName) return &field;
            return nullptr;
        }
        const NodeField* FindField(std::string_view fieldName) const {
            return const_cast<BehaviorNode*>(this)->FindField(fieldName);
        }

        // The node's value for a catalog field, added with the C# default if
        // it has none yet (a field added to the script after the graph was saved).
        NodeField& FieldFor(const NodeFieldInfo& info) {
            if (NodeField* field = FindField(info.name)) return *field;
            m_fields.push_back({ info.name, info.value, info.link, FieldKindOf(info.type), info.linkable });
            return m_fields.back();
        }

        // Where the node sits in the graph editor. Saved, never used at runtime.
        Math::vec2 m_editorPosition{ 0.0f };

    private:

        std::string                                m_type;
        std::vector<NodeField>                     m_fields;
        std::vector<std::unique_ptr<BehaviorNode>> m_children;

    };  // BEHAVIORNODE CLASS

    // A new node of `type` with every field at its C# default.
    inline std::unique_ptr<BehaviorNode> MakeNode(const NodeTypeInfo& type) {

        auto node = std::make_unique<BehaviorNode>(type.id);
        for (const NodeFieldInfo& field : type.fields) node->FieldFor(field);
        return node;

    }   // MakeNode()

    // The same by id - even one C# hasn't listed (yet): then it has no fields
    // stored, and C# uses its defaults when it runs. How "start" is made for a
    // new graph whether or not C# is running.
    inline std::unique_ptr<BehaviorNode> MakeNode(std::string_view typeId) {

        if (const NodeTypeInfo* type = NodeCatalog::Instance().Find(typeId)) return MakeNode(*type);
        return std::make_unique<BehaviorNode>(std::string(typeId));

    }   // MakeNode()

}   // NAMESPACE CRUCIBLE
