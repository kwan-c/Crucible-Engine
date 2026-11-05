/*************************************************************************
* \file:         VariablesEditor.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                VariablesEditor: the list of a graph's blackboard variables
*                - name, kind, default value - with add and delete. Shared
*                by the Behavior Graph and State Graph windows.
*
* \history
*    -v1.0       first version (moved out of ImGuiPanel_BehaviorGraph)
*
**************************************************************************/
#pragma once

// STL
#include <string>

namespace Crucible {

    class Blackboard;

    class VariablesEditor {

    public:

        /*!
            \brief
            Draws the variables, one block each, and an "Add" row at the end.

            \param message
            Set when something needs saying (a duplicate name on Add).

            \return
            True if any variable changed this frame.
        */
        bool Draw(Blackboard& variables, bool readOnly, std::string& message);

    private:

        char m_newName[64] = "";   // the Add row's text box

    };  // VARIABLESEDITOR CLASS

}   // NAMESPACE CRUCIBLE
