/*************************************************************************
* \file:         BehaviorTreeEdit.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The behaviour tree editing operations.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "BehaviorTreeEdit.h"

// Crucible engine
#include "AI/NodeRegistry.h"   // NodeDescription (names in the refusal messages)

namespace Crucible::BehaviorTreeEdit {

    namespace {

        void CollectFrom(BehaviorNode* node, BehaviorNode* parent, int index, bool attached, std::vector<NodeInfo>& out) {

            out.push_back({ node, parent, index, attached });
            for (std::size_t i = 0; i < node->ChildCount(); ++i) CollectFrom(node->ChildAt(i), node, static_cast<int>(i), attached, out);

        }   // CollectFrom()

        // Takes `node` out of wherever it lives: the root slot, the detached
        // list, or its parent. Null if it isn't in this graph.
        std::unique_ptr<BehaviorNode> Take(BehaviorGraph& graph, BehaviorNode* node) {

            if (graph.Root() == node) return graph.ReleaseRoot();

            auto& detached = graph.Detached();
            for (auto it = detached.begin(); it != detached.end(); ++it) {
                if (it->get() != node) continue;
                std::unique_ptr<BehaviorNode> taken = std::move(*it);
                detached.erase(it);
                return taken;
            }

            BehaviorNode* parent = ParentOf(graph, node);
            return parent ? parent->DetachChild(node) : nullptr;

        }   // Take()

    }   // ANONYMOUS NAMESPACE

    void Collect(BehaviorGraph& graph, std::vector<NodeInfo>& out) {

        out.clear();
        if (graph.Root()) CollectFrom(graph.Root(), nullptr, 0, true, out);
        for (const auto& detached : graph.Detached()) CollectFrom(detached.get(), nullptr, 0, false, out);

    }   // Collect()

    BehaviorNode* ParentOf(BehaviorGraph& graph, const BehaviorNode* node) {

        std::vector<NodeInfo> nodes;
        Collect(graph, nodes);
        for (const NodeInfo& info : nodes) {
            if (info.node == node) return info.parent;
        }
        return nullptr;

    }   // ParentOf()

    bool IsInSubtree(const BehaviorNode* top, const BehaviorNode* node) {

        if (!top) return false;
        if (top == node) return true;
        for (std::size_t i = 0; i < top->ChildCount(); ++i) {
            if (IsInSubtree(top->ChildAt(i), node)) return true;
        }
        return false;

    }   // IsInSubtree()

    BehaviorNode* AddDetached(BehaviorGraph& graph, std::unique_ptr<BehaviorNode> node) {

        BehaviorNode* raw = node.get();
        if (node) graph.Detached().push_back(std::move(node));
        return raw;

    }   // AddDetached()

    ConnectResult Connect(BehaviorGraph& graph, BehaviorNode* parent, BehaviorNode* child, std::string& reason) {

        if (!parent || !child || parent == child) { reason = "A node can't connect to itself"; return ConnectResult::Refused; }
        if (child == graph.Root())                { reason = "The root can't be another node's child"; return ConnectResult::Refused; }
        if (IsInSubtree(child, parent))           { reason = "That would make a loop - the target is above this node"; return ConnectResult::Refused; }
        if (parent->MaxChildren() == 0) {
            reason = std::string(parent->Description().name) + " can't have children";
            return ConnectResult::Refused;
        }
        if (ParentOf(graph, child) == parent) return ConnectResult::Connected;   // already its child: nothing to do

        // Full (a Modifier with its one child): the caller asks for a Composite
        // to go in between - Unity's Branch Options.
        if (parent->ChildCount() >= parent->MaxChildren()) return ConnectResult::NeedsBranch;

        parent->AddChild(Take(graph, child));
        SortAll(graph);
        return ConnectResult::Connected;

    }   // Connect()

    bool InsertBranch(BehaviorGraph& graph, BehaviorNode* parent, BehaviorNode* child, std::unique_ptr<BehaviorNode> composite) {

        if (!parent || !child || !composite || parent->ChildCount() != 1 || IsInSubtree(child, parent)) return false;

        // The new child first: it may be somewhere under the parent's current
        // child, and must be out before that subtree is moved.
        std::unique_ptr<BehaviorNode> newChild = Take(graph, child);
        if (!newChild) return false;
        std::unique_ptr<BehaviorNode> oldChild = parent->DetachChild(parent->ChildAt(0));

        // The composite sits between the parent and the two children it now holds.
        composite->m_editorPosition = { (oldChild->m_editorPosition.x + newChild->m_editorPosition.x) * 0.5f,
                                        (parent->m_editorPosition.y + oldChild->m_editorPosition.y) * 0.5f };
        composite->AddChild(std::move(oldChild));
        composite->AddChild(std::move(newChild));
        parent->AddChild(std::move(composite));
        SortAll(graph);
        return true;

    }   // InsertBranch()

    void Disconnect(BehaviorGraph& graph, BehaviorNode* child) {

        if (!ParentOf(graph, child)) return;    // the root or already detached
        AddDetached(graph, Take(graph, child));

    }   // Disconnect()

    void Delete(BehaviorGraph& graph, BehaviorNode* node) {

        std::unique_ptr<BehaviorNode> taken = Take(graph, node);
        if (!taken) return;
        while (taken->ChildCount() > 0) AddDetached(graph, taken->DetachChild(taken->ChildAt(0)));
        // `taken` is destroyed here, without its children.

    }   // Delete()

    BehaviorNode* Duplicate(BehaviorGraph& graph, const BehaviorNode& node, const Math::vec2& offset) {

        std::unique_ptr<BehaviorNode> copy = node.Clone();   // the whole subtree, settings and all
        Offset(*copy, offset, true);
        return AddDetached(graph, std::move(copy));

    }   // Duplicate()

    bool MakeRoot(BehaviorGraph& graph, BehaviorNode* node) {

        if (!node || graph.Root() == node || ParentOf(graph, node)) return false;   // only a detached top can become the root
        std::unique_ptr<BehaviorNode> newRoot = Take(graph, node);
        std::unique_ptr<BehaviorNode> oldRoot = graph.ReleaseRoot();
        graph.SetRoot(std::move(newRoot));
        if (oldRoot) AddDetached(graph, std::move(oldRoot));
        return true;

    }   // MakeRoot()

    void SortAll(BehaviorGraph& graph) {

        std::vector<NodeInfo> nodes;
        Collect(graph, nodes);
        for (const NodeInfo& info : nodes) info.node->SortChildrenByEditorX();   // does nothing on non-Composites

    }   // SortAll()

    void Offset(BehaviorNode& node, const Math::vec2& delta, bool withSubtree) {

        node.m_editorPosition += delta;
        if (!withSubtree) return;
        for (std::size_t i = 0; i < node.ChildCount(); ++i) Offset(*node.ChildAt(i), delta, true);

    }   // Offset()

}   // NAMESPACE CRUCIBLE::BEHAVIORTREEEDIT
