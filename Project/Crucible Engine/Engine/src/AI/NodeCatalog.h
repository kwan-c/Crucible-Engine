/*************************************************************************
* \file:         NodeCatalog.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          5 November 2026
* \version       1.0
* \brief
*                NodeCatalog: every behaviour tree node and arrow condition
*                type the C# scripts define - name, menu category, kind,
*                fields and source file. The editor's menus and side panel
*                are built from it.
*
* \details
*                The node types live in C# (ManagedScripts/Runtime/AI/, and
*                designers' own in Assets/Scripts). Refresh() asks C#
*                (NodeHost.Catalog) for the list and keeps it here, so C++
*                code can look a type up by its id without calling C#.
*
*                Only the editor needs it. Running a graph doesn't: C# finds
*                each node's class itself from the id in the file.
*
*                Empty until the first successful Refresh, and when C# isn't
*                running: graphs still load, save and run, they just show
*                ids instead of names in the editor.
*
* \history
*    -v1.0       first version (replaces the C++ NodeRegistry)
*
**************************************************************************/
#pragma once

// STL
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

// Crucible engine
#include "EngineExport.h"

namespace Crucible {

    class ScriptRuntime;

    // One field of a node type, as the C# class declares it.
    struct NodeFieldInfo {

        std::string              name;            // file key: "seconds"
        std::string              label;           // the C# name, shown in the editor: "Seconds"
        std::string              type;            // "float", "int", "bool", "string", "enum", "Vector3", "Entity", "BehaviorGraph"
        bool                     linkable = false;// a BlackboardVariable<T>: can be linked to a graph variable
        std::string              value;           // its default, as text (NodeHost.cs's format)
        std::string              link;            // the variable it starts linked to, or ""
        std::vector<std::string> options;         // an enum's names

    };  // NODEFIELDINFO STRUCT

    // One node or condition type.
    struct NodeTypeInfo {

        std::string                id;            // what files store: "wait"
        std::string                className;     // "Crucible.AI.WaitAction"
        std::string                name;          // "Wait"
        std::string                story;         // the sentence on the node, [Field] filled in: "Wait [Seconds] seconds" ("" = list the fields)
        std::string                category;      // the Add Node submenu: "Actions"; '/' nests: "Action/Navigation"
        std::string                description;
        std::string                kind;          // "action", "modifier", "composite" or "condition"
        std::string                file;          // the .cs it's written in: "Inspect Script"
        std::vector<NodeFieldInfo> fields;

        bool IsCondition() const { return kind == "condition"; }

        // 0 (action), 1 (modifier) or no limit (composite).
        std::size_t MaxChildren() const {
            if (kind == "composite") return std::numeric_limits<std::size_t>::max();
            return kind == "modifier" ? 1u : 0u;
        }

        const NodeFieldInfo* Field(std::string_view fieldName) const {
            for (const NodeFieldInfo& field : fields) if (field.name == fieldName) return &field;
            return nullptr;
        }

    };  // NODETYPEINFO STRUCT

    class NodeCatalog {

    public:

        // The one catalog. Exported, so the Editor shares Engine.dll's copy
        // instead of getting an empty one of its own.
        ENGINE_API static NodeCatalog& Instance();

        /*!
            \brief
            Asks C# for the current list of types (after scripts compile, or
            when the editor opens a graph).

            \return
            False if C# isn't running or the call failed (warned); the old
            list is kept then.
        */
        ENGINE_API bool Refresh(ScriptRuntime& runtime);

        const NodeTypeInfo* Find(std::string_view id) const {
            for (const NodeTypeInfo& type : m_types) if (type.id == id) return &type;
            return nullptr;
        }

        const std::vector<NodeTypeInfo>& All() const { return m_types; }

        // Goes up by one on every successful Refresh: a pointer into the old
        // list is stale once it changes.
        std::uint64_t Revision() const { return m_revision; }

    private:

        NodeCatalog() = default;

        std::vector<NodeTypeInfo> m_types;
        std::uint64_t             m_revision = 0;

    };  // NODECATALOG CLASS

}   // NAMESPACE CRUCIBLE
