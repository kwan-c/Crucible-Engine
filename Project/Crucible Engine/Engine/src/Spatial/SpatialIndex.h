#pragma once
/*************************************************************************
* \file:         SpatialIndex.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          1 October 2026
* \version       1.0
* \brief
*                The engine's spatial index: "which objects are in this
*                frustum / hit by this ray / inside this box?", built to stay
*                cheap with tens of thousands of objects, most of them moving.
*
* \details
*                A hybrid of two structures, chosen per object automatically:
*
*                  - SETTLED objects (level geometry, props, idle enemies) live
*                    in a DynamicAABBTree (a BVH). Queries skip whole branches.
*                    Keeping it updated costs nothing while they stay put.
*                  - ACTIVE objects (anything that keeps leaving its fat box -
*                    walking enemies, projectiles) live in a flat array that
*                    queries test one by one. Updating them is a single store.
*
*                Why: measured on 20,000 enemies walking at 5 m/s, keeping a
*                BVH up to date cost 3.5-5.5 ms per frame (reinserting the
*                ~7-25% of leaves that left their fat boxes), while testing
*                all 20,000 boxes against a frustum costs ~0.5 ms. A tree pays
*                off only for objects that don't move.
*
*                Policy (all automatic; callers just call Update()):
*                  - New objects start in the tree.
*                  - A tree object that has to be reinserted twice within
*                    kActiveWindow frames is moved to the active array.
*                  - An active object that hasn't moved for kSettleFrames
*                    frames goes back into the tree (checked in EndFrame()).
*
*                Handles are stable for an object's lifetime; queries report
*                handles, GetUserData() maps them back to the caller's ids.
*                Not thread-safe.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include <cstdint>
#include <vector>

#include "EngineExport.h"
#include "Spatial/Bounds.h"
#include "Spatial/DynamicAABBTree.h"

namespace Crucible {

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL members in an exported class; Engine.dll and its clients share one toolchain
#endif

    /*************************************************************************
     \brief  See the file header.
     *************************************************************************/
    class ENGINE_API SpatialIndex {
    public:
        using Handle = std::uint32_t;
        static constexpr Handle kInvalid = 0xFFFFFFFFu;

        static constexpr std::uint32_t kActiveWindow = 30;    //!< frames: 2 reinserts within this -> active
        static constexpr std::uint32_t kSettleFrames = 120;   //!< frames without moving -> back into the tree

        /*********************************************************************
         \brief  Creates an empty index.
         \param[in] treeMargin Fat-box margin for settled objects (see
                               DynamicAABBTree).
         *********************************************************************/
        explicit SpatialIndex(float treeMargin = 0.25f) : m_tree(treeMargin) {}

        /*********************************************************************
         \brief  Adds an object with its world-space box.
         \return Its handle.
         *********************************************************************/
        Handle Insert(const AABB& box, std::uint64_t userData);

        /*********************************************************************
         \brief  Sets an object's new box (call when it moves; calling with
                 an unchanged box is a cheap no-op).
         *********************************************************************/
        void   Update(Handle handle, const AABB& box);

        /*********************************************************************
         \brief  Removes an object; its handle may be reused later.
         *********************************************************************/
        void   Remove(Handle handle);

        /*********************************************************************
         \brief  Once per frame: advances the frame counter and moves active
                 objects that have stopped moving back into the tree.
         *********************************************************************/
        void   EndFrame();

        std::uint64_t GetUserData(Handle h) const { return m_entries[h].userData; }   //!< as given to Insert
        const AABB&   GetBounds(Handle h)   const { return m_entries[h].box; }        //!< tight box
        bool          IsActive(Handle h)    const { return m_entries[h].treeId == DynamicAABBTree::kNull; }   //!< in the brute-force array?

        std::uint32_t GetCount()        const { return m_count; }                                     //!< live objects
        std::uint32_t GetActiveCount()  const { return static_cast<std::uint32_t>(m_active.size()); } //!< in the flat array
        std::uint32_t GetSettledCount() const { return m_count - GetActiveCount(); }                  //!< in the tree
        const DynamicAABBTree& GetTree() const { return m_tree; }                                     //!< for debug drawing

        /*********************************************************************
         \brief  visit(handle) for every object whose TIGHT box is (or may be)
                 inside the frustum.
         *********************************************************************/
        template <typename Visit>
        void QueryFrustum(const Frustum& frustum, Visit&& visit) const {
            m_tree.QueryFrustum(frustum, [&](int32_t leaf) {
                const Handle h = static_cast<Handle>(m_tree.GetUserData(leaf));
                if (frustum.Intersects(m_entries[h].box)) { visit(h); }   // leaf boxes are fat: recheck tight
            });
            for (size_t i = 0; i < m_active.size(); ++i) {
                if (frustum.Intersects(m_activeBoxes[i])) { visit(m_active[i]); }
            }
        }

        /*********************************************************************
         \brief  visit(handle) for every object whose tight box overlaps box.
         *********************************************************************/
        template <typename Visit>
        void QueryAABB(const AABB& box, Visit&& visit) const {
            m_tree.QueryAABB(box, [&](int32_t leaf) {
                const Handle h = static_cast<Handle>(m_tree.GetUserData(leaf));
                if (m_entries[h].box.Overlaps(box)) { visit(h); }
            });
            for (size_t i = 0; i < m_active.size(); ++i) {
                if (m_activeBoxes[i].Overlaps(box)) { visit(m_active[i]); }
            }
        }

        /*********************************************************************
         \brief  Ray query. hit(handle, tEnterTightBox) -> float: return the
                 t of an exact hit to clip farther candidates, tMax to keep
                 going, or 0 to stop. Candidates come in no particular order.
         *********************************************************************/
        template <typename Hit>
        void RayCast(const Ray& ray, float tMax, Hit&& hit) const {
            bool stop = false;
            m_tree.RayCast(ray, tMax, [&](int32_t leaf, float) {
                const Handle h = static_cast<Handle>(m_tree.GetUserData(leaf));
                float t;
                if (!ray.Intersects(m_entries[h].box, tMax, t)) { return tMax; }
                const float clip = hit(h, t);
                if (clip <= 0.0f) { stop = true; return 0.0f; }
                tMax = std::min(tMax, clip);
                return tMax;
            });
            if (stop) { return; }
            for (size_t i = 0; i < m_active.size(); ++i) {
                float t;
                if (!ray.Intersects(m_activeBoxes[i], tMax, t)) { continue; }
                const float clip = hit(m_active[i], t);
                if (clip <= 0.0f) { return; }
                tMax = std::min(tMax, clip);
            }
        }

    private:
        struct Entry {
            AABB          box;
            std::uint64_t userData = 0;
            int32_t       treeId = DynamicAABBTree::kNull;   // in the tree, or...
            std::uint32_t activeIndex = 0xFFFFFFFFu;         // ...in m_active / m_activeBoxes at this index
            std::uint32_t lastReinsertFrame = 0;             // when the tree last had to reinsert it
            std::uint32_t lastMoveFrame = 0;
            bool          alive = false;
        };

        void MakeActive(Handle h);
        void MakeSettled(Handle h);

        DynamicAABBTree     m_tree;
        std::vector<Entry>  m_entries;
        std::vector<Handle> m_free;
        std::vector<Handle> m_active;        // dense: handles of active objects
        std::vector<AABB>   m_activeBoxes;   // dense, parallel to m_active (contiguous for the brute-force loop)
        std::uint32_t       m_count = 0;
        std::uint32_t       m_frame = 1;
    };

#ifdef _MSC_VER
#pragma warning(pop)
#endif

}   // NAMESPACE CRUCIBLE
