/*************************************************************************
* \file:         NodeCatalogSync.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          5 November 2026
* \version       1.0
* \brief
*                Refreshing the node list when the scripts change.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "NodeCatalogSync.h"

// STL
#include <cstdint>

// Crucible engine
#include "AI/NodeCatalog.h"
#include "Scripting/ScriptRuntime.h"

namespace Crucible {

    void SyncNodeCatalog(ScriptRuntime& runtime) {

        // The reload count the list was last asked for under; "none yet" to start.
        static std::uint64_t asked = ~0ull;

        if (!runtime.IsRunning() || asked == runtime.ReloadCount()) return;

        // Noted even if the call fails: Refresh has warned, and asking again
        // every frame would only repeat the warning. The next reload retries.
        asked = runtime.ReloadCount();
        NodeCatalog::Instance().Refresh(runtime);

    }   // SyncNodeCatalog()

}   // NAMESPACE CRUCIBLE
