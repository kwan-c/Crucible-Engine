#pragma once

//std libs
#include <vector>


//header fils
#include "ComponentInfo.h"

/*************************************************************************
* \file:         ComponentRegistry.h
* \author:       Abhishek Akilandeswari Patchamuthu
* \par:          Email: a.patchamuthu@digipen.edu
* \date          13 September 2026
* \version       1.0
* \brief
*                owns and registers component pools, one SparseSet<T> per type
*
* \details
*
* \history
*    -v1.0       skeleton
*
*
**************************************************************************/

namespace Crucible 
{

    class ComponentRegistry
    {
    public:
        ComponentRegistry();
        ~ComponentRegistry() = default;
        template<typename T>
        ComponentId Register() {  return RegisterInfo(MakeComponentInfo<T>()); }
        void RegisterAll();

        const ComponentInfo& Get(ComponentId id)const;
        const ComponentInfo& Get(ComponentKind kind)const { return Get(ToComponentId(kind)); }
        bool IsRegistered(ComponentId id) const;

    private:
        ComponentId RegisterInfo(ComponentInfo info);
        std::vector<ComponentInfo> m_components;
    };
}