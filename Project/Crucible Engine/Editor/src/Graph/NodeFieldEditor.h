/*************************************************************************
* \file:         NodeFieldEditor.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                Draws a node's (or a condition's) fields as ImGui widgets,
*                straight from its RTTR registration, and writes a one-line
*                summary of them for the node box.
*
* \details
*                No per-node editor code: a new node type registers its
*                fields with RTTR and they appear here. Handled field kinds:
*                bool, int, float, text, Vector3, enums, and NodeValue<T> -
*                which gets the link toggle ("Var") to use a blackboard
*                variable of the matching kind instead of a typed value.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <string>

// RTTR
#include <rttr/instance.h>

namespace Crucible {

    class Blackboard;

    namespace NodeFieldEditor {

        /*!
            \brief
            One row of widgets per field of `object`.

            \param variables
            The graph's variables: what a linked field can choose from.

            \return
            True if any field changed this frame.
        */
        bool Draw(rttr::instance object, const Blackboard& variables, bool readOnly);

        // "seconds 1, destination PatrolA" - each field's name and value (a
        // linked field shows its variable's name), for the node box.
        std::string Summary(rttr::instance object);

    }   // NAMESPACE NODEFIELDEDITOR

}   // NAMESPACE CRUCIBLE
