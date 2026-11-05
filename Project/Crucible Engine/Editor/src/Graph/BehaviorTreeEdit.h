/*************************************************************************
* \file:         BehaviorTreeEdit.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                The editing operations on a behaviour tree - connect,
*                disconnect, delete, duplicate, set as root - with the
*                rules that keep it a valid tree.
*
* \details
*                Plain functions on a BehaviorGraph, with no ImGui in them,
*                so the same rules serve the behaviour graph editor now and
*                a state's embedded tree in the state graph editor next.
*
*                A graph has one root (On Start) plus "detached" nodes:
*                anything placed on the canvas but not connected under the
*                root yet. Detached nodes are saved but never run.
*
*                New nodes are always made through the engine's node
*                registry: the node classes live in Engine.dll, and only
*                its "make one" functions can construct them.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <memory>
#include <string>
#include <vector>

// Crucible engine
#include "AI/BehaviorGraph.h"
#include "Math/Math.h"

namespace Crucible::BehaviorTreeEdit {

    // One node as the editor sees it this frame.
    struct NodeInfo {

        BehaviorNode* node     = nullptr;
        BehaviorNode* parent   = nullptr;   // null for the root and for detached tops
        int           index    = 0;         // its place among its parent's children (the order it runs in)
        bool          attached = false;     // under the root - it will run

    };  // NODEINFO STRUCT

    // Every node: the root's tree first, then each detached subtree - always
    // parents before their children. Clears `out` first.
    void Collect(BehaviorGraph& graph, std::vector<NodeInfo>& out);

    // The node's parent, or null (the root, or the top of a detached subtree).
    BehaviorNode* ParentOf(BehaviorGraph& graph, const BehaviorNode* node);

    // True if `node` is `top` or anywhere under it.
    bool IsInSubtree(const BehaviorNode* top, const BehaviorNode* node);

    // Puts a new node (and anything under it) on the canvas, detached.
    BehaviorNode* AddDetached(BehaviorGraph& graph, std::unique_ptr<BehaviorNode> node);

    enum class ConnectResult {

        Connected,      // done
        NeedsBranch,    // the parent can't take another child: ask which Composite to put in between
        Refused         // not allowed; `reason` says why

    };  // CONNECTRESULT ENUM

    /*!
        \brief
        Makes `child` (and everything under it) a child of `parent`, moving it
        from wherever it was.

        \details
        Refused: connecting a node to itself or into its own subtree (a tree
        can't loop), making the root a child, or giving an Action children.
        NeedsBranch: a Modifier (On Start) that already has its one child.
    */
    ConnectResult Connect(BehaviorGraph& graph, BehaviorNode* parent, BehaviorNode* child, std::string& reason);

    /*!
        \brief
        Branch Options: puts `composite` (Sequence, Try In Order...) under
        `parent` in place of parent's current child, with that child and
        `child` as the composite's two children.
    */
    bool InsertBranch(BehaviorGraph& graph, BehaviorNode* parent, BehaviorNode* child, std::unique_ptr<BehaviorNode> composite);

    // Cuts `child` (and its subtree) loose from its parent: it becomes detached.
    void Disconnect(BehaviorGraph& graph, BehaviorNode* child);

    // Removes `node`. Its children survive as detached subtrees (Unity keeps
    // them too), so deleting a Sequence doesn't silently delete what was in it.
    void Delete(BehaviorGraph& graph, BehaviorNode* node);

    // A copy of `node` and its subtree, detached, shifted by `offset`.
    BehaviorNode* Duplicate(BehaviorGraph& graph, const BehaviorNode& node, const Math::vec2& offset);

    // Makes a detached top node the root; the old root (if any) becomes detached.
    bool MakeRoot(BehaviorGraph& graph, BehaviorNode* node);

    // Every Composite's children in left-to-right order (the order they run in).
    void SortAll(BehaviorGraph& graph);

    // Moves `node`'s editor position (and its subtree's, if `withSubtree`).
    void Offset(BehaviorNode& node, const Math::vec2& delta, bool withSubtree);

}   // NAMESPACE CRUCIBLE::BEHAVIORTREEEDIT
