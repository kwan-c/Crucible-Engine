/*************************************************************************
* \file:         NodeCatalogSync.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          5 November 2026
* \version       1.0
* \brief
*                Keeps NodeCatalog (the C# node types) in step with the
*                scripts: asks C# again the first time, and after every
*                script reload - so a node a designer just wrote appears in
*                Add Node without restarting the Editor.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <string>
#include <string_view>

namespace Crucible {

    class ScriptRuntime;
    struct NodeTypeInfo;

    // Call every frame a graph window is open; it only asks C# when something changed.
    void SyncNodeCatalog(ScriptRuntime& runtime);

    // The node (or condition) written in a .cs file - an Asset Browser path like
    // "Scripts/AI/AttackPlayerAction.cs" - matched against the file each C# node
    // type was compiled from. Null if the file has none (an entity script, or not
    // compiled yet). A file with several nodes gives the first.
    const NodeTypeInfo* NodeTypeForScript(std::string_view assetPath);

    // Why NodeTypeForScript found no node in that file, in plain words - read
    // from the file itself: an entity script, no [NodeDescription], an id
    // another node already has, or (otherwise) not compiled yet.
    std::string WhyNotANode(std::string_view assetPath);

}   // NAMESPACE CRUCIBLE
