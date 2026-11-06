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
#include <cctype>       // std::tolower
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// Crucible engine
#include "AI/NodeCatalog.h"
#include "AssetPipe/Assetmanager.h"
#include "Core.h"
#include "Scripting/ScriptRuntime.h"

// Crucible editor
#include "Graph/InspectScript.h"  // NodeDescriptionValues: reading a script's id
#include "ImGui/ImGuiManager.h"   // EditorManager: the Core, for the asset root

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

    const NodeTypeInfo* NodeTypeForScript(std::string_view assetPath) {

        Core* core = EditorManager::GetInstance().GetCore();
        if (!core) return nullptr;

        // Both paths tidied the same way and lower-cased: Windows paths ignore
        // case, and the compiler may spell the folder differently from the asset root.
        const auto key = [](const std::filesystem::path& path) {
            std::string text = path.lexically_normal().generic_string();
            for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        };
        const std::string wanted = key(std::filesystem::path(core->GetAssets().GetAssetRoot()) / std::filesystem::path(assetPath));

        for (const NodeTypeInfo& type : NodeCatalog::Instance().All()) {
            if (!type.file.empty() && key(std::filesystem::path(type.file)) == wanted) return &type;
        }
        return nullptr;

    }   // NodeTypeForScript()

    std::string WhyNotANode(std::string_view assetPath) {

        const std::string file = std::filesystem::path(assetPath).filename().string();
        Core* core = EditorManager::GetInstance().GetCore();
        std::ifstream in(core ? std::filesystem::path(core->GetAssets().GetAssetRoot()) / std::filesystem::path(assetPath) : std::filesystem::path(), std::ios::binary);
        if (!in) return "Couldn't read " + file + ".";
        const std::string text{ std::istreambuf_iterator<char>(in), {} };

        const std::string howTo = " Add Node > Create new makes a node script with everything it needs.";
        const std::size_t attribute = text.find("[NodeDescription(");
        if (text.find("EntityScript") != std::string::npos) {
            return file + " is an entity script (for Add Component), not a node: a node's class derives from Action, Modifier, Composite or Condition." + howTo;
        }
        if (attribute == std::string::npos) return file + " has no [NodeDescription], so it isn't a node." + howTo;

        // Its id (id: "...", or the 4th value in order): another node using it wins.
        const std::vector<InspectScript::AttributeValue> values = InspectScript::NodeDescriptionValues(text);
        if (const InspectScript::AttributeValue* idValue = InspectScript::FindValue(values, "id", 3)) {
            const std::string id = text.substr(idValue->begin, idValue->end - idValue->begin);
            if (const NodeTypeInfo* other = NodeCatalog::Instance().Find(id)) {
                return file + " uses the id \"" + id + "\", which " + other->name + " already has - each node needs its own (id in [NodeDescription]).";
            }
        }
        return file + " hasn't compiled into a node yet - wait for \"Scripts reloaded\" in the Console, or check it for script errors (a missing \"using Crucible.AI;\" is a common one).";

    }   // WhyNotANode()

}   // NAMESPACE CRUCIBLE
