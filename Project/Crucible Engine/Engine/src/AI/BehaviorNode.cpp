/*************************************************************************
* \file:         BehaviorNode.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                Running a node (Tick, Abort), and how Modifier and
*                Composite hold, copy and bind their children.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "BehaviorNode.h"

// STL
#include <algorithm>    // std::stable_sort

namespace Crucible {

    BehaviorStatus BehaviorNode::Tick(float deltaTime) {

        if (!m_running) {

            // A fresh run: start it, and if it didn't finish on the spot,
            // give it its first update this same frame (see the header).
            m_running = true;
            m_status  = OnStart();
            if (m_status == BehaviorStatus::Running) m_status = OnUpdate(deltaTime);

        } else {

            m_status = OnUpdate(deltaTime);

        }

        // Finished either way: end the run, so the next Tick starts a new one.
        if (m_status != BehaviorStatus::Running) {
            m_running = false;
            OnEnd();
        }
        return m_status;

    }   // BehaviorNode::Tick()

    void BehaviorNode::Abort() {

        if (!m_running) return;

        // Children first: they started after this node, so they end before it,
        // and its OnEnd sees them already stopped.
        AbortChildren();
        m_running = false;
        m_status  = BehaviorStatus::Failure;   // cut short counts as not having worked
        OnEnd();

    }   // BehaviorNode::Abort()

    // --- Modifier ---------------------------------------------------------- //

    Modifier::Modifier(const Modifier& other) : BehaviorNode(other) {

        if (other.m_child) m_child = other.m_child->Clone();

    }   // Modifier::Modifier()

    void Modifier::Bind(BehaviorContext* context) {

        BehaviorNode::Bind(context);
        if (m_child) m_child->Bind(context);

    }   // Modifier::Bind()

    bool Modifier::AddChild(std::unique_ptr<BehaviorNode> child) {

        if (m_child || !child) return false;    // one child only
        m_child = std::move(child);
        return true;

    }   // Modifier::AddChild()

    std::unique_ptr<BehaviorNode> Modifier::DetachChild(BehaviorNode* child) {

        if (!child || m_child.get() != child) return nullptr;
        return std::move(m_child);

    }   // Modifier::DetachChild()

    // --- Composite --------------------------------------------------------- //

    Composite::Composite(const Composite& other) : BehaviorNode(other) {

        m_children.reserve(other.m_children.size());
        for (const auto& child : other.m_children) m_children.push_back(child->Clone());

    }   // Composite::Composite()

    void Composite::Bind(BehaviorContext* context) {

        BehaviorNode::Bind(context);
        for (const auto& child : m_children) child->Bind(context);

    }   // Composite::Bind()

    bool Composite::AddChild(std::unique_ptr<BehaviorNode> child) {

        if (!child) return false;
        m_children.push_back(std::move(child));
        return true;

    }   // Composite::AddChild()

    std::unique_ptr<BehaviorNode> Composite::DetachChild(BehaviorNode* child) {

        for (auto it = m_children.begin(); it != m_children.end(); ++it) {
            if (it->get() != child) continue;
            std::unique_ptr<BehaviorNode> detached = std::move(*it);
            m_children.erase(it);
            return detached;
        }
        return nullptr;

    }   // Composite::DetachChild()

    void Composite::SortChildrenByEditorX() {

        // stable_sort: two children at exactly the same x keep their current
        // order instead of swapping back and forth from one frame to the next.
        std::stable_sort(m_children.begin(), m_children.end(),
            [](const std::unique_ptr<BehaviorNode>& a, const std::unique_ptr<BehaviorNode>& b) { return a->m_editorPosition.x < b->m_editorPosition.x; });

    }   // Composite::SortChildrenByEditorX()

    void Composite::AbortChildren() {

        // Abort does nothing to a child that isn't running, so this is safe to
        // call on all of them without tracking which one was current.
        for (const auto& child : m_children) child->Abort();

    }   // Composite::AbortChildren()

}   // NAMESPACE CRUCIBLE
