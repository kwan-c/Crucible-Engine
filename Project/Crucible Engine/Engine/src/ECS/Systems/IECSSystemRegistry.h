/*************************************************************************
* \file:         IECSSystemRegistry.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          19 September 2026
* \version       1.0
* \brief
*                Declares ECSSystemId (the fixed enum naming every
*                engine system) and ECSSystemRegistry, which owns one
*                instance of every system, indexed by that id.
*
* \details
*                This is the file to edit when adding a new system
*                type: add an id here (before MAX) and construct the
*                matching IECSSystem-derived instance for it in
*                ECSSystemRegistry's constructor (see
*                IECSSystemRegistry.cpp) - that constructor asserts
*                every id in the enum ends up with a constructed
*                system, so a missed entry fails fast at startup
*                rather than crashing later on a null system. See
*                IECSSystems.h for the IECSSystem interface these
*                systems implement, and ECSSystemManager.h/.cpp for how
*                the registry is actually driven each frame.
*
* \history
*    -v1.0       initial fixed system-id enum + owning registry
*
*
**************************************************************************/
#pragma once
#include <array>
#include <cstddef>
#include <memory>

#include "IECSSystems.h"


namespace Crucible
{
    /*************************************************************************
     \brief
        Fixed enumeration naming every engine system, used to index
        ECSSystemRegistry's storage.

     \note
        This is the file to edit when adding a new system type: add an id
        here (before MAX) and construct the matching instance for it in
        ECSSystemRegistry's constructor.
     *************************************************************************/
    enum class ECSSystemId : std::size_t
    {
        Physics,
        MAX
    };


    /*************************************************************************
     \brief
        Owns one instance of every system named in ECSSystemId, indexed by
        that id.

     \details
        The constructor constructs every system and asserts (via a thrown
        std::logic_error) that no slot was left null - so a new ECSSystemId
        added without a matching construction fails fast at startup instead
        of a null-dereference later inside ECSSystemManager.

     \note
        Non-copyable and non-movable: owns systems via std::unique_ptr in a
        fixed-size std::array; there is never a reason for more than one
        live registry.
     *************************************************************************/
    class ECSSystemRegistry
    {
    public:

        /*********************************************************************
         \brief
            Constructs every system named in ECSSystemId.
         \details
            Throws std::logic_error if any ECSSystemId slot was not
            constructed - see the class-level note.
         *********************************************************************/
        ECSSystemRegistry();

        //
        ECSSystemRegistry(const ECSSystemRegistry&) = delete;
        ECSSystemRegistry& operator=(const ECSSystemRegistry&) = delete;
        ECSSystemRegistry(ECSSystemRegistry&&) = delete;
        ECSSystemRegistry& operator=(ECSSystemRegistry&&) = delete;

        /*********************************************************************
         \brief  Fetches a system by id, mutable access.
         \param[in] Id The system to fetch.
         \return Reference to that system.
         \note  Uses std::array::at(), so an out-of-range id throws
                std::out_of_range rather than being undefined behaviour.
         *********************************************************************/
        IECSSystem& GetSystem(ECSSystemId Id)
        {
            return *m_systems.at(static_cast<std::size_t>(Id));
        }

        /*********************************************************************
         \brief  Fetches a system by id, read-only access.
         \param[in] id The system to fetch.
         \return Const reference to that system.
         \note  Uses std::array::at(), so an out-of-range id throws
                std::out_of_range rather than being undefined behaviour.
         *********************************************************************/
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
