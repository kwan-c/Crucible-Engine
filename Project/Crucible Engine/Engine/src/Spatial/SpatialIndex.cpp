/*************************************************************************
* \file:         SpatialIndex.cpp
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          1 October 2026
* \version       1.1
* \brief
*                See SpatialIndex.h for the settled/active policy and why.
*
* \history
*    -v1.0       initial implementation
*    -v1.1       m_activeUserData, kept parallel to m_active
*
**************************************************************************/

#include "SpatialIndex.h"

#include <cassert>

namespace Crucible {

    SpatialIndex::Handle SpatialIndex::Insert(const AABB& box, std::uint64_t userData) {

        Handle h;
        if (!m_free.empty()) { h = m_free.back(); m_free.pop_back(); }
        else { h = static_cast<Handle>(m_entries.size()); m_entries.emplace_back(); }

        Entry& e = m_entries[h];
        e = Entry{};
        e.box = box;
        e.userData = userData;
        e.alive = true;
        e.lastMoveFrame = m_frame;
        e.treeId = m_tree.CreateProxy(box, h);
        ++m_count;
        return h;
    }

    void SpatialIndex::Update(Handle h, const AABB& box) {

        assert(h < m_entries.size() && m_entries[h].alive);
        Entry& e = m_entries[h];
        if (e.box.min == box.min && e.box.max == box.max) { return; }

        e.box = box;
        e.lastMoveFrame = m_frame;

        if (e.treeId == DynamicAABBTree::kNull) {
            m_activeBoxes[e.activeIndex] = box;   // active: a single store
            return;
        }

        if (m_tree.MoveProxy(e.treeId, box)) {
            // Reinserted. Twice in a short window means it is genuinely on the move: stop paying
            // for tree maintenance and test it brute force instead.
            if (e.lastReinsertFrame != 0 && m_frame - e.lastReinsertFrame < kActiveWindow) { MakeActive(h); }
            else { e.lastReinsertFrame = m_frame; }
        }
    }

    void SpatialIndex::Remove(Handle h) {

        assert(h < m_entries.size() && m_entries[h].alive);
        Entry& e = m_entries[h];
        if (e.treeId != DynamicAABBTree::kNull) {
            m_tree.DestroyProxy(e.treeId);
        } else {
            // Swap-and-pop out of the dense arrays, fixing the moved entry's index.
            const std::uint32_t i = e.activeIndex;
            const Handle last = m_active.back();
            m_active[i] = last;
            m_activeBoxes[i] = m_activeBoxes.back();
            m_activeUserData[i] = m_activeUserData.back();
            m_entries[last].activeIndex = i;
            m_active.pop_back();
            m_activeBoxes.pop_back();
            m_activeUserData.pop_back();
        }
        e = Entry{};
        m_free.push_back(h);
        --m_count;
    }

    void SpatialIndex::MakeActive(Handle h) {

        Entry& e = m_entries[h];
        m_tree.DestroyProxy(e.treeId);
        e.treeId = DynamicAABBTree::kNull;
        e.activeIndex = static_cast<std::uint32_t>(m_active.size());
        m_active.push_back(h);
        m_activeBoxes.push_back(e.box);
        m_activeUserData.push_back(e.userData);
    }

    void SpatialIndex::MakeSettled(Handle h) {

        Entry& e = m_entries[h];
        const std::uint32_t i = e.activeIndex;
        const Handle last = m_active.back();
        m_active[i] = last;
        m_activeBoxes[i] = m_activeBoxes.back();
        m_activeUserData[i] = m_activeUserData.back();
        m_entries[last].activeIndex = i;
        m_active.pop_back();
        m_activeBoxes.pop_back();
        m_activeUserData.pop_back();

        e.activeIndex = 0xFFFFFFFFu;
        e.lastReinsertFrame = 0;
        e.treeId = m_tree.CreateProxy(e.box, h);
    }

    void SpatialIndex::EndFrame() {

        ++m_frame;
        // Iterate backwards: MakeSettled swap-pops, which only disturbs indices we've already seen.
        for (size_t i = m_active.size(); i-- > 0;) {
            const Handle h = m_active[i];
            if (m_frame - m_entries[h].lastMoveFrame > kSettleFrames) { MakeSettled(h); }
        }
    }

}   // NAMESPACE CRUCIBLE
