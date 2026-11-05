/*************************************************************************
* \file:         BehaviorGraphIO.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                Saving and loading behaviour graphs (.behaviorgraph) as
*                versioned JSON, plus the pieces StateGraphIO reuses for
*                the trees inside states.
*
* \details
*                The layout is a FLAT list of nodes, each once:
*
*                  { "version": 1,
*                    "variables": [ { "name": "Wait", "type": "Float", "value": 2 } ],
*                    "tree": { "root": 0,
*                              "nodes": [ { "id": 0, "type": "start", "editor": [0, 0],
*                                           "fields": { "repeat": true }, "children": [1] },
*                                         ... ] } }
*
*                Flat rather than nested so the editor can keep nodes that
*                aren't connected yet, and the same idea carries over to
*                state graphs, which have loops. Node ids only exist in the
*                file; "type" is the node's registry id; "fields" is written
*                and read through the node's RTTR registration, so a new
*                node type saves with no code here.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <cstddef>
#include <memory>
#include <span>
#include <string>

// rapidjson
#include <rapidjson/document.h>

// Crucible engine
#include "AI/BehaviorGraph.h"
#include "EngineExport.h"

namespace Crucible {

    // Far bigger than any real graph; stops a broken file eating memory.
    inline constexpr std::size_t kMaxBehaviorGraphBytes = 4 * 1024 * 1024;

    struct BehaviorGraphResult {

        bool                           ok = false;
        std::string                    error;    // why not, when !ok
        std::unique_ptr<BehaviorGraph> graph;    // null when !ok

    };  // BEHAVIORGRAPHRESULT STRUCT

    // Reads a .behaviorgraph file's bytes. Never throws: problems come back in `error`.
    ENGINE_API BehaviorGraphResult DecodeBehaviorGraph(std::span<const unsigned char> bytes);

    // Writes a graph as pretty-printed JSON, ready to save.
    ENGINE_API std::string EncodeBehaviorGraph(const BehaviorGraph& graph);

    // The building blocks, shared with StateGraphIO (a state's tree and the
    // state graph's variables use the same layout as a .behaviorgraph's).
    namespace BehaviorJson {

        using Allocator = rapidjson::Document::AllocatorType;

        // { "root": id or -1, "nodes": [...] } - the tree and its detached nodes.
        void WriteTree(const BehaviorGraph& tree, rapidjson::Value& out, Allocator& allocator);
        bool ReadTree(const rapidjson::Value& in, BehaviorGraph& tree, std::string& error);

        // A node's (or condition's) "fields" object.
        bool ReadFields(const rapidjson::Value& in, std::vector<NodeField>& fields);
        void WriteFields(const std::vector<NodeField>& fields, rapidjson::Value& out, Allocator& allocator);

        // [ { "name", "type", "value" }, ... ]
        void WriteVariables(const Blackboard& variables, rapidjson::Value& out, Allocator& allocator);
        bool ReadVariables(const rapidjson::Value& in, Blackboard& variables, std::string& error);

        // Saving and loading the editor's 2D positions, as [x, y].
        rapidjson::Value WriteVec2(const Math::vec2& v, Allocator& allocator);
        Math::vec2       ReadVec2(const rapidjson::Value& in);

    }   // NAMESPACE BEHAVIORJSON

}   // NAMESPACE CRUCIBLE
