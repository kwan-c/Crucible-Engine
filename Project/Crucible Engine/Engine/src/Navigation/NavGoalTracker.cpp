/*************************************************************************
* \file:         NavGoalTracker.cpp
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          17 November 2026
* \version       1.1
* \brief
*                The goals (players) and when the flow field is rebuilt.
*
* \history
*    -v1.0       first version (moved out of ECSNavigationSystem, no
*                behaviour change)
*    -v1.1       IsGoal
*
**************************************************************************/
#include "NavGoalTracker.h"

namespace Crucible {

    void NavGoalTracker::ClearGoals() {

        m_goals.clear();
        m_goalEntities.clear();

    }   // NavGoalTracker::ClearGoals()

    void NavGoalTracker::Add(const Math::vec3& position, Entity entity) {

        m_goals.push_back(position);
        m_goalEntities.push_back(entity);   // same index as its position

    }   // NavGoalTracker::Add()

    Entity NavGoalTracker::NearestEntity(const Math::vec3& from) const {

        Entity nearest   = kNullEntity;
        float  nearestSq = 0.0f;
        for (std::size_t i = 0; i < m_goals.size(); ++i) {
            const float dx = m_goals[i].x - from.x;
            const float dz = m_goals[i].z - from.z;
            const float sq = dx * dx + dz * dz;
            if (nearest == kNullEntity || sq < nearestSq) { nearest = m_goalEntities[i]; nearestSq = sq; }
        }
        return nearest;

    }   // NavGoalTracker::NearestEntity()

    bool NavGoalTracker::IsGoal(Entity entity) const {

        for (const Entity goal : m_goalEntities) {
            if (goal == entity) return true;
        }
        return false;

    }   // NavGoalTracker::IsGoal()

    /*!
        \details
        No player this frame (none in the scene, or the player died or
        despawned): nothing is rebuilt, so the last field stays as it was,
        frozen and still pointing at where the player was last. That is the
        "search" behaviour - enemies head for the last-known spot instead of
        stopping dead. With no field ever built there is simply nothing to
        follow, and enemies wander.
    */
    void NavGoalTracker::UpdateField(FlowField& field, float rebuildDistance, float floodRadius) {

        if (m_goals.empty()) return;

        // Due: no field built for this bake yet, a player joined or left, or
        // any player walked far enough. Measured flat (XZ) - jumping on the
        // spot doesn't change which cell is the goal, so it shouldn't rebuild.
        bool due = m_goalsAtBuild.size() != m_goals.size();
        const float rebuildSq = rebuildDistance * rebuildDistance;
        for (std::size_t i = 0; !due && i < m_goals.size(); ++i) {
            const float dx = m_goals[i].x - m_goalsAtBuild[i].x;
            const float dz = m_goals[i].z - m_goalsAtBuild[i].z;
            due = dx * dx + dz * dz >= rebuildSq;
        }
        if (!due) return;

        // Recorded even if the rebuild fails (a player sealed inside walls), so
        // a failure isn't retried every frame - only after they move again.
        field.Rebuild(m_goals, floodRadius);
        m_goalsAtBuild = m_goals;

    }   // NavGoalTracker::UpdateField()

}   // NAMESPACE CRUCIBLE
