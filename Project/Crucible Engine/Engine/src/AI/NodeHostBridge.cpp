/*************************************************************************
* \file:         NodeHostBridge.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          5 November 2026
* \version       1.0
* \brief
*                Building and running the C# trees through NodeHost.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "NodeHostBridge.h"

// STL
#include <algorithm>    // std::find
#include <charconv>     // std::from_chars: the asset id's text
#include <variant>

// Crucible engine
#include "AI/BehaviorGraph.h"
#include "Diagnostics/Logger.h"
#include "Scripting/ScriptRuntime.h"

namespace Crucible {

    bool NodeHostBridge::Ready() {

        if (!m_runtime.IsRunning()) {
            if (!m_warned) {
                CRU_WARN("AI: C# scripting isn't running, so state graphs do nothing - enemies stand idle "
                         "(build with CRUCIBLE_ENABLE_MONO=ON and keep scripting enabled)");
                m_warned = true;
            }
            return false;
        }

        // A reload threw the old DLLs (and every C# object) away: load them again.
        if (m_loaded && m_generation == m_runtime.ReloadCount()) return true;
        m_loaded = false;
        if (!m_runtime.LoadAssembly(kScriptCoreAssembly, m_core)) {
            if (!m_warned) { CRU_WARN("AI: could not load {}: {}", kScriptCoreAssembly, m_runtime.LastError()); m_warned = true; }
            return false;
        }
        ScriptAssemblyHandle project{};
        (void)m_runtime.LoadAssembly(kProjectScriptsAssembly, project);   // designers' nodes, if there are any scripts yet

        m_loaded     = true;
        m_generation = m_runtime.ReloadCount();
        return true;

    }   // NodeHostBridge::Ready()

    bool NodeHostBridge::Call(const char* method, const std::vector<ScriptValue>& arguments, ScriptValue& result) {

        if (!m_loaded) return false;
        if (m_runtime.InvokeStatic(m_core, "Crucible.AI", "NodeHost", method, arguments, result)) return true;
        if (!m_warned) { CRU_WARN("AI: NodeHost.{} failed (rebuild the C# scripts?): {}", method, m_runtime.LastError()); m_warned = true; }
        return false;

    }   // NodeHostBridge::Call()

    int NodeHostBridge::CallInt(const char* method, const std::vector<ScriptValue>& arguments) {

        ScriptValue result;
        if (!Call(method, arguments, result)) return 0;
        const std::int32_t* value = std::get_if<std::int32_t>(&result);
        return value ? *value : 0;

    }   // NodeHostBridge::CallInt()

    int NodeHostBridge::Create(const BehaviorNode& node, Entity self) {

        const int id = CallInt("Create", { node.TypeId(), static_cast<std::int32_t>(self.index), static_cast<std::int32_t>(self.generation) });
        if (id == 0) return 0;   // unknown type: C# has warned

        // Only the fields the file has; the rest keep their C# defaults.
        for (const NodeField& field : node.Fields()) {
            if (!field.link.empty()) CallInt("LinkField", { id, field.name, field.link });
            else if (field.kind != FieldKind::None) CallInt("SetField", { id, field.name, field.text });
        }
        return id;

    }   // NodeHostBridge::Create()

    int NodeHostBridge::BuildTree(const BehaviorNode& root, Entity self, const SubgraphFinder& subgraphs) {

        if (!Ready()) return 0;
        std::vector<std::uint64_t> including;
        return BuildNode(root, self, subgraphs, including);

    }   // NodeHostBridge::BuildTree()

    int NodeHostBridge::BuildNode(const BehaviorNode& node, Entity self, const SubgraphFinder& subgraphs, std::vector<std::uint64_t>& including) {

        const int id = Create(node, self);
        if (id == 0) return 0;

        const auto attach = [&](int child) {
            if (child == 0) return;
            if (CallInt("AddChild", { id, child }) == 0) {
                CRU_WARN("AI: '{}' can't take another child - a child was left out", node.TypeId());
                Release(child, m_generation);
            }
        };

        // Run Subgraph: the chosen graph's tree becomes its hidden child. Its On
        // Start is skipped (it repeats forever, so the node could never finish):
        // what runs is what On Start would have run.
        if (node.TypeId() == kRunSubgraphType && subgraphs) {
            const NodeField* field = node.FindField(kRunSubgraphField);
            std::uint64_t asset = 0;
            if (field) std::from_chars(field->text.data(), field->text.data() + field->text.size(), asset);
            if (asset != 0) {
                if (std::find(including.begin(), including.end(), asset) != including.end()) {
                    if (!m_warnedLoop) { CRU_WARN("AI: a Run Subgraph runs a graph it is already inside - left out, or it would never end"); m_warnedLoop = true; }
                }
                else if (const BehaviorGraph* graph = subgraphs(asset)) {
                    const BehaviorNode* entry = graph->Root();
                    if (entry && entry->TypeId() == "start") entry = entry->ChildAt(0);
                    if (entry) {
                        including.push_back(asset);
                        attach(BuildNode(*entry, self, subgraphs, including));
                        including.pop_back();
                    }
                }
            }
        }

        // Depth first. A child that can't be made is left out with its subtree,
        // so the rest of the tree still runs.
        for (std::size_t i = 0; i < node.ChildCount(); ++i) attach(BuildNode(*node.ChildAt(i), self, subgraphs, including));
        return id;

    }   // NodeHostBridge::BuildNode()

    int NodeHostBridge::BuildCondition(const BehaviorNode& condition, Entity self) {

        return Ready() ? Create(condition, self) : 0;

    }   // NodeHostBridge::BuildCondition()

    BehaviorStatus NodeHostBridge::Tick(int root, float deltaTime) {

        if (root == 0 || !Ready()) return BehaviorStatus::Failure;
        const int status = CallInt("Tick", { root, deltaTime });
        return status == 0 ? BehaviorStatus::Running : status == 1 ? BehaviorStatus::Success : BehaviorStatus::Failure;

    }   // NodeHostBridge::Tick()

    void NodeHostBridge::Abort(int root) {

        ScriptValue ignored;
        if (root != 0 && Ready()) Call("Abort", { root }, ignored);

    }   // NodeHostBridge::Abort()

    bool NodeHostBridge::IsTrue(int condition, float timeInState, bool finished, BehaviorStatus finishedWith) {

        if (condition == 0 || !Ready()) return false;   // couldn't be built: the arrow never passes
        return CallInt("IsTrue", { condition, timeInState, std::int32_t{ finished ? 1 : 0 }, static_cast<std::int32_t>(finishedWith) }) != 0;

    }   // NodeHostBridge::IsTrue()

    void NodeHostBridge::Release(int id, std::uint64_t generation) {

        // An id from before a reload is meaningless now - and the number may
        // already belong to a new tree. Its C# object went with the old domain.
        if (id == 0 || !m_runtime.IsRunning() || !m_loaded || generation != m_runtime.ReloadCount()) return;
        ScriptValue ignored;
        Call("Release", { id }, ignored);

    }   // NodeHostBridge::Release()

}   // NAMESPACE CRUCIBLE
