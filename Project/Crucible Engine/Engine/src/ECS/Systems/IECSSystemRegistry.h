/*************************************************************************
* \file:         IECSSystemRegistry.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.1
* \brief
*      Dynamic registry that owns all registered ECS systems.
*
* \details
*      Systems are registered explicitly through RegisterSystem<T>().
*      Each system is stored through IECSSystem ownership so engine,
*      game, and plugin systems can share the same execution pipeline
*      without modifying a fixed system enum. One instance per system
*      type is allowed, so FindSystem<T>() is unambiguous.
*
* \history
*    -v1.0       initial fixed system-id enum + owning registry
*    -v1.1       dynamic registration; added FindSystem<T>() and rejection
*                of duplicate system types; existing
*                RegisterSystem/GetSystem/Size unchanged
*
**************************************************************************/
#pragma once
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <typeinfo>
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
        lifetimes. Registering a second system of the same type throws.
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

         \note
            Throws std::logic_error if a system of type T is already
            registered.
         *********************************************************************/
        template<typename T, typename... Args>
        T& RegisterSystem(Args&&... args)
        {
            static_assert(
                std::is_base_of_v<IECSSystem, T>,
                "T must derive from IECSSystem"
                );

            if (FindSystem<T>() != nullptr)
            {
                throw std::logic_error("ECSSystemRegistry::RegisterSystem - a system of this type is already registered");
            }

            // Reserve first so the two pushes below cannot throw and leave
            // m_systems and m_types out of step.
            m_systems.reserve(m_systems.size() + 1);
            m_types.reserve(m_types.size() + 1);

            auto system = std::make_unique<T>(
                std::forward<Args>(args)...
            );

            T& reference = *system;

            m_systems.push_back(std::move(system));
            m_types.emplace_back(typeid(T));

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
         \param[in] index The system to fetch.
         \return Const reference to that system.
         *********************************************************************/
        const IECSSystem& GetSystem(std::size_t index) const
        {
            return *m_systems.at(index);
        }

        /*********************************************************************
         \brief  The registered system of type T.
         \return Pointer to it, or nullptr if no system of type T exists.
         *********************************************************************/
        template <typename T>
        T* FindSystem() noexcept
        {
            const std::type_index wanted(typeid(T));
            for (std::size_t i = 0; i < m_systems.size(); ++i)
            {
                if (m_types[i] == wanted) return static_cast<T*>(m_systems[i].get());
            }
            return nullptr;
        }

    private:
        std::vector<std::unique_ptr<IECSSystem>> m_systems;
        std::vector<std::type_index> m_types;   // parallel to m_systems

    };
}
