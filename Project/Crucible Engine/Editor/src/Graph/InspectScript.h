/*************************************************************************
* \file:         InspectScript.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       2.0
* \brief
*                "Inspect Script": opens the file a node type is written in.
*                "Edit a Copy": makes a built-in node's script your own.
*
* \details
*                Every node is a C# class, so it's that class's .cs - the
*                path the C# compiler recorded in its [NodeDescription]
*                (NodeCatalog's "file"), built-ins and designers' nodes
*                alike. Shared by the tree and state graph views.
*
*                Built-in nodes (Wait, Arrive...) live in the engine's own
*                library, ManagedScripts, outside Assets. Editing one there
*                would change every graph for every teammate, and wouldn't
*                even show until a full rebuild (the Editor only recompiles
*                Assets/Scripts). So, as in Unity, a built-in opens
*                read-only, and Edit a Copy writes a copy into
*                Assets/Scripts/AI with its own class and id - hot-reloaded
*                and free to change.
*
* \history
*    -v1.0       first version (moved out of BehaviorTreeView)
*    -v2.0       built-ins open read-only; Edit a Copy
*
**************************************************************************/
#pragma once

// STL
#include <cstddef>
#include <string>
#include <vector>

namespace Crucible {

    class BehaviorNode;
    struct NodeTypeInfo;

    namespace InspectScript {

        // Opens `path` in the program Windows uses for it. Warns if it can't.
        void OpenFile(const char* path);

        // True if the type's script is the engine's (not in Assets/Scripts).
        bool IsBuiltIn(const NodeTypeInfo& type);

        // Opens the file the type is written in - a built-in's made read-only
        // first. False if the file isn't known.
        bool OpenTypeSource(const NodeTypeInfo& type);

        // The same for a node's type. False if C# doesn't know the type.
        bool OpenNodeSource(const BehaviorNode& node);

        // Can Edit a Copy make one? A built-in whose script only uses what
        // designers' scripts can use (Run Subgraph, for one, needs engine-only parts).
        bool CanCopy(const NodeTypeInfo& type);

        /*!
            \brief
            Writes an editable copy of a built-in node's script:
            Assets/Scripts/AI/ArriveCustomAction.cs, named "Arrive (Custom)",
            with the id "arrive_custom" (or _2, _3... if that's taken). Opens it.

            \return
            The copy's id - it can be used once the scripts have compiled - or
            "" with `error` set.
        */
        std::string MakeEditableCopy(const NodeTypeInfo& type, std::string& error);

        // One text value of a script's [NodeDescription(...)]: where its
        // contents sit in the script, and its name if given as name: / id:.
        struct AttributeValue {

            std::size_t begin = 0;   // the first character inside the quotes
            std::size_t end   = 0;   // the closing quote
            std::string label;       // "name", "id"...; "" when given in order
            int         position = -1;   // its place among the unnamed values (0 = name, 3 = id), -1 if named

        };  // ATTRIBUTEVALUE STRUCT

        /*!
            \brief
            The text values of the first [NodeDescription(...)] in a script, in
            either style - Unity's named one (name: "...", id: "...") or ours in
            order. Quoted text is skipped while looking for the closing ")]", so
            a story's "[Target]" can't end it early; "a" + "b" counts as one value
            (only its first part is listed).
        */
        std::vector<AttributeValue> NodeDescriptionValues(const std::string& script);

        // The value given as `label` (or at `position` if given in order); null if neither.
        const AttributeValue* FindValue(const std::vector<AttributeValue>& values, const std::string& label, int position);

    }   // NAMESPACE INSPECTSCRIPT

}   // NAMESPACE CRUCIBLE
