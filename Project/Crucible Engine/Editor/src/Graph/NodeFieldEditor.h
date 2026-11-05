/*************************************************************************
* \file:         NodeFieldEditor.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       2.0
* \brief
*                Draws a node's (or a condition's) fields as ImGui widgets,
*                from the fields its C# class declares, and writes a
*                one-line summary of them for the node box.
*
* \details
*                No per-node editor code: a public field added to a C# node
*                appears here (NodeCatalog). Field types: bool, int, float,
*                string, enum, Vector3, and BlackboardVariable<T> - which gets
*                the link toggle ("Var") to use a graph variable of the
*                matching kind instead of a value of its own. Entity fields
*                can only be linked: an entity can't be stored in an asset.
*
*                Values are kept as text (BehaviorNode.h); the widgets turn
*                that text into numbers and back.
*
* \history
*    -v1.0       first version (RTTR fields of the C++ nodes)
*    -v2.0       the C# nodes' fields, from NodeCatalog
*
**************************************************************************/
#pragma once

// STL
#include <string>

namespace Crucible {

    class BehaviorNode;
    class Blackboard;

    namespace NodeFieldEditor {

        /*!
            \brief
            One row of widgets per field of the node's C# type. A node whose
            type C# doesn't know shows its stored values, read-only.

            \param variables
            The graph's variables: what a linked field can choose from.

            \return
            True if any field changed this frame.
        */
        bool Draw(BehaviorNode& node, const Blackboard& variables, bool readOnly);

        // "Seconds 1, Destination PatrolA" - each field's value (a linked
        // field shows its variable's name), for the node box.
        std::string Summary(const BehaviorNode& node);

    }   // NAMESPACE NODEFIELDEDITOR

}   // NAMESPACE CRUCIBLE
