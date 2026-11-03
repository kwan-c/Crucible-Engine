/*************************************************************************
* \file:         NodeValue.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                NodeValue<T>: a node field that is either a value typed
*                into the node, or the name of a blackboard variable - the
*                link icon on Unity Behavior's node fields.
*
* \details
*                "Move To (10, 0, 10)" and "Move To HomePosition" are the
*                same node with the same field; only where the value comes
*                from changes. Nodes never check which: they call Get().
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <string>

// Crucible engine
#include "AI/Blackboard.h"
#include "AI/BehaviorNode.h"   // BehaviorContext

namespace Crucible {

    template <typename T>
    struct NodeValue {

        T           value{};    // used when no variable is named
        std::string variable;   // a blackboard variable's name; empty = use `value`

        NodeValue() = default;
        NodeValue(T typed) : value(std::move(typed)) {}   // not explicit: `NodeValue<float> seconds = 1.0f;` reads naturally

        // A field that starts out linked to a variable: `NodeValue<Entity>::Variable("Target")`.
        static NodeValue Variable(std::string name) { NodeValue linked; linked.variable = std::move(name); return linked; }

        /*!
            \brief
            The value to use this frame: the blackboard variable if one is
            named and holds a T, otherwise the typed-in value.

            \details
            A named variable that is missing or of the wrong kind falls back
            to the typed value instead of failing, so a half-set-up graph
            still runs. Read it every time rather than caching it: the
            variable may change while the node runs (Target moves).
        */
        T Get(const BehaviorContext& context) const {
            if (!variable.empty() && context.blackboard) {
                if (const T* found = context.blackboard->Get<T>(variable)) return *found;
            }
            return value;
        }

        bool UsesVariable() const { return !variable.empty(); }

    };  // NODEVALUE STRUCT

}   // NAMESPACE CRUCIBLE
