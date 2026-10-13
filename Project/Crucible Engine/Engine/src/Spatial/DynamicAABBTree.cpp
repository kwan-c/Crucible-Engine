/*************************************************************************
* \file:         DynamicAABBTree.cpp
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          1 October 2026
* \version       1.0
* \brief
*                See DynamicAABBTree.h. Insertion/removal/balancing follow
*                Box2D v2.4's b2DynamicTree (MIT licence, Erin Catto),
*                extended to 3D with surface area as the cost metric.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include "DynamicAABBTree.h"

#include <cassert>
#include <cstdlib>

namespace Crucible {

    DynamicAABBTree::DynamicAABBTree(float margin) : m_margin{ margin > 0.0f ? margin : 0.0f } {
        m_nodes.reserve(256);
    }

    void DynamicAABBTree::Partition(uint32_t maxParts, std::vector<int32_t>& outNodes) const {

        outNodes.clear();
        if (m_root == kNull || maxParts == 0) { return; }
        outNodes.push_back(m_root);
        while (outNodes.size() < maxParts) {
            // Split the tallest inner node (the first of equals, so the result is always the same).
            size_t  best = outNodes.size();
            int32_t bestHeight = 0;
            for (size_t i = 0; i < outNodes.size(); ++i) {
                const Node& n = m_nodes[outNodes[i]];
                if (!n.IsLeaf() && n.height > bestHeight) { bestHeight = n.height; best = i; }
            }
            if (best == outNodes.size()) { break; }   // only leaves left
            const Node& n = m_nodes[outNodes[best]];
            outNodes[best] = n.child1;
            outNodes.push_back(n.child2);
        }

    }

    // ------------------------------------------------------------------------------------------------------------- //
    //  NODE POOL
    // ------------------------------------------------------------------------------------------------------------- //

    int32_t DynamicAABBTree::AllocateNode() {

        if (m_freeList == kNull) {
            m_nodes.emplace_back();
            m_nodes.back().height = 0;
            return static_cast<int32_t>(m_nodes.size() - 1);
        }
        const int32_t id = m_freeList;
        m_freeList = m_nodes[id].parent;
        m_nodes[id] = Node{};
        m_nodes[id].height = 0;
        return id;
    }

    void DynamicAABBTree::FreeNode(int32_t id) {

        m_nodes[id].parent = m_freeList;
        m_nodes[id].child1 = kNull;
        m_nodes[id].child2 = kNull;
        m_nodes[id].height = -1;
        m_freeList = id;
    }

    // ------------------------------------------------------------------------------------------------------------- //
    //  PROXIES
    // ------------------------------------------------------------------------------------------------------------- //

    int32_t DynamicAABBTree::CreateProxy(const AABB& box, std::uint64_t userData) {

        const int32_t id = AllocateNode();
        m_nodes[id].box = box.Expanded(m_margin);
        m_nodes[id].userData = userData;
        m_nodes[id].height = 0;
        InsertLeaf(id);
        ++m_proxyCount;
        return id;
    }

    void DynamicAABBTree::DestroyProxy(int32_t proxyId) {

        assert(proxyId >= 0 && proxyId < static_cast<int32_t>(m_nodes.size()) && m_nodes[proxyId].IsLeaf());
        RemoveLeaf(proxyId);
        FreeNode(proxyId);
        --m_proxyCount;
    }

    bool DynamicAABBTree::MoveProxy(int32_t proxyId, const AABB& box) {

        assert(proxyId >= 0 && proxyId < static_cast<int32_t>(m_nodes.size()) && m_nodes[proxyId].IsLeaf());

        const AABB fat = box.Expanded(m_margin);
        const AABB& current = m_nodes[proxyId].box;
        if (current.Contains(box)) {
            // Still inside its fat box - unless that box has become far too big for the object
            // (e.g. it shrank a lot), in which case refit so culling stays tight.
            const AABB huge = fat.Expanded(4.0f * m_margin);
            if (huge.Contains(current)) { return false; }
        }

        RemoveLeaf(proxyId);
        m_nodes[proxyId].box = fat;
        InsertLeaf(proxyId);
        return true;
    }

    int32_t DynamicAABBTree::GetHeight() const {
        return m_root == kNull ? 0 : m_nodes[m_root].height;
    }

    // ------------------------------------------------------------------------------------------------------------- //
    //  INSERT / REMOVE
    // ------------------------------------------------------------------------------------------------------------- //

    void DynamicAABBTree::InsertLeaf(int32_t leaf) {

        if (m_root == kNull) {
            m_root = leaf;
            m_nodes[leaf].parent = kNull;
            return;
        }

        // 1. Find the best sibling: walk down choosing the child whose box would grow the least,
        //    stopping when making a new parent here is cheaper than descending (surface-area cost).
        const AABB leafBox = m_nodes[leaf].box;
        int32_t index = m_root;
        while (!m_nodes[index].IsLeaf()) {
            const int32_t child1 = m_nodes[index].child1;
            const int32_t child2 = m_nodes[index].child2;

            const float area = m_nodes[index].box.SurfaceArea();
            const float combinedArea = AABB::Union(m_nodes[index].box, leafBox).SurfaceArea();

            const float cost = 2.0f * combinedArea;                       // new parent for (index, leaf)
            const float inheritanceCost = 2.0f * (combinedArea - area);   // pushing the leaf further down

            auto descendCost = [&](int32_t child) {
                const AABB u = AABB::Union(leafBox, m_nodes[child].box);
                return m_nodes[child].IsLeaf()
                    ? u.SurfaceArea() + inheritanceCost
                    : (u.SurfaceArea() - m_nodes[child].box.SurfaceArea()) + inheritanceCost;
            };
            const float cost1 = descendCost(child1);
            const float cost2 = descendCost(child2);

            if (cost < cost1 && cost < cost2) { break; }
            index = (cost1 < cost2) ? child1 : child2;
        }
        const int32_t sibling = index;

        // 2. Make a new parent for (sibling, leaf).
        const int32_t oldParent = m_nodes[sibling].parent;
        const int32_t newParent = AllocateNode();
        m_nodes[newParent].parent = oldParent;
        m_nodes[newParent].box = AABB::Union(leafBox, m_nodes[sibling].box);
        m_nodes[newParent].height = m_nodes[sibling].height + 1;
        m_nodes[newParent].child1 = sibling;
        m_nodes[newParent].child2 = leaf;
        m_nodes[sibling].parent = newParent;
        m_nodes[leaf].parent = newParent;

        if (oldParent != kNull) {
            if (m_nodes[oldParent].child1 == sibling) { m_nodes[oldParent].child1 = newParent; }
            else                                      { m_nodes[oldParent].child2 = newParent; }
        } else {
            m_root = newParent;
        }

        // 3. Walk back up refitting boxes and heights, rebalancing as we go.
        index = m_nodes[leaf].parent;
        while (index != kNull) {
            index = Balance(index);
            const int32_t child1 = m_nodes[index].child1;
            const int32_t child2 = m_nodes[index].child2;
            m_nodes[index].height = 1 + std::max(m_nodes[child1].height, m_nodes[child2].height);
            m_nodes[index].box = AABB::Union(m_nodes[child1].box, m_nodes[child2].box);
            index = m_nodes[index].parent;
        }
    }

    void DynamicAABBTree::RemoveLeaf(int32_t leaf) {

        if (leaf == m_root) {
            m_root = kNull;
            return;
        }

        const int32_t parent = m_nodes[leaf].parent;
        const int32_t grandParent = m_nodes[parent].parent;
        const int32_t sibling = (m_nodes[parent].child1 == leaf) ? m_nodes[parent].child2 : m_nodes[parent].child1;

        if (grandParent != kNull) {
            // Replace the parent with the sibling, then refit upwards.
            if (m_nodes[grandParent].child1 == parent) { m_nodes[grandParent].child1 = sibling; }
            else                                        { m_nodes[grandParent].child2 = sibling; }
            m_nodes[sibling].parent = grandParent;
            FreeNode(parent);

            int32_t index = grandParent;
            while (index != kNull) {
                index = Balance(index);
                const int32_t child1 = m_nodes[index].child1;
                const int32_t child2 = m_nodes[index].child2;
                m_nodes[index].box = AABB::Union(m_nodes[child1].box, m_nodes[child2].box);
                m_nodes[index].height = 1 + std::max(m_nodes[child1].height, m_nodes[child2].height);
                index = m_nodes[index].parent;
            }
        } else {
            m_root = sibling;
            m_nodes[sibling].parent = kNull;
            FreeNode(parent);
        }
        m_nodes[leaf].parent = kNull;
    }

    // ------------------------------------------------------------------------------------------------------------- //
    //  BALANCE (a tree rotation when one child is 2+ levels taller than the other)
    // ------------------------------------------------------------------------------------------------------------- //

    int32_t DynamicAABBTree::Balance(int32_t iA) {

        Node* A = &m_nodes[iA];
        if (A->IsLeaf() || A->height < 2) { return iA; }

        const int32_t iB = A->child1;
        const int32_t iC = A->child2;
        Node* B = &m_nodes[iB];
        Node* C = &m_nodes[iC];

        const int32_t balance = C->height - B->height;

        // Rotate C up.
        if (balance > 1) {
            const int32_t iF = C->child1;
            const int32_t iG = C->child2;
            Node* F = &m_nodes[iF];
            Node* G = &m_nodes[iG];

            C->child1 = iA;
            C->parent = A->parent;
            A->parent = iC;

            if (C->parent != kNull) {
                if (m_nodes[C->parent].child1 == iA) { m_nodes[C->parent].child1 = iC; }
                else                                 { m_nodes[C->parent].child2 = iC; }
            } else {
                m_root = iC;
            }

            if (F->height > G->height) {
                C->child2 = iF;
                A->child2 = iG;
                G->parent = iA;
                A->box = AABB::Union(B->box, G->box);
                C->box = AABB::Union(A->box, F->box);
                A->height = 1 + std::max(B->height, G->height);
                C->height = 1 + std::max(A->height, F->height);
            } else {
                C->child2 = iG;
                A->child2 = iF;
                F->parent = iA;
                A->box = AABB::Union(B->box, F->box);
                C->box = AABB::Union(A->box, G->box);
                A->height = 1 + std::max(B->height, F->height);
                C->height = 1 + std::max(A->height, G->height);
            }
            return iC;
        }

        // Rotate B up.
        if (balance < -1) {
            const int32_t iD = B->child1;
            const int32_t iE = B->child2;
            Node* D = &m_nodes[iD];
            Node* E = &m_nodes[iE];

            B->child1 = iA;
            B->parent = A->parent;
            A->parent = iB;

            if (B->parent != kNull) {
                if (m_nodes[B->parent].child1 == iA) { m_nodes[B->parent].child1 = iB; }
                else                                 { m_nodes[B->parent].child2 = iB; }
            } else {
                m_root = iB;
            }

            if (D->height > E->height) {
                B->child2 = iD;
                A->child1 = iE;
                E->parent = iA;
                A->box = AABB::Union(C->box, E->box);
                B->box = AABB::Union(A->box, D->box);
                A->height = 1 + std::max(C->height, E->height);
                B->height = 1 + std::max(A->height, D->height);
            } else {
                B->child2 = iE;
                A->child1 = iD;
                D->parent = iA;
                A->box = AABB::Union(C->box, D->box);
                B->box = AABB::Union(A->box, E->box);
                A->height = 1 + std::max(C->height, D->height);
                B->height = 1 + std::max(A->height, E->height);
            }
            return iB;
        }

        return iA;
    }

    // ------------------------------------------------------------------------------------------------------------- //
    //  VALIDATION
    // ------------------------------------------------------------------------------------------------------------- //

    bool DynamicAABBTree::ValidateNode(int32_t id, int32_t& outHeight) const {

        const Node& n = m_nodes[id];
        if (n.IsLeaf()) {
            if (n.child2 != kNull || n.height != 0) { return false; }
            outHeight = 0;
            return true;
        }
        if (n.child1 < 0 || n.child2 < 0) { return false; }
        if (m_nodes[n.child1].parent != id || m_nodes[n.child2].parent != id) { return false; }

        int32_t h1 = 0, h2 = 0;
        if (!ValidateNode(n.child1, h1) || !ValidateNode(n.child2, h2)) { return false; }
        if (n.height != 1 + std::max(h1, h2)) { return false; }
        if (!n.box.Contains(m_nodes[n.child1].box) || !n.box.Contains(m_nodes[n.child2].box)) { return false; }

        outHeight = n.height;
        return true;
    }

    bool DynamicAABBTree::Validate() const {

        if (m_root == kNull) { return m_proxyCount == 0; }
        if (m_nodes[m_root].parent != kNull) { return false; }

        int32_t height = 0;
        if (!ValidateNode(m_root, height)) { return false; }

        // Leaves reachable from the root == live proxies; free list + reachable == all nodes.
        int32_t leaves = 0, reachable = 0;
        ForEachNode([&](int32_t, const AABB&, bool leaf, int32_t) { ++reachable; if (leaf) { ++leaves; } });
        int32_t freeCount = 0;
        for (int32_t f = m_freeList; f != kNull; f = m_nodes[f].parent) { ++freeCount; }
        return leaves == m_proxyCount && reachable + freeCount == static_cast<int32_t>(m_nodes.size());
    }

}   // NAMESPACE CRUCIBLE
