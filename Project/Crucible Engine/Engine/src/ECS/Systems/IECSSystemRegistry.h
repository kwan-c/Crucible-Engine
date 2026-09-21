/*************************************************************************
* \file:         IECSSystemRegistry.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.0
* \brief
*      Dynamic registry that owns all registered ECS systems.
*
* \details
*      Systems are registered explicitly through RegisterSystem<T>().
*      Each system is stored through IECSSystem ownership so engine,
*      game, and plugin systems can share the same execution pipeline
*      without modifying a fixed system enum.
*
* \history
*    -v1.0       initial fixed system-id enum + owning registry
*
*
**************************************************************************/
#pragma once
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "IECSSystems.h"


namespace Crucible
{


    /*************************************************************************
     \brief
        Owns all dynamically registered ECS systems.

     \details
        Systems are registered explicitly through RegisterSystem<T>() and
        stored polymorphically as std::unique_ptr<IECSSystem>. This allows
        engine, game, and plugin modules to contribute systems without
        modifying a fixed enum or registry table.

     \note
        Non-copyable and non-movable because the registry owns system
        lifetimes.
     *************************************************************************/
    class ECSSystemRegistry
    {
    public:

        /*********************************************************************
         \brief Creates an empty system registry.
         *********************************************************************/
        ECSSystemRegistry() = default;
        ~ECSSystemRegistry() = default;

        //
        ECSSystemRegistry(const ECSSystemRegistry&) = delete;
        ECSSystemRegistry& operator=(const ECSSystemRegistry&) = delete;
        ECSSystemRegistry(ECSSystemRegistry&&) = delete;
        ECSSystemRegistry& operator=(ECSSystemRegistry&&) = delete;

        /*********************************************************************
         \brief
            Registers a new ECS system.

         \tparam T
            Type derived from IECSSystem.

         \param args
            Constructor arguments forwarded to T.

         \return
            Reference to the newly created system.
         *********************************************************************/
        template<typename T, typename... Args>
        T& RegisterSystem(Args&&... args)
        {
            static_assert(
                std::is_base_of_v<IECSSystem, T>,
                "T must derive from IECSSystem"
                );

            auto system = std::make_unique<T>(
                std::forward<Args>(args)...
            );

            T& reference = *system;

            m_systems.push_back(std::move(system));

            return reference;
        }

        std::size_t Size() const noexcept
        {
            return m_systems.size();
        }

        /*********************************************************************
         \brief  Fetches a system by index, mutable access.
         \param[in] index The system to fetch.
         \return Reference to that system.
         *********************************************************************/
        IECSSystem& GetSystem(std::size_t index)
        {
            return *m_systems.at(index);
        }

        /*********************************************************************
         \brief  Fetches a system by index, read-only access.
         \param[in] indexThe system to fetch.
         \return Const reference to that system.
         *********************************************************************/
        const IECSSystem& GetSystem(std::size_t index) const
        {
            return *m_systems.at(index);
        }

    private:
        std::vector<std::unique_ptr<IECSSystem>> m_systems;

    };
}
