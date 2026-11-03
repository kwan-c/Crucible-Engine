/*************************************************************************
  \file       Prefab.h
  \author     Tok Duan Jie Nelson, Tan Chun Kwang Xavier
  \par        Email: t.duanjienelson@digipen.edu, chunkwangxavier.tan@digipen.edu
  \date       29 September 2026
  \version    3.1
  \brief
    A saved entity template - one entity and all its children - that can be
    stamped into a world repeatedly.
  \details
    A prefab is an entity subtree's components in a file. Unlike Scene it is
    additive - Instantiate() adds entities to whatever is already there and
    destroys nothing.

    A prefab deliberately carries no entity guids. It is a template, not an
    object: each instance is a set of distinct entities, which receive their
    own guids from Scene the next time the scene is saved. That is what lets
    the same prefab be dropped into a level twenty times.

    File layout (version 2):
    \code
        {
          "version": 2,
          "name": "Crate",
          "entities": [
            { "parent": -1, "components": { ... } },   // [0] is always the root
            { "parent":  0, "components": { ... } },
            { "parent":  1, "components": { ... } }
          ]
        }
    \endcode
    "parent" is an index into the same array, -1 for the root only. Entries
    are written depth-first, parents before children and children in sibling
    order, so loading is a single pass: each entity is appended to a parent
    that already exists, which reproduces the sibling order without storing
    it. Version 1 files (a single entity under "entity/components") still
    load, as a one-entity prefab.

    A loaded Prefab holds its parsed document, so instantiating the same
    prefab many times parses the file once.
  \dependencies
    Json/JsonWrapper.h, ECS/Core/ECSCore.h, <filesystem>, <string>, <vector>
  \see
    Prefab.cpp, Scene.h, EntityIO.h
  \history
    - 1.0 (29 September 2026): Initial version.
    - 2.0 (23 October 2026): Multi-entity prefabs. SaveFrom captures the
      whole subtree under the source entity; the root's position is zeroed
      on save (rotation and scale kept), so instances spawn at the origin
      or exactly on their parent. Instantiate takes an optional parent.
      Version 1 files still load. Prefab files use the .cprefab
      extension (kPrefabExtension).
    - 3.0 (25 October 2026): Every entry carries a stable "id". Instantiate
      links each new entity to the file (PrefabInstance, when the file was
      loaded from a registered asset); SaveFrom never writes that link into
      a file. Version 1 and 2 files still load, with positions as ids.
    - 3.1 (2 November 2026): Entity references inside a prefab (script
      Entity fields, PhysicsJoint connected body - see EntityRefs.h) are
      stored as the prefab ENTRY id they point at, not the source's scene
      id. SaveFrom translates them (given sceneIdOf) and clears references
      to entities outside the subtree; Instantiate translates them back to
      the new copies (given ensureSceneId). Both callbacks are optional, so
      older callers behave as before. Prefabs saved earlier keep scene ids
      until saved again.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <utility>

#include "ECS/Core/ECSCore.h"     // Entity, kNullEntity
#include "Json/JsonWrapper.h"
#include "EngineExport.h"

namespace Crucible {

    class World;

    /*************************************************************************
     \brief  Format version written into every .prefab file.
     \details
        1 - one entity under "entity/components".
        2 - an "entities" array with parent indices (see the file header).
        3 - each entry also has a stable "id" (16 hex digits) that instances
            link to (PrefabInstance::fileId). Version 1 and 2 files load with
            their positions as ids.
     *************************************************************************/
    inline constexpr int kPrefabVersion = 3;

    /*************************************************************************
     \brief  File extension of a prefab, including the dot.
     *************************************************************************/
    inline constexpr const char* kPrefabExtension = ".cprefab";

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // std::string / std::vector / JsonWrapper across the DLL boundary
#endif

    /*************************************************************************
     \brief
        One entity-subtree template, loaded once and instantiated many times.

     \par Usage
     \code
        // author one - saves crateRoot and everything under it
        Prefab::SaveFrom(world, crateRoot, assets.Resolve("Prefabs/Crate.cprefab"));

        // use it
        Prefab crate;
        if (crate.Load(assets.Resolve("Prefabs/Crate.cprefab")))
        {
            for (int i = 0; i < 20; ++i)
            {
                const Entity e = crate.Instantiate(world);   // returns the root
                world.GetComponent<Transform>(e)->positions.x = i * 2.f;
            }

            // or spawn one sitting exactly on another entity
            crate.Instantiate(world, someParent);
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
            Write an entity and all of its descendants out as a prefab file.

         \param[in] world  The world the entities live in.
         \param[in] root   The top entity to capture. Must be alive. It does
                           not need to be a root in the world - its own
                           parent is simply not saved.
         \param[in] file   Destination, e.g. "Prefabs/Crate.cprefab".
         \param[in] name   Name stored in the file. Defaults to the
                           filename without its extension.
         \param[out] outIds Optional: receives every saved entity with the
                           id its entry got, parents first.
         \param[in] sceneIdOf Optional: an entity's scene id (Scene::IdOf).
                           Given, entity references inside the subtree are
                           saved as the entry id of their target, and ones to
                           entities outside it are cleared (with a warning) -
                           a template can't point at one scene's objects.
                           Not given, references are saved as they are.
         \return true if the file was written.

         \details
            Captures the components as they are right now. The prefab does
            not stay linked to these entities - later changes to them do not
            change the file.

            The root's Transform position is written as zero; its rotation
            and scale are kept. Children keep their local Transforms
            unchanged, so the subtree's shape is preserved exactly.
         *********************************************************************/
        static bool SaveFrom(World& world,
                             Entity root,
                             const std::filesystem::path& file,
                             const std::string& name = std::string{},
                             std::vector<std::pair<Entity, std::uint64_t>>* outIds = nullptr,
                             const std::function<std::uint64_t(Entity)>& sceneIdOf = {});

        /*********************************************************************
         \brief  Read a prefab file into memory, ready to instantiate.
         \return true if the file parsed, is a version this build
                 understands, and its entity tree is well formed.
         *********************************************************************/
        bool Load(const std::filesystem::path& file);

        /*********************************************************************
         \brief  Same as Load, from text already in memory.
         \details
            Useful for duplicating entities in the Editor without going
            through disk.
         *********************************************************************/
        bool LoadFromString(const std::string& json);

        /*********************************************************************
         \brief  Same as Load(file), from the text of a registered prefab
                 file: copies made from it link back to that asset.
         \param[in] json        The .cprefab file's contents.
         \param[in] assetGuid   The file's asset guid (from its .meta).
         \param[in] contentHash AssetRegistry::HashFileContents of the file.
         \param[in] source      The file's path, for log messages; its stem
                                is the name when the file has none.
         \return true if the text parsed and its entity tree is well formed.
         \details
            What the ResourceManager uses (GetPrefab / ReadPrefab): the same
            result as Load(file), with the text read through the
            AssetManager by GUID instead of from a path.
         *********************************************************************/
        bool LoadFromAsset(const std::string& json, std::uint64_t assetGuid,
                           std::uint64_t contentHash, const std::string& source);

        /*********************************************************************
         \brief
            Add one copy of this prefab to a world.

         \param[in,out] world  The world to add to. Nothing existing is
                               destroyed or modified (except that parent
                               gains a child).
         \param[in]     parent Optional entity to attach the new root to.
                               kNullEntity (the default) leaves it a root.
                               The root's saved local Transform is kept, so
                               with a zeroed position it sits exactly on
                               the parent.
         \return
            The new root entity, or kNullEntity if this Prefab is not valid.

         \details
            When the file was Load()ed from a registered asset (it has a
            .meta), every new entity gets a PrefabInstance linking it back to
            this file and entry - that is what makes it an instance.

            ensureSceneId (optional, e.g. SceneManager::EnsureId): given, every
            entity reference that names one of this prefab's entries is
            pointed at the matching new copy (by that copy's scene id), so a
            copy's parts refer to each other, not to the original's. Not
            given, references are left as stored.

         \note
            Each component added is a structural change, so every new entity
            moves archetype once per component. Do not call this from inside
            a Query - use a CommandBuffer instead.
         *********************************************************************/
        Entity Instantiate(World& world, Entity parent = kNullEntity,
                           const std::function<std::uint64_t(Entity)>& ensureSceneId = {}) const;

        /*********************************************************************
         \brief  Points entity references that name one of this prefab's
                 entries at the matching entity of an instance.
         \param[in] entityForEntry  Entry index -> the instance's entity for
                                    it (kNullEntity if it has none).
         \param[in] sceneIdOf       That entity's scene id (EnsureId).
         \details
            The shared step behind Instantiate; the editor's prefab updates
            use it too. Returns a map for EntityRefs::Remap* - ids that are
            not this prefab's entry ids are returned unchanged.
         *********************************************************************/
        std::function<std::uint64_t(std::uint64_t)> EntryToSceneMap(
            const std::function<Entity(std::size_t)>& entityForEntry,
            const std::function<std::uint64_t(Entity)>& sceneIdOf) const;

        /*********************************************************************
         \brief  Whether a prefab has been successfully loaded.
         *********************************************************************/
        bool IsValid() const { return m_valid; }

        /*********************************************************************
         \brief  The prefab's name, as stored in the file.
         *********************************************************************/
        const std::string& Name() const { return m_name; }

        /*********************************************************************
         \brief  How many entities one instance creates (root included).
                 0 when not loaded.
         *********************************************************************/
        std::size_t EntityCount() const { return m_valid ? m_parents.size() : 0; }

        /*********************************************************************
         \brief  The loaded file's asset guid, read from its .meta - what
                 instances link to. 0 after LoadFromString, or for a file
                 with no .meta (Instantiate then makes unlinked entities).
         *********************************************************************/
        std::uint64_t AssetGuid() const { return m_asset; }

        /*********************************************************************
         \brief  Content hash of the loaded file (AssetRegistry::HashFileContents,
                 the same hash .meta files hold); 0 after LoadFromString.
                 Instantiate records it on the new root, so the editor can tell
                 when the file has changed since.
         *********************************************************************/
        std::uint64_t ContentHash() const { return m_hash; }

        /*********************************************************************
         \brief  Where the entry with this id is in the file: its index, or -1
                 (also -1 when nothing is loaded).
         *********************************************************************/
        int IndexOf(std::uint64_t fileId) const;

        /*********************************************************************
         \brief  The document path holding entry `index`'s components, e.g.
                 "entities/2/components" - what an instance is compared with.
         *********************************************************************/
        std::string ComponentsPath(std::size_t index) const;

        /*********************************************************************
         \brief  The parsed file, read-only.
         *********************************************************************/
        const JsonWrapper& Document() const { return m_doc; }

        /*********************************************************************
         \brief  Entry `index`'s stable id / parent index (-1 for the root),
                 and the loaded file's format version.
         *********************************************************************/
        std::uint64_t IdAt(std::size_t index) const { return index < m_ids.size() ? m_ids[index] : 0; }
        int ParentAt(std::size_t index) const { return index < m_parents.size() ? m_parents[index] : -1; }
        int Version() const { return m_version; }

    private:
        /*********************************************************************
         \brief  Shared tail of Load / LoadFromString: version check and
                 tree validation of the already-parsed m_doc.
         \param[in] source       Shown in log messages (file path or "text").
         \param[in] fallbackName Used when the file stores no name.
         *********************************************************************/
        bool Validate(const std::string& source, const std::string& fallbackName);

        JsonWrapper      m_doc;
        std::string      m_name;
        int              m_version = 0;   //!< version of the loaded file (1 or 2)
        std::vector<int> m_parents;       //!< m_parents[i] = parent index of entity i, -1 for the root
        std::vector<std::uint64_t> m_ids; //!< m_ids[i] = entry i's stable id (PrefabInstance::fileId)
        std::uint64_t    m_asset = 0;     //!< the file's asset guid, from its .meta (0 = none)
        std::uint64_t    m_hash = 0;      //!< content hash of the loaded file (0 = from text)
        bool             m_valid = false;
    };

#ifdef _MSC_VER
#pragma warning(pop)
#endif

}
