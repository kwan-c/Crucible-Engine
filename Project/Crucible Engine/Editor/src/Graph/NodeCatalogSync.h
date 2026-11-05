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

namespace Crucible {

    class ScriptRuntime;

    // Call every frame a graph window is open; it only asks C# when something changed.
    void SyncNodeCatalog(ScriptRuntime& runtime);

}   // NAMESPACE CRUCIBLE
