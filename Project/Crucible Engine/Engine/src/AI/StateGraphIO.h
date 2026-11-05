/*************************************************************************
* \file:         StateGraphIO.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                Saving and loading state graphs (.stategraph) as versioned
*                JSON.
*
* \details
*                  { "version": 1, "entry": 0,
*                    "variables": [ ... same as a .behaviorgraph's ... ],
*                    "states": [ { "name": "Patrol", "singleNode": false, "editor": [0, 0],
*                                  "tree": { "root": 0, "nodes": [ ... ] } },
*                                { "name": "Search", "subgraph": 1234567890, ... } ],
*                    "transitions": [ { "from": 0, "to": 1,
*                                       "conditions": [ { "type": "target_within",
*                                                         "fields": { "distance": { "value": 8 } } } ] } ] }
*
*                States and arrows refer to each other by their place in
*                the "states" list. Each state's tree uses exactly the
*                .behaviorgraph tree layout (BehaviorGraphIO.h), and each
*                condition saves like a node: its registry id plus its
*                RTTR fields.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <memory>
#include <span>
#include <string>

// Crucible engine
#include "AI/StateGraph.h"
#include "EngineExport.h"

namespace Crucible {

    struct StateGraphResult {

        bool                        ok = false;
        std::string                 error;   // why not, when !ok
        std::unique_ptr<StateGraph> graph;   // null when !ok

    };  // STATEGRAPHRESULT STRUCT

    // Reads a .stategraph file's bytes. Never throws: problems come back in `error`.
    ENGINE_API StateGraphResult DecodeStateGraph(std::span<const unsigned char> bytes);

    // Writes a state graph as pretty-printed JSON, ready to save.
    ENGINE_API std::string EncodeStateGraph(const StateGraph& graph);

}   // NAMESPACE CRUCIBLE
