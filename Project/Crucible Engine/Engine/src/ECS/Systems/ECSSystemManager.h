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
        Top-level driver that owns an ECSSystemRegistry and calls each
        system's Init/Update/Shutdown in a fixed, sequential order.

     \details
        This is the class the engine's Core is expected to own and drive
        each frame - it hides the per-system loop behind a single
        Init()/Update(World&)/Shutdown() call.

     \note
        Currently drives only ECSSystemId::Physics
     *************************************************************************/
    class ECSSystemManager
    {
    public:

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
         \brief  Fetches a registered system by id, mutable access.
         \param[in] id The system to fetch.
         \return Reference to that system.
         *********************************************************************/
        IECSSystem& GetSystem(ECSSystemId id)
        {
            return m_registry.GetSystem(id);
        }

        /*********************************************************************
         \brief  Fetches a registered system by id, read-only access.
         \param[in] id The system to fetch.
         \return Const reference to that system.
         *********************************************************************/
        const IECSSystem& GetSystem(ECSSystemId id)const
        {
            return m_registry.GetSystem(id);
        }

    private:
        ECSSystemRegistry m_registry;

        CommandBuffer m_commands;
    };
}
