/*************************************************************************
* \file:         NavGoalTracker.h
* \author:       Jean David Vidal Cruzado
* \par:          Email: jeandavidvidal.c@digipen.edu
* \date          17 November 2026
* \version       1.0
* \brief
*                NavGoalTracker: this frame's goals (the players) and the
*                rule for when the flow field is rebuilt towards them.
*
* \details
*                Split out of ECSNavigationSystem (build step 3). It never
*                sees the World: the system finds the players each frame
*                and hands them over with Add(). Entity is only kept as a
*                handle, for "which player is nearest" (Arrive).
*
*                The field is rebuilt only when it is due - no field built
*                for this bake yet, a player joined or left, or any player
*                walked m_rebuildDistance (flat, XZ) since the last
*                rebuild. With no player the field is left as it was:
*                frozen at the last-known spot ("search"), or empty.
*
*                Not exported (no ENGINE_API), like NavGrid: only the engine
*                uses it.
*
* \history
*    -v1.0       first version (moved out of ECSNavigationSystem's
*                CollectGoals, NearestGoalEntity and UpdateField, no
*                behaviour change)
*
**************************************************************************/
#pragma once

// STL
#include <vector>

// Crucible engine
#include "ECS/Core/ECSCore.h"   // Entity
#include "Math/Math.h"
#include "Navigation/FlowField.h"

namespace Crucible {

    class NavGoalTracker {

    public:

        // Starts a new frame's list (memory reused).
        void ClearGoals();

        // One player: where it is, and which entity (same index).
        void Add(const Math::vec3& position, Entity entity);

        // This frame's player positions: the field's goals, and where agents
        // head when the field has no arrow for them.
        const std::vector<Math::vec3>& Goals() const { return m_goals; }

        // The player nearest `from` (flat distance), or kNullEntity if there is none.
        Entity NearestEntity(const Math::vec3& from) const;

        /*!
            \brief
            Rebuilds `field` towards the goals if it is due (see the file's
            details). Does nothing with no goals.

            \param rebuildDistance
            How far (m, flat) a player walks before the field is rebuilt.

            \param floodRadius
            Passed to FlowField::Rebuild.
        */
        void UpdateField(FlowField& field, float rebuildDistance, float floodRadius);

        // Forgets where the goals were at the last rebuild, so the next
        // UpdateField rebuilds at once (a new bake, Stop, the surface gone).
        void ForgetLastBuild() { m_goalsAtBuild.clear(); }

    private:

        std::vector<Math::vec3> m_goals;         // this frame's player positions (reused every frame)
        std::vector<Entity>     m_goalEntities;  // the same players, same order - for Arrive(nearest player)
        std::vector<Math::vec3> m_goalsAtBuild;  // where they were at the last rebuild; empty = rebuild as soon as there is a player

    };  // NAVGOALTRACKER CLASS

}   // NAMESPACE CRUCIBLE
