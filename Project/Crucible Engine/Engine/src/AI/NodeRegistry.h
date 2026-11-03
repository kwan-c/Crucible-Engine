/*************************************************************************
* \file:         NodeRegistry.h
* \author:       Hugo Low Ren Hao
* \par:          Email: low.h@digipen.edu
* \date          3 November 2026
* \version       1.1
* \brief
*                NodeDescription: a type's id, name, category and
*                description (Unity Behavior's [NodeDescription]).
*                TypeRegistry: every type of one kind the engine knows, by
*                id - NodeRegistry for behaviour-tree nodes; the same
*                registry serves transition conditions (TransitionCondition.h).
*
* \details
*                The editor's "Add Node" menu lists a registry; loading a
*                saved graph asks it to make an object from its id. A type
*                joins with one line in its .cpp, inside namespace Crucible:
*
*                    CRUCIBLE_REGISTER_BEHAVIOR_NODE(WaitNode)
*
*                The id is what saved graphs store, so it must never change
*                once graphs use it - the display name can.
*
* \history
*    -v1.0       first version: NodeRegistry for nodes
*    -v1.1       one TypeRegistry template, so conditions reuse it
*
**************************************************************************/
#pragma once

// STL
#include <memory>
#include <string_view>
#include <vector>

// RTTR - an object's fields are reached through its registered type (saving, editor)
#include <rttr/instance.h>
#include <rttr/type.h>

// Crucible engine
#include "Diagnostics/Logger.h"
#include "EngineExport.h"

namespace Crucible {

    class BehaviorNode;

    struct NodeDescription {

        std::string_view id;            // stable: what saved graphs store ("wait"). Never rename
        std::string_view name;          // shown on the node and in the menu ("Wait")
        std::string_view category;      // the menu folder ("Actions", "Flow", "Events")
        std::string_view description;   // the menu's tooltip

    };  // NODEDESCRIPTION STRUCT

    // One registered type, of kind Base.
    template <typename Base>
    struct TypeEntry {

        const NodeDescription* description = nullptr;
        std::unique_ptr<Base> (*create)() = nullptr;      // a new one with default settings
        rttr::instance (*reflect)(Base& object) = nullptr; // the object as its real type, so RTTR sees its fields
        rttr::type type;                                  // its RTTR type: the fields to save and edit
        const char* sourceFile = "";                      // the .cpp it was registered in (__FILE__): the editor's "Inspect Script"

    };  // TYPEENTRY STRUCT

    template <typename Base>
    class TypeRegistry {

    public:

        // The one registry of this kind. Defined in NodeRegistry.cpp, inside
        // Engine.dll, and exported (the specialisations below): if it lived in
        // this header, the Editor's .exe would get its own empty copy of the
        // static, and its "Add Node" menu would list nothing. A function-local
        // static there, so it exists the first time anyone asks - even from
        // another file's static registration, whose order C++ leaves unspecified.
        static TypeRegistry& Instance();

        /*!
            \brief
            Adds type T. T must have `static constexpr NodeDescription
            kDescription` and a default constructor.

            \return
            False (and an error in the log) if another type already took the
            id - two types with one id would make saved graphs ambiguous.
        */
        template <typename T>
        bool Register(const char* sourceFile = "") {
            if (Find(T::kDescription.id)) {
                CRU_ERROR("Behavior: two types share the id '{}' - '{}' was not registered", T::kDescription.id, T::kDescription.name);
                return false;
            }
            m_entries.push_back(TypeEntry<Base>{
                &T::kDescription,
                [] () -> std::unique_ptr<Base> { return std::make_unique<T>(); },
                [] (Base& object) { return rttr::instance(static_cast<T&>(object)); },
                rttr::type::get<T>(),
                sourceFile
            });
            return true;
        }

        // The type registered under `id`, or null.
        const TypeEntry<Base>* Find(std::string_view id) const {
            for (const TypeEntry<Base>& entry : m_entries) {
                if (entry.description->id == id) return &entry;
            }
            return nullptr;
        }

        // Every registered type, in registration order.
        const std::vector<TypeEntry<Base>>& All() const { return m_entries; }

    private:

        TypeRegistry() = default;

        std::vector<TypeEntry<Base>> m_entries;

    };  // TYPEREGISTRY CLASS

    using NodeRegistry  = TypeRegistry<BehaviorNode>;
    using NodeTypeEntry = TypeEntry<BehaviorNode>;

    template <> ENGINE_API NodeRegistry& NodeRegistry::Instance();

}   // NAMESPACE CRUCIBLE

// Registers a node type when the engine loads: a file-scope constant whose
// initialiser does the registering. Put it in the node's .cpp, once, inside
// namespace Crucible (where the node's name is visible).
#define CRUCIBLE_REGISTER_BEHAVIOR_NODE(Type) \
    namespace { [[maybe_unused]] const bool kRegistered_##Type = ::Crucible::NodeRegistry::Instance().Register<Type>(__FILE__); }
