/*************************************************************************
  \file       Serialization.h
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       29 September 2026
  \version    1.0
  \brief
    Generic conversion between any RTTR-registered type and JSON.
  \details
    This is the one piece of serialization code in the engine that has to
    be written. Everything else - components, materials, engine config -
    gets saved and loaded by registering the type with RTTR and calling
    these functions.

    Nothing here names a single concrete type. The functions walk whatever
    properties RTTR reports, so adding a fifth component next month needs
    an rttr::registration block and no change to this file at all.

    A property tagged with MetaDataTypes::DO_NOT_SERIALIZE is skipped, so
    derived or runtime-only data (cached matrices, GPU handles) stays out
    of the file.
  \dependencies
    ReflectionRegistration.h, rapidjson/document.h, <string>
  \see
    Serialization.cpp, ReflectionRegistration.h, Json/JsonWrapper.h
  \history
    - 1.0 (29 September 2026): Initial version. Structure follows the
      rapidjson example in the RTTR repository (github.com/rttrorg/rttr),
      adapted to Crucible conventions and DO_NOT_SERIALIZE support.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#pragma once

#include <string>

#include <rapidjson/document.h>

#include "Reflection/ReflectionRegistration.h"
#include "EngineExport.h"

namespace Crucible::Reflection {

    /*************************************************************************
     \brief
        Convert any RTTR-registered object into a JSON string.

     \param[in]
        object The object to write. Pass it directly - rttr::instance is
        constructed implicitly, so ToJson(myTransform) works.
     \param[in]
        prettyPrint true for indented output that a human can read and git
        can diff line by line; false for the compact form.
     \return
        The JSON text, or "{}" if the object's type is not registered.

     \par Usage
     \code
        Transform t;
        t.positions = { 1.f, 2.f, 3.f };
        const std::string json = Reflection::ToJson(t);
     \endcode

     \note
        Leave prettyPrint on for anything committed to git. A compact scene
        file is one enormous line, which turns every merge into a conflict.
     *************************************************************************/
    ENGINE_API std::string ToJson(rttr::instance object, bool prettyPrint = true);

    /*************************************************************************
     \brief
        Fill an existing object from a JSON string.

     \param[in]
        json The JSON text.
     \param[out]
        object The object to write into. Must already exist; this fills its
        properties rather than creating anything.
     \return
        true if the text parsed and at least the object's own properties
        were applied.

     \details
        Properties present in the object but missing from the JSON keep
        whatever value they already had, so a file written by an older
        build still loads - the new fields simply keep their defaults.
        Properties in the JSON that the object does not have are ignored.

     \note
        Never throws. A malformed file logs through CRU_ERROR and returns
        false, matching AssetManager rather than JsonReader.
     *************************************************************************/
    ENGINE_API bool FromJson(const std::string& json, rttr::instance object);

    /*************************************************************************
     \brief
        Write an object into an existing rapidjson value.

     \param[in]
        object The object to write.
     \param[out]
        outValue Receives the object, as a JSON object value.
     \param[in]
        allocator The owning document's allocator.

     \details
        The building block JsonWrapper uses to place a whole component
        inside a larger scene document without serialising to text and
        re-parsing it.
     *************************************************************************/
    ENGINE_API void ToJsonValue(rttr::instance object,
                                rapidjson::Value& outValue,
                                rapidjson::Document::AllocatorType& allocator);

    /*************************************************************************
     \brief
        Fill an object from an existing rapidjson value.

     \param[in]
        value The JSON object to read from.
     \param[out]
        object The object to write into.
     \return
        true if the value was an object and its properties were applied.

     \note
        Never throws; see FromJson.
     *************************************************************************/
    ENGINE_API bool FromJsonValue(const rapidjson::Value& value, rttr::instance object);

}
