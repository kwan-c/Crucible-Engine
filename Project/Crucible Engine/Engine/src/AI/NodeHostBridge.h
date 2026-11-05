/*************************************************************************
* \file:         NodeHostBridge.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          5 November 2026
* \version       1.0
* \brief
*                NodeHostBridge: turns BehaviorNode data into C# node objects
*                for one enemy, and runs them - through C#'s Crucible.AI.NodeHost.
*
* \details
*                C++ can only pass numbers and strings to C#, so each C# tree
*                and condition is known here by the int id NodeHost gave it.
*                Building costs a few C# calls per node, once per enemy;
*                running a whole tree is ONE call per frame (its composites
*                run their children inside C#).
*
*                Ids belong to one script domain: a script reload (between
*                Play sessions) throws every C# object away. Generation()
*                says which reload ids came from; Release skips ids from an
*                older one instead of freeing a newer tree that reused the
*                number.
*
*                Without C# (Mono off, scripting disabled) nothing is built:
*                enemies stand idle, and the log says why, once.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Crucible engine
#include "AI/BehaviorNode.h"
#include "ECS/Core/ECSCore.h"           // Entity
#include "Scripting/ScriptTypes.h"      // ScriptAssemblyHandle, ScriptValue

namespace Crucible {

    class BehaviorGraph;
    class ScriptRuntime;

    // The type id of the Run Subgraph node (RunSubgraphNode.cs) and its field naming the graph.
    inline constexpr const char* kRunSubgraphType  = "run_subgraph";
    inline constexpr const char* kRunSubgraphField = "graph";

    // The C# DLLs the nodes live in: the engine's built-ins, and the designers'.
    inline constexpr const char* kScriptCoreAssembly     = "Managed/Crucible.ScriptCore.dll";
    inline constexpr const char* kProjectScriptsAssembly = "Managed/Crucible.Scripts.dll";

    class NodeHostBridge {

    public:

        explicit NodeHostBridge(ScriptRuntime& runtime) : m_runtime(runtime) {}

        // C# is up and the node DLLs are loaded (again, after a reload). Logs once if not.
        bool Ready();

        // The script reload the ids handed out now belong to.
        std::uint64_t Generation() const { return m_generation; }

        // Finds a Run Subgraph node's .behaviorgraph by asset id while a tree is
        // being built. Null: none, or it didn't load. The graph only has to
        // stay alive until BuildTree returns - its C# copy doesn't need it.
        using SubgraphFinder = std::function<const BehaviorGraph*(std::uint64_t)>;

        // A C# copy of the tree under `root` for `self`. 0 if nothing could
        // be built (no C#, or the root's type is unknown). A child whose type
        // is unknown is left out, with its subtree. Each Run Subgraph node gets
        // its graph's tree (without its On Start) as a hidden child.
        int BuildTree(const BehaviorNode& root, Entity self, const SubgraphFinder& subgraphs = {});

        // A C# copy of one arrow condition. 0 if it couldn't be built.
        int BuildCondition(const BehaviorNode& condition, Entity self);

        // One frame of a tree. Failure for id 0 or when C# is down.
        BehaviorStatus Tick(int root, float deltaTime);

        // Cuts a tree short: its running nodes get OnEnd.
        void Abort(int root);

        // One condition, told how the enemy's current state is going.
        bool IsTrue(int condition, float timeInState, bool finished, BehaviorStatus finishedWith);

        // Frees a tree (every node in it) or a condition built in `generation`.
        void Release(int id, std::uint64_t generation);

    private:

        // The node and its fields; children are added by BuildTree.
        int  Create(const BehaviorNode& node, Entity self);

        // BuildTree's work. `including` lists the subgraphs being built around
        // this node, so a graph that runs itself is refused instead of recursing forever.
        int  BuildNode(const BehaviorNode& node, Entity self, const SubgraphFinder& subgraphs, std::vector<std::uint64_t>& including);
        bool Call(const char* method, const std::vector<ScriptValue>& arguments, ScriptValue& result);
        int  CallInt(const char* method, const std::vector<ScriptValue>& arguments);

        ScriptRuntime&       m_runtime;
        ScriptAssemblyHandle m_core{};
        bool                 m_loaded     = false;
        std::uint64_t        m_generation = ~0ull;   // ReloadCount the DLLs were loaded under
        bool                 m_warned     = false;   // "C# isn't running" said once
        bool                 m_warnedLoop = false;   // "a subgraph runs itself" said once (every enemy would repeat it)

    };  // NODEHOSTBRIDGE CLASS

}   // NAMESPACE CRUCIBLE
