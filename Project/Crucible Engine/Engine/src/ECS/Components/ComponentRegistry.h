/*************************************************************************
* \file:         ComponentRegistry.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          13 September 2026
* \version       1.1
* \brief
*                owns and registers per-type ComponentInfo used to
*                build each Archetype's ComponentColumns
*
* \details
*                ComponentRegistry owns one ComponentInfo (see
*                ComponentInfo.h) per registered ComponentKind,
*                indexed directly by ComponentId. World (see World.h)
*                owns exactly one ComponentRegistry and calls
*                RegisterAll() once in its constructor (see World.cpp)
*                to populate every type listed in ComponentKind.h's
*                CRUCIBLE_COMPONENT_LIST. Archetype
*                (Archetype.h/.cpp) reads ComponentInfo back out via
*                Get() when building its ComponentColumns.
*
* \history
*    -v1.0       skeleton
*    -v1.1       added member-level comments for readability, no logic changes
*
*
**************************************************************************/
#pragma once

//std libs
#include <vector>


//header fils
#include "ComponentInfo.h"


namespace Crucible 
{
    /*************************************************************************
     \brief
        Owns one ComponentInfo per registered ComponentKind, indexed
        directly by ComponentId.

     \details
        World owns exactly one ComponentRegistry and calls RegisterAll()
        once in its constructor to populate every type listed in
        ComponentKind.h's CRUCIBLE_COMPONENT_LIST. Archetype reads
        ComponentInfo back out via Get() when building its ComponentColumns.

     \note
        m_components is sized to kMaxComponents up front, so every valid
        ComponentId has a slot from construction on - RegisterAll() only
        fills those slots in.
     *************************************************************************/
    class ComponentRegistry
    {
    public:

        /*********************************************************************
         \brief  Constructs an empty registry with one (invalid) slot per
                 possible ComponentId.
         *********************************************************************/
        ComponentRegistry();

        ~ComponentRegistry() = default;

        /*********************************************************************
         \brief
            Registers component type T, building and storing its
            ComponentInfo if not already registered.

         \tparam T
            The component type to register; must satisfy RegisteredComponent.
         \return
            The ComponentId T was registered under.
         *********************************************************************/
        template<typename T>
        ComponentId Register() {  return RegisterInfo(MakeComponentInfo<T>()); }

        /*********************************************************************
         \brief
            Registers every component type listed in ComponentKind.h's
            CRUCIBLE_COMPONENT_LIST.

         \details
            Called once by World's constructor. Asserts afterwards that
            every ComponentKind ended up with a valid ComponentInfo -
            catching a component added to the enum but never bound via
            CRUCIBLE_COMPONENT.
         *********************************************************************/
        void RegisterAll();


        /*********************************************************************
         \brief  Fetches the ComponentInfo for a numeric component id.
         \param[in] id The id to look up.
         \return Reference to that component's ComponentInfo.
         \note  Asserts id is valid; does not check it has been registered.
         *********************************************************************/
        const ComponentInfo& Get(ComponentId id)const;


        /*********************************************************************
         \brief  Fetches the ComponentInfo for a ComponentKind.
         \param[in] kind The kind to look up.
         \return Reference to that component's ComponentInfo.
         *********************************************************************/
        const ComponentInfo& Get(ComponentKind kind)const { return Get(ToComponentId(kind)); }

        /*********************************************************************
         \brief  Whether a component id has a valid, registered ComponentInfo.
         \param[in] id The id to check.
         \return true if id is in range and its slot has been filled in.
         *********************************************************************/
        bool IsRegistered(ComponentId id) const;

    private:
        /*********************************************************************
         \brief
            Stores a built ComponentInfo at its own id's slot, if not
            already filled.

         \param[in]
            info The ComponentInfo to store (its id selects the slot).
         \return
            info.id.

         \note
            A second registration of the same id is a silent no-op rather
            than an overwrite or an error.
         *********************************************************************/
        ComponentId RegisterInfo(ComponentInfo info);
        std::vector<ComponentInfo> m_components;
    };
}