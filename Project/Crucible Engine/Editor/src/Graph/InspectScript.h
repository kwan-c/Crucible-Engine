/*************************************************************************
* \file:         InspectScript.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                "Inspect Script": opens the file a node type is written in.
*
* \details
*                Every node is a C# class, so it's that class's .cs - the
*                path the C# compiler recorded in its [NodeDescription]
*                (NodeCatalog's "file"), built-ins and designers' nodes
*                alike. Shared by the tree and state graph views.
*
* \history
*    -v1.0       first version (moved out of BehaviorTreeView)
*
**************************************************************************/
#pragma once

namespace Crucible {

    class BehaviorNode;

    namespace InspectScript {

        // Opens `path` in the program Windows uses for it. Warns if it can't.
        void OpenFile(const char* path);

        // Opens the file `node`'s type is written in. False if it isn't known.
        bool OpenNodeSource(const BehaviorNode& node);

    }   // NAMESPACE INSPECTSCRIPT

}   // NAMESPACE CRUCIBLE
