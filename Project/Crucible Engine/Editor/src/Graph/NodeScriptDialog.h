/*************************************************************************
* \file:         NodeScriptDialog.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          5 November 2026
* \version       1.0
* \brief
*                "Create new Action / Modifier / Composite / Condition":
*                Unity Behavior's way to make a node of your own. Asks for a
*                name, story, category and description, writes the C# script
*                from a template (Unity's named [NodeDescription] style), and
*                opens it.
*
* \details
*                The script goes in Assets/Scripts/AI/ (only Assets/Scripts
*                is compiled into Crucible.Scripts.dll), from
*                ManagedScripts/Templates/<Kind>.cs.txt. "Attack Player"
*                becomes class AttackPlayerAction with the file id
*                "attack_player" - the id is written into the script, so
*                renaming the class later doesn't break graphs that use it.
*
*                The Editor's script hot reload compiles the new file by
*                itself; once it has, the node appears in the catalog. The
*                caller waits for that (Draw returns the id to wait for)
*                and then places the node.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#pragma once

// STL
#include <string>

namespace Crucible {

    class NodeScriptDialog {

    public:

        enum class Kind { Action, Modifier, Composite, Condition };

        // Shows the dialog on the next Draw.
        void Open(Kind kind);

        // Draws it while open. Returns the new node's id the frame its
        // script is written, "" otherwise.
        std::string Draw();

    private:

        bool Create(std::string& id);   // writes the script; false with m_error set

        Kind        m_kind = Kind::Action;
        bool        m_openRequested = false;
        char        m_name[64]         = {};
        char        m_category[64]     = {};
        char        m_description[256] = {};
        char        m_story[128]       = {};   // Unity's story sentence: "[Agent] attacks [Target]"
        std::string m_error;

    };  // NODESCRIPTDIALOG CLASS

}   // NAMESPACE CRUCIBLE
