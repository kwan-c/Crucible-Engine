#pragma once

#include "IECSSystemRegistry.h"

namespace Crucible
{
    class ECSSystemManager
    {
    public:
        void Init();
        void Update(World& world);
        void Shutdown() noexcept;

        IECSSystem& GetSystem(ECSSystemId id)
        {
            return m_registry.GetSystem(id);
        }

        const IECSSystem& GetSystem(ECSSystemId id)const
        {
            return m_registry.GetSystem(id);
        }

    private:
        ECSSystemRegistry m_registry;
    };
}
