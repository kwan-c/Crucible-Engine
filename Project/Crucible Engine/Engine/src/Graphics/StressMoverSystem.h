/*************************************************************************
  \file       StressMoverSystem.h
  \brief
    TEMPORARY - load testing only. Walks every entity that has a
    StressMover component around a circle, so the cost of thousands of
    MOVING entities (transform update, re-extraction into the renderer)
    can be measured before real gameplay movement exists.
  \details
    Runs in the Update stage, in the editor as well as in play mode, so a
    "Stress Moving" scene moves as soon as it is opened. To remove: this
    file and its .cpp, the registration in Core::RegisterEngineSystems,
    the StressMover component (Components.h, ComponentKind.h,
    Reflection/Components/StressMoverReflection.cpp) and the scenes.
*************************************************************************/
#pragma once

#include "ECS/Systems/IECSSystems.h"
#include "ECS/Core/ECSCore.h"
#include "ECS/Components/Components.h"
#include "ECS/Systems/Query.h"
#include "EngineExport.h"

#include <optional>

namespace Crucible {

    class ENGINE_API StressMoverSystem final : public IECSSystem
    {
    public:
        void Describe(SystemDesc& desc) const override;
        void Update(World& world, SystemContext& ctx) override;

    private:
        using MoverQuery = Query<Transform, const StressMover>;

        std::optional<MoverQuery> m_query;   // built once, on first Update() (needs a World&)
        float                     m_time = 0.0f;
    };

}   // NAMESPACE CRUCIBLE
