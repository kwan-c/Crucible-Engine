/*************************************************************************
* \file:         NodeRegistry.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.1
* \brief
*                The one node registry and the one condition registry, in
*                Engine.dll.
*
* \details
*                Each Instance() is defined here, not in the header, so
*                there is exactly one of each for the engine and the
*                editor together (see NodeRegistry.h).
*
* \history
*    -v1.0       first version: Add/Find (moved into the header template in v1.1)
*    -v1.1       just the two exported Instance() definitions
*
**************************************************************************/
#include "NodeRegistry.h"

// Crucible engine
#include "AI/TransitionCondition.h"

namespace Crucible {

    template <>
    NodeRegistry& NodeRegistry::Instance() {

        static NodeRegistry registry;
        return registry;

    }   // NodeRegistry::Instance()

    template <>
    ConditionRegistry& ConditionRegistry::Instance() {

        static ConditionRegistry registry;
        return registry;

    }   // ConditionRegistry::Instance()

}   // NAMESPACE CRUCIBLE
