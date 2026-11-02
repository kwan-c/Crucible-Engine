/*************************************************************************
  \file       Scene.h
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       29 September 2026
  \version    1.1
  \brief
    Saves a World to a .scene file and loads one back.
  \details
    A scene file is a list of entities, each with the components it had.
    Saving walks every archetype; loading clears the world and rebuilds it.

    Each entity carries a guid in the file. That guid is NOT stored on the
    entity itself - this class keeps the entity-to-guid mapping for as long
    as it is alive, which is why the Editor should hold one Scene object per
    open scene rather than constructing a throwaway on every save.

    The reason the guid exists at all is file stability. Entities are stored
    grouped by archetype, so adding one component to one entity moves it to
    a different archetype and therefore to a different position in the walk.
    Without a stable key to sort by, saving an unchanged scene twice could
    produce two different files, and every scene commit would conflict.
    Entities are written sorted by guid, so an unchanged scene saves
    byte-identical.
  \dependencies
    ECS/World.h, ECS/Core/ECSCore.h, xresource_guid.h, <filesystem>,
    <string>, <unordered_map>
  \see
    Scene.cpp, Prefab.h, EntityIO.h
  \history
    - 1.0 (29 September 2026): Initial version.
    - 1.1 (2 October 2026): In-memory snapshots for play mode
      (CaptureSnapshot / RestoreSnapshot) and EntityOf, the guid -> Entity
      lookup, as a separate section of Scene.cpp. Save and Load are
      unchanged.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>

#include <xresource_guid.h>

#include "ECS/Core/ECSCore.h"     // Entity
#include "EngineExport.h"

namespace Crucible {

    class World;
    class JsonWrapper;

    /*************************************************************************
     \brief  An in-memory copy of a scene, taken by Scene::CaptureSnapshot.
     \details
        The same JSON a .scene file holds (compact), in the same layout Save
        writes. Play mode keeps one from Play to Stop. Nothing is written
        to disk.
     *************************************************************************/
    struct SceneSnapshot
    {
        std::string document;          //!< compact .scene JSON
        std::size_t entityCount = 0;   //!< entities in document, checked at capture

        //! False for a default-constructed snapshot (nothing captured).
        bool IsValid() const noexcept { return !document.empty(); }
    };

    /*************************************************************************
     \brief  Format version written into every .scene file.
     \details
        Bump this when the layout changes in a way older files cannot be
        read as. Load refuses a file from a newer build rather than
        misreading it.
     *************************************************************************/
    inline constexpr int kSceneVersion = 1;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // std::string / std::unordered_map across the DLL boundary
#endif

    /*************************************************************************
     \brief
        One scene: save a World out, load a World back.

     \par Usage
     \code
        Scene scene;

        // save
        scene.SetName("level01");
        scene.Save(world, assets.Resolve("Scenes/level01.scene"));

        // load - this destroys everything currently in the world
        scene.Load(world, assets.Resolve("Scenes/level01.scene"));
     \endcode

     \note
        Nothing here throws. A missing or corrupt file logs through
        CRU_ERROR and returns false, matching AssetManager. On a failed
        Load the world is left untouched - the file is fully validated
        before any entity is destroyed.
     *************************************************************************/
    class ENGINE_API Scene
    {
    public:
        /*********************************************************************
         \brief
            Write every live entity in the world to a file.

         \param[in] world The world to save. Taken by non-const reference
                          only because World::Archetypes() is non-const; no
                          component is modified.
         \param[in] file  Full path, e.g. assets.Resolve("Scenes/a.scene").
         \return true if the file was written.

         \details
            Creates the containing folder if needed. Entities are written
            sorted by guid, so re-saving an unchanged scene produces an
            identical file and an empty git diff.
         *********************************************************************/
        bool Save(World& world, const std::filesystem::path& file);

        /*********************************************************************
         \brief
            Replace the world's contents with a scene file.

         \param[in,out] world The world to fill.
         \param[in]     file  The .scene file to read.
         \return true if the file was read and the world rebuilt.

         \details
            Calls World::DestroyAllEntities() first, so every entity handle
            held anywhere becomes stale. The file is parsed and
            version-checked before anything is destroyed, so a bad file
            leaves the world exactly as it was.

         \note
            For adding one object to the existing world rather than
            replacing everything, use Prefab::Instantiate.
         *********************************************************************/
        bool Load(World& world, const std::filesystem::path& file);

        /*********************************************************************
         \brief
            Replace the world's contents with a scene document already in
            memory - the text of a .scene file.

         \param[in,out] world  The world to fill.
         \param[in]     text   The .scene file's contents.
         \param[in]     source Where the text came from, for log messages
                               (normally the file's path). Its stem is the
                               scene's name when the document has none.
         \return true if the text parsed and the world was rebuilt.

         \details
            Load(world, file) without the file read: the same checks, and on
            failure the world is left exactly as it was. This is how scenes
            load through the ResourceManager (SceneManager::RequestLoadAsset):
            the ResourceManager reads the file through the AssetManager by
            the scene's GUID, and the world is built from that text.
         *********************************************************************/
        bool LoadFromText(World& world, const std::string& text, const std::filesystem::path& source);

        /*********************************************************************
         \brief
            Save the world into memory instead of a file.

         \param[in]  world The world to copy. Non-const for the same reason
                           as Save; nothing is modified.
         \param[out] out   Replaced on success, untouched on failure.
         \return true if the snapshot was built and checked.

         \details
            Exactly what Save would write, including edits never saved. Like
            Save, every live entity gets a guid (an unsaved one gets a new
            guid, which later Saves keep). The text is parsed back and
            checked before this returns, so RestoreSnapshot can rely on it.
         *********************************************************************/
        bool CaptureSnapshot(World& world, SceneSnapshot& out);

        /*********************************************************************
         \brief
            Replace the world's contents with a snapshot.

         \param[in,out] world    The world to rebuild.
         \param[in]     snapshot From CaptureSnapshot.
         \return true if the world was rebuilt.

         \details
            The snapshot is parsed and checked before anything is destroyed,
            so a bad one leaves the world exactly as it was. Every entity
            gets back the guid it had at capture, and the scene its name -
            but NOT its Entity handle: all handles held anywhere are stale
            afterwards. Find an entity again with EntityOf(guid).
         *********************************************************************/
        bool RestoreSnapshot(World& world, const SceneSnapshot& snapshot);

        /*********************************************************************
         \brief  Drop the entity-to-guid mapping.
         \details
            Call when the world has been reset behind this object's back.
            The next Save then issues fresh guids for everything.
         *********************************************************************/
        void Forget();

        /*********************************************************************
         \brief  The scene's name, as stored in the file.
         *********************************************************************/
        const std::string& Name() const { return m_name; }

        /*********************************************************************
         \brief  Set the name written into the file on the next Save.
         *********************************************************************/
        void SetName(std::string name) { m_name = std::move(name); }

        /*********************************************************************
         \brief  The guid an entity will be saved under.
         \return The guid, or a zero guid if this scene has not seen that
                 entity yet (it gets one on the next Save).
         *********************************************************************/
        xresource::instance_guid IdOf(Entity entity) const;

        /*********************************************************************
         \brief  The guid an entity will be saved under, issuing one now if
                 it has none yet.
         \return The guid - never zero for a valid entity.
         \details
            For anything that stores a link to another entity before the
            scene has been saved (the Inspector's entity picker, e.g. a
            PhysicsJoint's connected body). Save keeps every guid it already
            knows, so the one issued here is the one written to the file.
         *********************************************************************/
        xresource::instance_guid EnsureId(Entity entity);

        /*********************************************************************
         \brief  The entity saved under a guid - the reverse of IdOf.
         \return The entity, or kNullEntity if this scene has no entity with
                 that guid (or the guid is zero).
         \details
            How a guid remembered before a reload or a play-mode Stop finds
            its entity's new handle. A search of the guid map: fine for the
            few lookups a Stop makes, not meant for per-frame use.
         *********************************************************************/
        Entity EntityOf(const xresource::instance_guid& id) const;

        /*********************************************************************
         \brief  How many entities this scene currently has guids for.
         *********************************************************************/
        std::size_t TrackedCount() const { return m_ids.size(); }

    private:
        /*********************************************************************
         \brief  The shared part of Load and LoadFromText: everything after
                 the document has been read.
         \param[in] source For log messages and the fallback scene name.
         *********************************************************************/
        bool LoadDocument(World& world, const JsonWrapper& doc, const std::filesystem::path& source);

        /*********************************************************************
         \brief  Hashes an Entity by both its index and its generation.
         \note
            Generation matters: a destroyed entity's index gets recycled,
            and a recycled slot must not inherit the dead entity's guid.
         *********************************************************************/
        struct EntityHash
        {
            std::size_t operator()(Entity entity) const noexcept
            {
                return (static_cast<std::size_t>(entity.generation) << 32)
                     ^  static_cast<std::size_t>(entity.index);
            }
        };

        std::string m_name{ "Untitled" };
        std::unordered_map<Entity, xresource::instance_guid, EntityHash> m_ids;
    };

#ifdef _MSC_VER
#pragma warning(pop)
#endif

}
