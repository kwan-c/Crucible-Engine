/*************************************************************************
* \file:         InspectScript.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                Opening a node type's source file.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "InspectScript.h"

// STL
#include <filesystem>
#include <string>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <shellapi.h>   // ShellExecuteW
#endif

// Crucible engine
#include "AI/BehaviorNode.h"
#include "AI/NodeRegistry.h"
#include "Diagnostics/Logger.h"

namespace Crucible::InspectScript {

    void OpenFile(const char* path) {

#ifdef _WIN32
        const std::wstring file = std::filesystem::path(path).wstring();
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
            CRU_USER_WARN("Could not open {}", path);
        }
#else
        CRU_USER_WARN("Inspect Script is only implemented on Windows ({})", path);
#endif

    }   // OpenFile()

    bool OpenNodeSource(const BehaviorNode& node) {

        const NodeTypeEntry* entry = NodeRegistry::Instance().Find(node.Description().id);
        if (!entry || !entry->sourceFile || !*entry->sourceFile) return false;
        OpenFile(entry->sourceFile);
        return true;

    }   // OpenNodeSource()

}   // NAMESPACE CRUCIBLE::INSPECTSCRIPT
