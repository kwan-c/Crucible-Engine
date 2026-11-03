/*************************************************************************
* \file:         NodeValue.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                RTTR registration for NodeValue - the field type node and
*                condition settings are made of - for each kind of value.
*
* \details
*                Entity has no "value": an entity differs from scene to
*                scene, so a graph asset can only name a blackboard
*                variable that holds one (Self, Target), never store one.
*
* \history
*    -v1.0       first version (moved here from NodeRegistry.cpp)
*
**************************************************************************/
#include "NodeValue.h"

// Crucible engine
#include "Reflection/ReflectionRegistration.h"

RTTR_REGISTRATION
{
    using namespace Crucible;

    rttr::registration::class_<NodeValue<float>>("NodeValueFloat")
        .property("value",    &NodeValue<float>::value)
        .property("variable", &NodeValue<float>::variable);

    rttr::registration::class_<NodeValue<int>>("NodeValueInt")
        .property("value",    &NodeValue<int>::value)
        .property("variable", &NodeValue<int>::variable);

    rttr::registration::class_<NodeValue<bool>>("NodeValueBool")
        .property("value",    &NodeValue<bool>::value)
        .property("variable", &NodeValue<bool>::variable);

    rttr::registration::class_<NodeValue<Math::vec3>>("NodeValueVec3")
        .property("value",    &NodeValue<Math::vec3>::value)
        .property("variable", &NodeValue<Math::vec3>::variable);

    rttr::registration::class_<NodeValue<Entity>>("NodeValueEntity")
        .property("variable", &NodeValue<Entity>::variable);
}
