/*************************************************************************
* \file:         ECSSystemManager.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.0
* \brief
*                Declares ECSSystemManager: the top-level driver that
*                owns an ECSSystemRegistry (see IECSSystemRegistry.h)
*                and calls each system's Init/Update/Shutdown in a
*                fixed, sequential order.
*
* \details
*                This is the class the engine's Core (see
*                Engine/src/Core.cpp) is expected to own and drive each
*                frame - it hides the individual per-system loop behind
*                a single Init()/Update(World&)/Shutdown() call. See
*                ECSSystemManager.cpp for the current (sequential,
*                single-system) implementation;
*
* \history
*    -v1.0       initial sequential single-system driver (Physics only)
*
*
**************************************************************************/
#pragma once

#include "IECSSystemRegistry.h"

namespace Crucible
{
    /*************************************************************************
     \brief
        Top-level driver for all registered ECS systems.

     \details
        Owns the ECSSystemRegistry and CommandBuffer. Registered systems are
        sorted by priority during Init(), updated sequentially each frame,
        and shut down in reverse execution order.

     \note
        All systems must be registered before Init().
     *************************************************************************/
    class ECSSystemManager
    {
    public:

        ECSSystemManager()= default;

        ECSSystemManager(const ECSSystemManager&) = delete;
        ECSSystemManager& operator=(const ECSSystemManager&) = delete;
        ECSSystemManager(ECSSystemManager&&) = delete;
        ECSSystemManager& operator=(ECSSystemManager&&) = delete;

        /*********************************************************************
         \brief
            Registers a system into the ECS system manager.

         \note
            Register systems before Init() is called.
         *********************************************************************/
        template<typename T, typename... Args>
        T& RegisterSystem(Args&&... args)
        {
            assert(!m_initialized &&"Systems must be registered before ECSSystemManager::Init()");

            T& system = m_registry.RegisterSystem<T>( std::forward<Args>(args)...);
            return system;
        }

        /*********************************************************************
         \brief  Calls Init() on every registered system, in order.
         *********************************************************************/
        void Init();

        /*********************************************************************
         \brief  Calls Update(world) on every registered system, in order.
         \param[in,out] world The world every system will read/modify.
         *********************************************************************/
        void Update(World& world);

        /*********************************************************************
         \brief  Calls Shutdown() on every registered system, in order.
         *********************************************************************/
        void Shutdown() noexcept;

        /*********************************************************************
         \brief  Fetches a registered system by index, mutable access.
         \param[in] index The system to fetch.
         \return Reference to that system.
         *********************************************************************/
        IECSSystem& GetSystem(std::size_t index)
        {
            return m_registry.GetSystem(index);
        }

        /*********************************************************************
         \brief  Fetches a registered system by id, read-only access.
         \param[in] id The system to fetch.
         \return Const reference to that system.
         *********************************************************************/
        const IECSSystem& GetSystem(std::size_t index) const
        {
            return m_registry.GetSystem(index);
        }


    private:
        bool m_initialized = false;

        void RebuildUpdateOrder();

        ECSSystemRegistry m_registry;

        CommandBuffer m_commands;

        // Stores registry indices in execution order.
        std::vector<std::size_t> m_updateOrder;
    };
}
