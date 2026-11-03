/*************************************************************************
* \file:         BehaviorGraph.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                Making an enemy's copy of a graph, and running it.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "BehaviorGraph.h"

namespace Crucible {

    std::unique_ptr<BehaviorGraphInstance> BehaviorGraph::Instantiate(World& world, Entity self) const {

        // new, not make_unique: the constructor is private (only a graph makes
        // instances), and make_unique can't reach a private constructor.
        std::unique_ptr<BehaviorGraphInstance> instance(new BehaviorGraphInstance());

        // The variables first, then "Self": every graph can rely on it, so
        // nodes never need the enemy passed to them separately.
        instance->m_variables = m_variables;
        instance->m_variables.Set("Self", self);
        instance->m_context = BehaviorContext{ &world, self, &instance->m_variables };

        // Cloned nodes, all pointed at this enemy's context.
        if (m_root) {
            instance->m_root = m_root->Clone();
            instance->m_root->Bind(&instance->m_context);
        }
        return instance;

    }   // BehaviorGraph::Instantiate()

    BehaviorStatus BehaviorGraphInstance::Tick(float deltaTime) {

        if (!m_root) return BehaviorStatus::Failure;
        return m_root->Tick(deltaTime);

    }   // BehaviorGraphInstance::Tick()

    void BehaviorGraphInstance::Abort() {

        if (m_root) m_root->Abort();

    }   // BehaviorGraphInstance::Abort()

}   // NAMESPACE CRUCIBLE
