/*************************************************************************
* \file:         NodeCatalog.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          5 November 2026
* \version       1.0
* \brief
*                Asking C# for the node types, and reading its answer.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "NodeCatalog.h"

// STL
#include <variant>

// rapidjson
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

// Crucible engine
#include "AI/NodeHostBridge.h"   // kScriptCoreAssembly, kProjectScriptsAssembly
#include "Diagnostics/Logger.h"
#include "Scripting/ScriptRuntime.h"

namespace Crucible {

    namespace {

        std::string String(const rapidjson::Value& in, const char* name) {

            const auto found = in.FindMember(name);
            return found != in.MemberEnd() && found->value.IsString() ? std::string(found->value.GetString()) : std::string();

        }   // String()

    }   // ANONYMOUS NAMESPACE

    NodeCatalog& NodeCatalog::Instance() {

        static NodeCatalog catalog;
        return catalog;

    }   // NodeCatalog::Instance()

    bool NodeCatalog::Refresh(ScriptRuntime& runtime) {

        if (!runtime.IsRunning()) return false;   // no C#: keep what we have (normally nothing)

        // The engine's built-in nodes, and the designers' (optional: there may
        // be no project scripts yet). Loading an already loaded DLL is free.
        ScriptAssemblyHandle core{};
        ScriptAssemblyHandle project{};
        if (!runtime.LoadAssembly(kScriptCoreAssembly, core)) {
            CRU_WARN("NodeCatalog: could not load {}: {}", kScriptCoreAssembly, runtime.LastError());
            return false;
        }
        (void)runtime.LoadAssembly(kProjectScriptsAssembly, project);

        ScriptValue result;
        if (!runtime.InvokeStatic(core, "Crucible.AI", "NodeHost", "Catalog", {}, result)) {
            CRU_WARN("NodeCatalog: NodeHost.Catalog failed (rebuild the C# scripts?): {}", runtime.LastError());
            return false;
        }
        const std::string* json = std::get_if<std::string>(&result);
        if (!json) return false;

        rapidjson::Document document;
        document.Parse(json->c_str(), json->size());
        const auto nodes = document.IsObject() ? document.FindMember("nodes") : document.MemberEnd();
        if (document.HasParseError() || !document.IsObject() || nodes == document.MemberEnd() || !nodes->value.IsArray()) {
            CRU_WARN("NodeCatalog: C# sent a list that couldn't be read");
            return false;
        }

        std::vector<NodeTypeInfo> types;
        for (const rapidjson::Value& entry : nodes->value.GetArray()) {

            if (!entry.IsObject()) continue;
            NodeTypeInfo type;
            type.id          = String(entry, "id");
            type.className   = String(entry, "class");
            type.name        = String(entry, "name");
            type.story       = String(entry, "story");
            type.category    = String(entry, "category");
            type.description = String(entry, "description");
            type.kind        = String(entry, "kind");
            type.file        = String(entry, "file");
            if (type.id.empty()) continue;

            if (const auto fields = entry.FindMember("fields"); fields != entry.MemberEnd() && fields->value.IsArray()) {
                for (const rapidjson::Value& in : fields->value.GetArray()) {
                    if (!in.IsObject()) continue;
                    NodeFieldInfo field;
                    field.name  = String(in, "name");
                    field.label = String(in, "label");
                    field.type  = String(in, "type");
                    field.value = String(in, "value");
                    field.link  = String(in, "link");
                    if (const auto linkable = in.FindMember("linkable"); linkable != in.MemberEnd() && linkable->value.IsBool()) field.linkable = linkable->value.GetBool();
                    if (const auto options = in.FindMember("options"); options != in.MemberEnd() && options->value.IsArray()) {
                        for (const rapidjson::Value& option : options->value.GetArray()) if (option.IsString()) field.options.emplace_back(option.GetString());
                    }
                    if (!field.name.empty()) type.fields.push_back(std::move(field));
                }
            }
            types.push_back(std::move(type));

        }

        m_types = std::move(types);
        ++m_revision;
        return true;

    }   // NodeCatalog::Refresh()

}   // NAMESPACE CRUCIBLE
