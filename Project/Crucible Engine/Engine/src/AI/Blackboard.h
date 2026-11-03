/*************************************************************************
* \file:         Blackboard.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                Blackboard: an enemy's named memory - Self, Target,
*                PatrolPoints, DetectRadius - that its graph's nodes read
*                and write.
*
* \details
*                A graph declares its variables with default values. Every
*                enemy running the graph gets its own copy (its private
*                blackboard), so one enemy's Target is not another's.
*
*                Variables are found by NAME. That keeps nodes simple (a
*                node field just says "use the variable called Target"),
*                and lets shared blackboards - one copy for many enemies -
*                slot in later behind the same lookup.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Crucible engine
#include "ECS/Core/ECSCore.h"   // Entity
#include "Math/Math.h"

namespace Crucible {

    // The kinds of value a variable can hold. The order is the variant's index,
    // so new kinds go at the END - saved graphs will store it.
    using BlackboardValue = std::variant<
        Entity,                 // an enemy, the player, a waypoint entity
        Math::vec3,             // a point: home, last known position
        float,                  // a radius, a time
        int,                    // a counter, an index
        bool,                   // a flag
        std::vector<Entity>     // a list of entities: patrol points
    >;

    // One named variable and its value.
    struct BlackboardVariable {

        std::string     name;
        BlackboardValue value;

    };  // BLACKBOARDVARIABLE STRUCT

    class Blackboard {

    public:

        // Sets a variable, adding it if it doesn't exist. Setting an existing
        // one to a different kind of value changes its kind - it is the
        // graph's job to declare each variable with the kind it means.
        void Set(std::string_view name, BlackboardValue value);

        bool Has(std::string_view name) const { return Find(name) != nullptr; }

        /*!
            \brief
            The value of a variable, if it exists AND holds a T.

            \return
            Null if there is no variable by that name, or it holds a different
            kind of value - so a node asking for the float "Target" (an
            Entity) gets nothing, rather than garbage.
        */
        template <typename T>
        const T* Get(std::string_view name) const {
            const BlackboardVariable* variable = Find(name);
            return variable ? std::get_if<T>(&variable->value) : nullptr;
        }

        // Every variable, in the order they were declared (the editor lists them so).
        const std::vector<BlackboardVariable>& Variables() const { return m_variables; }

    private:

        const BlackboardVariable* Find(std::string_view name) const;

        // A vector searched by name, not a map: a graph has a handful of
        // variables, and a short linear search over contiguous memory beats a
        // hash at that size.
        std::vector<BlackboardVariable> m_variables;

    };  // BLACKBOARD CLASS

}   // NAMESPACE CRUCIBLE
