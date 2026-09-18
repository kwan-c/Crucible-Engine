#pragma once
#include <array>
#include <cstddef>
#include <memory>

#include "IECSSystems.h"


namespace Crucible
{
    //for fixed ecs systems
    enum class ECSSystemId : std::size_t
    {
        Physics,
        MAX
    };



    class ECSSystemRegistry
    {
    public:
        ECSSystemRegistry();

        //
        ECSSystemRegistry(const ECSSystemRegistry&) = delete;
        ECSSystemRegistry& operator=(const ECSSystemRegistry&) = delete;
        ECSSystemRegistry(ECSSystemRegistry&&) = delete;
        ECSSystemRegistry& operator=(ECSSystemRegistry&&) = delete;

        IECSSystem& GetSystem(ECSSystemId Id)
        {
            return *m_systems.at(static_cast<std::size_t>(Id));
        }
        const IECSSystem& GetSystem(ECSSystemId id) const
        {
            return *m_systems.at(static_cast<std::size_t>(id));
        }

    private:
        static constexpr std::size_t SystemCount =
            static_cast<std::size_t>(ECSSystemId::MAX);

        std::array<std::unique_ptr<IECSSystem>, SystemCount>
            m_systems;

    };
}
