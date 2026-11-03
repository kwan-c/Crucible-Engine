/*************************************************************************
* \file:         Blackboard.cpp
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.0
* \brief
*                Blackboard's lookup and setting by name.
*
* \history
*    -v1.0       first version
*
**************************************************************************/
#include "Blackboard.h"

namespace Crucible {

    const BlackboardVariable* Blackboard::Find(std::string_view name) const {

        for (const BlackboardVariable& variable : m_variables) {
            if (variable.name == name) return &variable;
        }
        return nullptr;

    }   // Blackboard::Find()

    void Blackboard::Set(std::string_view name, BlackboardValue value) {

        for (BlackboardVariable& variable : m_variables) {
            if (variable.name == name) { variable.value = std::move(value); return; }
        }
        m_variables.push_back({ std::string(name), std::move(value) });

    }   // Blackboard::Set()

}   // NAMESPACE CRUCIBLE
