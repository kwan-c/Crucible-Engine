/*************************************************************************
  \file       JsonWrapper.h
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       29 September 2026
  \version    1.0
  \brief
    A JSON document addressed by slash-separated paths.
  \details
    Serialization.h converts one whole object at a time. This class is the
    layer above it: a document you can reach into at any depth with a
    single string, so scene and prefab code reads like what it means.

        doc.Set("entities/0/name", std::string("Player"));
        doc.SetObject("entities/0/components/Transform", transform);
        auto count = doc.GetListSize("entities");

    Missing containers along a path are created on the way in, so writing
    a deep value never needs the intermediate objects built by hand. A
    numeric segment indexes an array; anything else is an object member.
  \dependencies
    Serialization.h, rapidjson/document.h, <filesystem>, <string>, <vector>
  \see
    JsonWrapper.cpp, JsonWrapper.tpp, Reflection/Serialization.h
  \history
    - 1.0 (29 September 2026): Initial version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <rapidjson/document.h>

#include "Reflection/Serialization.h"
#include "EngineExport.h"

namespace Crucible {

    /*************************************************************************
     \brief
        A JSON document with path-based access.

     \details
        Wraps a rapidjson::Document. Reads never throw and never create
        anything - a missing path returns the fallback. Writes create
        whatever containers the path needs.

     \par Usage
     \code
        JsonWrapper doc;
        doc.SetObject("components/Transform", myTransform);
        doc.Set("version", 1);
        doc.SaveToFile(assets.Resolve("Scenes/level01.scene"));

        JsonWrapper in;
        if (in.ReadFromFile(path))
        {
            Transform t;
            in.GetObject("components/Transform", t);
        }
     \endcode

     \note
        Every public function returns a value or a bool. Nothing here
        throws, matching AssetManager - a corrupt scene file must not be
        able to bring the engine down.
     *************************************************************************/
    class ENGINE_API JsonWrapper
    {
    public:
        /*********************************************************************
         \brief  Construct an empty JSON object, ready to write into.
         *********************************************************************/
        JsonWrapper();

        /*********************************************************************
         \brief  Discard the contents and start again from an empty object.
         *********************************************************************/
        void CreateEmpty();

        // ===================================================================
        //  LOADING AND SAVING
        // ===================================================================

        /*********************************************************************
         \brief  Replace the contents with a file's.
         \param[in] path Full path to the .json / .scene / .cprefab file.
         \return true if the file was read and parsed.
         \note   Logs and returns false on a parse error; never throws.
         *********************************************************************/
        bool ReadFromFile(const std::filesystem::path& path);

        /*********************************************************************
         \brief  Replace the contents with parsed text.
         \return true if the text parsed as JSON.
         *********************************************************************/
        bool ReadFromString(const std::string& json);

        /*********************************************************************
         \brief  Write the document to a file, creating folders as needed.
         \param[in] path   Destination.
         \param[in] pretty Indented output. Leave on for anything in git.
         \return true if the file was written.
         *********************************************************************/
        bool SaveToFile(const std::filesystem::path& path, bool pretty = true) const;

        /*********************************************************************
         \brief  The document as JSON text.
         *********************************************************************/
        std::string ToString(bool pretty = true) const;

        // ===================================================================
        //  QUERYING - none of this changes the document
        // ===================================================================

        /*********************************************************************
         \brief  Whether anything exists at a path.
         \param[in] path Slash-separated, e.g. "entities/0/name".
         *********************************************************************/
        bool Has(const std::string& path) const;

        /*********************************************************************
         \brief  How many elements the array at a path holds.
         \return The count, or 0 if the path is missing or not an array.
         *********************************************************************/
        std::size_t GetListSize(const std::string& path) const;

        /*********************************************************************
         \brief  Every member name of the object at a path.
         \details
            What a scene loader iterates to find which components an entity
            has, without knowing the names in advance.
         *********************************************************************/
        std::vector<std::string> GetMemberNames(const std::string& path) const;

        // ===================================================================
        //  READING VALUES
        // ===================================================================

        /*********************************************************************
         \brief
            Read a single value.

         \param[in] path     Where to look.
         \param[in] fallback Returned when the path is missing or the value
                             is not convertible to T.
         \return The value, or the fallback.

         \details
            T may be bool, any integer, float, double or std::string.

         \note
            A wrong type is not an error here - it returns the fallback. A
            scene half-edited by hand should load with sensible defaults
            rather than refuse to open.
         *********************************************************************/
        template<typename T>
        T Get(const std::string& path, const T& fallback = T{}) const;

        /*********************************************************************
         \brief  Read an array of values.
         \return The elements, or an empty vector if the path is not an array.
         *********************************************************************/
        template<typename T>
        std::vector<T> GetList(const std::string& path) const;

        /*********************************************************************
         \brief
            Read a whole RTTR-registered object from a path.

         \param[in]  path   Where the object sits.
         \param[out] object The object to fill. Must already exist.
         \return true if an object was found there and applied.

         \details
            Properties absent from the file keep their current values, so
            an older scene still loads into a component that has gained
            fields since.
         *********************************************************************/
        bool GetObject(const std::string& path, rttr::instance object) const;

        // ===================================================================
        //  WRITING VALUES - containers along the path are created as needed
        // ===================================================================

        /*********************************************************************
         \brief  Write a single value, replacing anything already there.
         *********************************************************************/
        template<typename T>
        void Set(const std::string& path, const T& value);

        /*********************************************************************
         \brief  Write an array of values.
         *********************************************************************/
        template<typename T>
        void SetList(const std::string& path, const std::vector<T>& values);

        /*********************************************************************
         \brief  Write a whole RTTR-registered object at a path.
         \note
            Properties tagged DO_NOT_SERIALIZE are left out - see
            Serialization.h.
         *********************************************************************/
        void SetObject(const std::string& path, rttr::instance object);

        /*********************************************************************
         \brief  Make sure an empty object exists at a path.
         \details
            For keeping a file's shape constant: an entity with no
            components still gets "components": {} rather than the key
            being absent. Leaves an existing object alone.
         *********************************************************************/
        void EnsureObject(const std::string& path);

        /*********************************************************************
         \brief  Make sure an empty array exists at a path.
         \details
            As EnsureObject. A scene with no entities still gets
            "entities": [].
         *********************************************************************/
        void EnsureArray(const std::string& path);

        /*********************************************************************
         \brief  Append an empty object to the array at a path, creating the
                 array if needed.
         \return The index of the new element, for building the next path.
         \details
            The usual way to add an entity: PushObject("entities") then
            write into "entities/<index>/...".
         *********************************************************************/
        std::size_t PushObject(const std::string& path);

        /*********************************************************************
         \brief  Delete whatever is at a path.
         \return true if something was removed.
         *********************************************************************/
        bool Remove(const std::string& path);

        // ===================================================================
        //  ESCAPE HATCH
        // ===================================================================

        /*********************************************************************
         \brief  The underlying document, for anything this class does not
                 cover.
         *********************************************************************/
        rapidjson::Document&       GetDocument()       { return m_document; }
        const rapidjson::Document& GetDocument() const { return m_document; }

    private:
        /*********************************************************************
         \brief  Resolve a path for reading.
         \return The value, or nullptr if any segment is missing.
         *********************************************************************/
        const rapidjson::Value* Find(const std::string& path) const;

        /*********************************************************************
         \brief
            Resolve a path for writing, creating what is missing.

         \details
            A segment made only of digits creates or indexes an array;
            anything else creates or indexes an object member. So
            "entities/0/name" builds an array under "entities" whose first
            element is an object with a "name".
         *********************************************************************/
        rapidjson::Value* FindOrCreate(const std::string& path);

        /*********************************************************************
         \brief  Split a path on '/', discarding empty segments.
         *********************************************************************/
        static std::vector<std::string> SplitPath(const std::string& path);

        rapidjson::Document m_document;
    };

}

#include "Json/JsonWrapper.tpp"
