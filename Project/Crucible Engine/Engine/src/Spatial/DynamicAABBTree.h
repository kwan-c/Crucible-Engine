#pragma once
/*************************************************************************
* \file:         DynamicAABBTree.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          1 October 2026
* \version       1.0
* \brief
*                A dynamic bounding volume hierarchy (BVH) of axis-aligned
*                boxes: insert, move and remove objects in O(log n), and ask
*                "what is inside this frustum / hit by this ray / touching
*                this box?" without testing every object.
*
* \details
*                The same design as Box2D's b2DynamicTree (also Bullet's
*                btDbvt and the idea behind Jolt's broadphase):
*
*                  - Leaves hold one object's box, ENLARGED by a margin (a
*                    "fat" box). MoveProxy() does nothing while the object's
*                    real box still fits inside its fat box, so an enemy
*                    shuffling around costs nothing most frames; only when it
*                    leaves the fat box is its leaf removed and reinserted.
*                  - Inner nodes hold the union of their two children.
*                  - Insertion walks down choosing the child whose box grows
*                    least (surface-area heuristic), then rotations rebalance
*                    the path back up (AVL-style), so the tree stays shallow
*                    whatever order objects arrive in.
*
*                Queries are templates taking a callback, so there is no
*                virtual call or std::function per leaf:
*
*                    tree.QueryFrustum(frustum, [&](int32_t id) { visible.push_back(tree.GetUserData(id)); });
*                    tree.RayCast(ray, maxT, [&](int32_t id, float tEnter) { return refine(id); });
*
*                Callbacks see FAT boxes - re-test the object's tight box if
*                exactness matters (the Renderer does).
*
*                Not thread-safe; one tree per owner.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include <cstdint>
#include <vector>

#include "EngineExport.h"
#include "Spatial/Bounds.h"

namespace Crucible {

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL members in an exported class; Engine.dll and its clients share one toolchain
#endif

    /*************************************************************************
     \brief  See the file header.
     *************************************************************************/
    class ENGINE_API DynamicAABBTree {
    public:
        static constexpr int32_t kNull = -1;   //!< "no node"

        /*********************************************************************
         \brief  Creates an empty tree.
         \param[in] margin How far each leaf's fat box extends past the
                           object's real box, in world units. Bigger = fewer
                           reinsertions for moving objects, looser culling.
         *********************************************************************/
        explicit DynamicAABBTree(float margin = 0.25f);

        /*********************************************************************
         \brief  Adds an object.
         \param[in] box      Its world-space box (tight - the margin is added
                             here).
         \param[in] userData Anything the owner wants back from queries.
         \return The proxy id, for MoveProxy/DestroyProxy/GetUserData.
         *********************************************************************/
        int32_t  CreateProxy(const AABB& box, std::uint64_t userData);

        /*********************************************************************
         \brief  Removes an object. The id may be reused later.
         *********************************************************************/
        void     DestroyProxy(int32_t proxyId);

        /*********************************************************************
         \brief  Updates an object's box.
         \return true if the leaf had to be reinserted (the new box left
                 the fat box), false if nothing in the tree changed.
         *********************************************************************/
        bool     MoveProxy(int32_t proxyId, const AABB& box);

        std::uint64_t GetUserData(int32_t proxyId) const { return m_nodes[proxyId].userData; }   //!< as given to CreateProxy
        const AABB&   GetFatAABB(int32_t proxyId)  const { return m_nodes[proxyId].box; }        //!< the enlarged leaf box

        int32_t  GetProxyCount() const { return m_proxyCount; }   //!< live objects
        int32_t  GetHeight()     const;                           //!< 0 = empty or one leaf
        float    GetMargin()     const { return m_margin; }

        /*********************************************************************
         \brief  Debug/test check of every invariant (links, heights, child
                 boxes inside parents, balance). O(n).
         \return true if the tree is consistent.
         *********************************************************************/
        bool     Validate() const;

        /*********************************************************************
         \brief  Calls visit(proxyId) for every leaf whose fat box overlaps
                 `box`.
         *********************************************************************/
        template <typename Visit>
        void QueryAABB(const AABB& box, Visit&& visit) const {
            if (m_root == kNull) { return; }
            std::vector<int32_t>& stack = Stack();
            stack.clear();
            stack.push_back(m_root);
            while (!stack.empty()) {
                const int32_t id = stack.back(); stack.pop_back();
                const Node& n = m_nodes[id];
                if (!n.box.Overlaps(box)) { continue; }
                if (n.IsLeaf()) { visit(id); }
                else { stack.push_back(n.child1); stack.push_back(n.child2); }
            }
        }

        /*********************************************************************
         \brief  Calls visit(proxyId) for every leaf whose fat box is (or may
                 be) inside the frustum. A subtree wholly inside is reported
                 without testing its leaves.
         *********************************************************************/
        template <typename Visit>
        void QueryFrustum(const Frustum& frustum, Visit&& visit) const {
            if (m_root == kNull) { return; }
            std::vector<int32_t>& stack = Stack();
            stack.clear();
            stack.push_back(m_root);
            while (!stack.empty()) {
                const int32_t id = stack.back(); stack.pop_back();
                const Node& n = m_nodes[id];
                const Frustum::Result r = frustum.Classify(n.box);
                if (r == Frustum::Result::Outside) { continue; }
                if (r == Frustum::Result::Inside) { VisitLeaves(id, visit); continue; }
                if (n.IsLeaf()) { visit(id); }
                else { stack.push_back(n.child1); stack.push_back(n.child2); }
            }
        }

        /*********************************************************************
         \brief  Walks the leaves whose fat box the ray enters before tMax.
         \param[in] ray  The ray.
         \param[in] tMax Farthest t to consider.
         \param[in] hit  hit(proxyId, tEnterFatBox) -> float. Return a
                         smaller value to clip the search (e.g. the t of an
                         exact hit you found), tMax to keep going unchanged,
                         or 0 to stop.
         *********************************************************************/
        template <typename Hit>
        void RayCast(const Ray& ray, float tMax, Hit&& hit) const {
            if (m_root == kNull) { return; }
            std::vector<int32_t>& stack = Stack();
            stack.clear();
            stack.push_back(m_root);
            while (!stack.empty()) {
                const int32_t id = stack.back(); stack.pop_back();
                const Node& n = m_nodes[id];
                float tEnter;
                if (!ray.Intersects(n.box, tMax, tEnter)) { continue; }
                if (n.IsLeaf()) {
                    const float clip = hit(id, tEnter);
                    if (clip <= 0.0f) { return; }
                    tMax = std::min(tMax, clip);
                } else {
                    stack.push_back(n.child1);
                    stack.push_back(n.child2);
                }
            }
        }

        /*********************************************************************
         \brief  Calls visit(nodeId, box, isLeaf, depth) for every node -
                 for drawing the hierarchy with DebugDraw.
         *********************************************************************/
        template <typename Visit>
        void ForEachNode(Visit&& visit) const {
            if (m_root == kNull) { return; }
            std::vector<std::pair<int32_t, int32_t>> stack{ { m_root, 0 } };
            while (!stack.empty()) {
                const auto [id, depth] = stack.back(); stack.pop_back();
                const Node& n = m_nodes[id];
                visit(id, n.box, n.IsLeaf(), depth);
                if (!n.IsLeaf()) { stack.push_back({ n.child1, depth + 1 }); stack.push_back({ n.child2, depth + 1 }); }
            }
        }

    private:
        struct Node {
            AABB          box;                  // fat box for leaves, union for inner nodes
            std::uint64_t userData = 0;
            int32_t       parent = kNull;       // doubles as the free-list "next" link
            int32_t       child1 = kNull;
            int32_t       child2 = kNull;
            int32_t       height = -1;          // leaf = 0, free = -1
            bool IsLeaf() const { return child1 == kNull; }
        };

        int32_t AllocateNode();
        void    FreeNode(int32_t id);
        void    InsertLeaf(int32_t leaf);
        void    RemoveLeaf(int32_t leaf);
        int32_t Balance(int32_t a);
        bool    ValidateNode(int32_t id, int32_t& outHeight) const;

        template <typename Visit>
        void VisitLeaves(int32_t start, Visit& visit) const {
            // Separate small stack: called from inside QueryFrustum, which is using Stack().
            std::vector<int32_t> stack{ start };
            while (!stack.empty()) {
                const int32_t id = stack.back(); stack.pop_back();
                const Node& n = m_nodes[id];
                if (n.IsLeaf()) { visit(id); }
                else { stack.push_back(n.child1); stack.push_back(n.child2); }
            }
        }

        // Reused traversal stack (avoids an allocation per query). Mutable: queries are const.
        std::vector<int32_t>& Stack() const { return m_stack; }

        std::vector<Node>            m_nodes;
        int32_t                      m_root = kNull;
        int32_t                      m_freeList = kNull;
        int32_t                      m_proxyCount = 0;
        float                        m_margin = 0.25f;
        mutable std::vector<int32_t> m_stack;
    };

#ifdef _MSC_VER
#pragma warning(pop)
#endif

}   // NAMESPACE CRUCIBLE
