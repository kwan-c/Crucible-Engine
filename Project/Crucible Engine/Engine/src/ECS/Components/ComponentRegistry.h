/*************************************************************************
* \file:         ComponentRegistry.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          13 September 2026
* \version       1.3
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
*    -v1.2       Get()/RegisterAll() validate in all builds,
*                RegisteredSignature()/ValidateSignature(); registration is
*                private to World
*    -v1.3       FinalizeRequirements (requirement closure, dependents,
*                cycle check); Detail::CloseRequirements
*
*
**************************************************************************/
#pragma once

//std libs
#include <vector>

#include "EngineExport.h"

//header fils
#include "ComponentInfo.h"
#include "ComponentSignature.h"


namespace Crucible
{
    namespace Detail
    {
        /*********************************************************************
         \brief  Transitive closure of component requirements.
         \param[in]  direct  direct[i] = what component i requires itself.
         \param[out] closure closure[i] = everything i requires, directly or
                             through its requirements' requirements.
         \return kInvalidComponentId when there is no cycle; otherwise a
                 component that (indirectly) requires itself.
         *********************************************************************/
        inline ComponentId CloseRequirements(const std::vector<ComponentSignature>& direct,
                                             std::vector<ComponentSignature>& closure)
        {
            const std::size_t count = direct.size();
            closure = direct;
            // Warshall: anything k needs, everything needing k needs too.
            for (std::size_t k = 0; k < count; ++k)
            {
                for (std::size_t i = 0; i < count; ++i)
                {
                    if (closure[i].test(k)) closure[i] |= closure[k];
                }
            }
            for (std::size_t i = 0; i < count; ++i)
            {
                if (closure[i].test(i)) return static_cast<ComponentId>(i);
            }
            return kInvalidComponentId;
        }
    }

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)
#endif
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
    class ENGINE_API ComponentRegistry
    {
    public:

        /*********************************************************************
         \brief  Constructs an empty registry with one (invalid) slot per
                 possible ComponentId.
         *********************************************************************/
        ComponentRegistry();

        ~ComponentRegistry() = default;

        /*********************************************************************
         \brief  Fetches the ComponentInfo for a numeric component id.
         \param[in] id The id to look up.
         \return Reference to that component's ComponentInfo.
         \note  Throws std::out_of_range (all builds) if id is not a
                registered component.
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

        /*********************************************************************
         \brief  Every registered component id, as a signature mask.
         *********************************************************************/
        ComponentSignature RegisteredSignature() const noexcept;

        /*********************************************************************
         \brief  Rejects a signature that names an unregistered component.
         \param[in] signature The signature to check.
         \param[in] operation Name of the calling operation, used as the
                    message prefix (e.g. "World::CreateEntity").
         \note  Throws std::invalid_argument (all builds), listing the bad
                ids.
         *********************************************************************/
        void ValidateSignature(const ComponentSignature& signature, const char* operation) const;

    private:
        // Registration is World's job (World::RegisterComponent / constructor).
        friend class World;

        /*********************************************************************
         \brief
            Registers component type T, building and storing its
            ComponentInfo if not already registered.
         \return
            The ComponentId T was registered under.
         *********************************************************************/
        template<typename T>
        ComponentId Register() {  return RegisterInfo(MakeComponentInfo<T>()); }

        /*********************************************************************
         \brief
            Registers every component type listed in ComponentKind.h's
            CRUCIBLE_COMPONENT_LIST.
         \note
            Throws std::logic_error (all builds) if a ComponentKind entry
            ended up without a registered type.
         *********************************************************************/
        void RegisterAll();

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

        /*********************************************************************
         \brief  Fills every ComponentInfo's requiredClosure and dependents
                 from the direct requirements (RequiresComponents<T>).
         \note   Throws std::logic_error (all builds), naming the component,
                 if a component requires itself directly or indirectly.
         *********************************************************************/
        void FinalizeRequirements();
        std::vector<ComponentInfo> m_components;
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif
}