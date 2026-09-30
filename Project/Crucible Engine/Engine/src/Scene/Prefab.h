/*************************************************************************
  \file       Prefab.h
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       29 September 2026
  \version    1.0
  \brief
    A saved entity template that can be stamped into a world repeatedly.
  \details
    A prefab is one entity's components in a file. Unlike Scene it is
    additive - Instantiate() adds an entity to whatever is already there
    and destroys nothing.

    A prefab deliberately carries no entity guid. It is a template, not an
    object: each instance is a distinct entity and receives its own guid
    from Scene the next time the scene is saved. That is what lets the same
    prefab be dropped into a level twenty times.

    A loaded Prefab holds its parsed document, so instantiating the same
    prefab many times parses the file once.
  \dependencies
    Json/JsonWrapper.h, ECS/Core/ECSCore.h, <filesystem>, <string>
  \see
    Prefab.cpp, Scene.h, EntityIO.h
  \history
    - 1.0 (29 September 2026): Initial version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#pragma once

#include <filesystem>
#include <string>

#include "ECS/Core/ECSCore.h"     // Entity, kNullEntity
#include "Json/JsonWrapper.h"
#include "EngineExport.h"

namespace Crucible {

    class World;

    /*************************************************************************
     \brief  Format version written into every .prefab file.
     *************************************************************************/
    inline constexpr int kPrefabVersion = 1;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // std::string / JsonWrapper across the DLL boundary
#endif

    /*************************************************************************
     \brief
        One entity template, loaded once and instantiated many times.

     \par Usage
     \code
        // author one
        Prefab::SaveFrom(world, someEntity, assets.Resolve("Prefabs/Crate.prefab"));

        // use it
        Prefab crate;
        if (crate.Load(assets.Resolve("Prefabs/Crate.prefab")))
        {
            for (int i = 0; i < 20; ++i)
            {
                const Entity e = crate.Instantiate(world);
                world.GetComponent<Transform>(e)->positions.x = i * 2.f;
            }
        }
     \endcode

     \note
        Nothing here throws. A missing or corrupt file logs and leaves the
        Prefab invalid; Instantiate on an invalid Prefab returns
        kNullEntity without touching the world.
     *************************************************************************/
    class ENGINE_API Prefab
    {
    public:
        /*********************************************************************
         \brief
            Write one entity out as a prefab file.

         \param[in] world  The world the entity lives in.
         \param[in] source The entity to capture. Must be alive.
         \param[in] file   Destination, e.g. "Prefabs/Crate.prefab".
         \param[in] name   Name stored in the file. Defaults to the
                           filename without its extension.
         \return true if the file was written.

         \details
            Captures the entity's components as they are right now. The
            prefab does not stay linked to that entity - later changes to
            it do not change the file.
         *********************************************************************/
        static bool SaveFrom(World& world,
                             Entity source,
                             const std::filesystem::path& file,
                             const std::string& name = std::string{});

        /*********************************************************************
         \brief  Read a prefab file into memory, ready to instantiate.
         \return true if the file parsed and is a prefab this build understands.
         *********************************************************************/
        bool Load(const std::filesystem::path& file);

        /*********************************************************************
         \brief  Same as Load, from text already in memory.
         \details
            Useful for duplicating an entity in the Editor without going
            through disk.
         *********************************************************************/
        bool LoadFromString(const std::string& json);

        /*********************************************************************
         \brief
            Add one copy of this prefab to a world.

         \param[in,out] world The world to add to. Nothing existing is
                              destroyed or modified.
         \return
            The new entity, or kNullEntity if this Prefab is not valid.

         \note
            Each component added is a structural change, so the new entity
            moves archetype once per component. Do not call this from
            inside a Query - use a CommandBuffer instead.
         *********************************************************************/
        Entity Instantiate(World& world) const;

        /*********************************************************************
         \brief  Whether a prefab has been successfully loaded.
         *********************************************************************/
        bool IsValid() const { return m_valid; }

        /*********************************************************************
         \brief  The prefab's name, as stored in the file.
         *********************************************************************/
        const std::string& Name() const { return m_name; }

    private:
        JsonWrapper m_doc;
        std::string m_name;
        bool        m_valid = false;
    };

#ifdef _MSC_VER
#pragma warning(pop)
#endif

}
