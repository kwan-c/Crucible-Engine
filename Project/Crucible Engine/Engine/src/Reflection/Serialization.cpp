/*************************************************************************
  \file       Serialization.cpp
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       29 September 2026
  \version    1.0
  \brief
    Generic RTTR to JSON conversion.
  \details
    Two recursive walkers, one in each direction.

    Writing: ToJsonValue asks RTTR for the object's properties and, for
    each one, works out whether the value is a plain number, an enum, a
    sequence, a map, or another registered class - recursing in the last
    case.

    Reading: FromJsonValue matches each JSON member against a property of
    the same name and converts it back. Names that do not match on either
    side are skipped rather than treated as errors, which is what lets an
    old scene file load into a build that has gained new fields.

    Exceptions are used internally where recursion makes error codes
    unbearable, but every public function catches them and returns a bool,
    so a corrupt file can never bring the engine down.
  \dependencies
    Serialization.h, Diagnostics/Logger.h, rapidjson, <string>
  \see
    Serialization.h
  \history
    - 1.0 (29 September 2026): Initial version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#include "Reflection/Serialization.h"

#include <string>

#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "Diagnostics/Logger.h"

using namespace rapidjson;
using namespace rttr;

namespace {

    using Allocator = Document::AllocatorType;

    // Forward declarations - the two directions are mutually recursive.
    void WriteInstance(const instance& object, Value& outValue, Allocator& allocator);
    void ReadInstance(const Value& value, const instance& object);

    /*************************************************************************
     \brief
        Whether a property was tagged to stay out of saved files.

     \param[in]
        prop The property to test.
     \return
        true when MetaDataTypes::DO_NOT_SERIALIZE is set on it.

     \details
        Use it for anything the engine recomputes: cached transform
        matrices, GPU handles, resolved pointers. Saving those makes files
        bigger, noisier in git, and wrong the moment the derivation changes.
     *************************************************************************/
    bool IsSkipped(const property& prop)
    {
        const variant meta = prop.get_metadata(MetaDataTypes::DO_NOT_SERIALIZE);
        return meta.is_valid() && meta.to_bool();
    }

    /*************************************************************************
     \brief
        True if the property is only read from files (MetaDataTypes::LOAD_ONLY):
        an older format's key, upgraded by its setter. Writing skips it.
     *************************************************************************/
    bool IsLoadOnly(const property& prop)
    {
        const variant meta = prop.get_metadata(MetaDataTypes::LOAD_ONLY);
        return meta.is_valid() && meta.to_bool();
    }

    /*************************************************************************
     \brief
        Write a number, bool, string or enum into a JSON value.

     \param[in]
        valueType The declared type of the value.
     \param[in]
        var The value itself.
     \param[out]
        outValue Receives the result on success.
     \param[in]
        allocator The owning document's allocator.
     \return
        true if the type was one this function handles.

     \note
        Enums are written by name, not by number. A name survives someone
        inserting a new enumerator in the middle; an index does not, and
        would silently turn every saved Circle into a Square.
     *************************************************************************/
    bool WriteAtomic(const type& valueType, const variant& var, Value& outValue, Allocator& allocator)
    {
        if (valueType.is_arithmetic())
        {
            if      (valueType == type::get<bool>())               outValue.SetBool(var.to_bool());
            else if (valueType == type::get<char>())               outValue.SetBool(var.to_bool());
            else if (valueType == type::get<int8_t>())             outValue.SetInt(var.to_int8());
            else if (valueType == type::get<int16_t>())            outValue.SetInt(var.to_int16());
            else if (valueType == type::get<int32_t>())            outValue.SetInt(var.to_int32());
            else if (valueType == type::get<int64_t>())            outValue.SetInt64(var.to_int64());
            else if (valueType == type::get<uint8_t>())            outValue.SetUint(var.to_uint8());
            else if (valueType == type::get<uint16_t>())           outValue.SetUint(var.to_uint16());
            else if (valueType == type::get<uint32_t>())           outValue.SetUint(var.to_uint32());
            else if (valueType == type::get<uint64_t>())           outValue.SetUint64(var.to_uint64());
            else if (valueType == type::get<float>())              outValue.SetDouble(static_cast<double>(var.to_double()));
            else if (valueType == type::get<double>())             outValue.SetDouble(var.to_double());
            else                                                   return false;
            return true;
        }

        if (valueType.is_enumeration())
        {
            bool ok = false;
            const std::string name = var.to_string(&ok);
            if (ok)
            {
                outValue.SetString(name.c_str(), static_cast<SizeType>(name.size()), allocator);
                return true;
            }

            // Unnamed value - fall back to the number so nothing is lost.
            const uint64_t number = var.to_uint64(&ok);
            if (ok) { outValue.SetUint64(number); return true; }

            outValue.SetNull();
            return true;
        }

        if (valueType == type::get<std::string>())
        {
            const std::string text = var.to_string();
            outValue.SetString(text.c_str(), static_cast<SizeType>(text.size()), allocator);
            return true;
        }

        return false;
    }

    /*************************************************************************
     \brief  Write one value of any supported kind into a JSON value.
     *************************************************************************/
    void WriteVariant(const variant& var, Value& outValue, Allocator& allocator)
    {
        // A wrapped value (std::reference_wrapper and friends) has to be
        // unwrapped first, or every property would serialise as an object.
        const type valueType = var.get_type().is_wrapper()
            ? var.get_type().get_wrapped_type()
            : var.get_type();
        const variant value = var.get_type().is_wrapper() ? var.extract_wrapped_value() : var;

        if (WriteAtomic(valueType, value, outValue, allocator))
            return;

        if (value.is_sequential_container())
        {
            outValue.SetArray();
            for (const auto& item : value.create_sequential_view())
            {
                Value element;
                WriteVariant(item, element, allocator);
                outValue.PushBack(element, allocator);
            }
            return;
        }

        if (value.is_associative_container())
        {
            // Written as an array of {key, value} pairs rather than a JSON
            // object, because a JSON object's keys must be strings and an
            // engine map is just as likely to be keyed by an id.
            outValue.SetArray();
            for (const auto& pair : value.create_associative_view())
            {
                Value entry(kObjectType);
                Value key, mapped;
                WriteVariant(pair.first, key, allocator);
                WriteVariant(pair.second, mapped, allocator);
                entry.AddMember("key", key, allocator);
                entry.AddMember("value", mapped, allocator);
                outValue.PushBack(entry, allocator);
            }
            return;
        }

        // Anything left should be a registered class.
        if (!valueType.get_properties().empty())
        {
            WriteInstance(value, outValue, allocator);
            return;
        }

        // Unregistered and not a type we know: record null rather than
        // dropping the member, so the shape of the file still matches the
        // shape of the object and the gap is visible.
        CRU_ERROR("Serialization: no way to write type '{}' - writing null",
                  valueType.get_name().to_string());
        outValue.SetNull();
    }

    /*************************************************************************
     \brief  Write every serialisable property of an object.
     *************************************************************************/
    void WriteInstance(const instance& object, Value& outValue, Allocator& allocator)
    {
        outValue.SetObject();

        // get_wrapped_instance() reaches through a wrapper so that a
        // shared_ptr<Foo> serialises as a Foo, not as a pointer.
        const instance target = object.get_type().get_raw_type().is_wrapper()
            ? object.get_wrapped_instance()
            : object;

        for (const property& prop : target.get_derived_type().get_properties())
        {
            if (IsSkipped(prop) || IsLoadOnly(prop))
                continue;

            const variant value = prop.get_value(target);
            if (!value)
                continue;   // unreadable property, skip rather than write junk

            const std::string name = prop.get_name().to_string();

            Value member;
            WriteVariant(value, member, allocator);

            outValue.AddMember(Value(name.c_str(), static_cast<SizeType>(name.size()), allocator),
                               member, allocator);
        }
    }

    // -----------------------------------------------------------------------
    //  Reading
    // -----------------------------------------------------------------------

    /*************************************************************************
     \brief
        Turn a JSON value into a variant of the requested type.

     \param[in]
        value The JSON value.
     \param[in]
        wanted The type the caller needs.
     \return
        The converted value, or an invalid variant if it could not be made.

     \note
        convert() is called explicitly because JSON has one number type and
        C++ has a dozen. Without it, reading 3 into a float property gives
        an int variant that the property then refuses.
     *************************************************************************/
    variant ExtractBasic(const Value& value, const type& wanted)
    {
        switch (value.GetType())
        {
        case kStringType:
        {
            variant text = std::string(value.GetString());
            if (wanted.is_enumeration())
            {
                variant asEnum = text;
                if (asEnum.convert(wanted))
                    return asEnum;
            }
            if (text.convert(wanted))
                return text;
            return text;
        }
        case kNullType:  return variant();
        case kFalseType:
        case kTrueType:
        {
            variant flag = value.GetBool();
            if (flag.convert(wanted)) return flag;
            return flag;
        }
        case kNumberType:
        {
            variant number;
            if      (value.IsInt())     number = value.GetInt();
            else if (value.IsInt64())   number = value.GetInt64();
            else if (value.IsUint())    number = value.GetUint();
            else if (value.IsUint64())  number = value.GetUint64();
            else if (value.IsDouble())  number = value.GetDouble();
            else                        return variant();

            if (number.convert(wanted))
                return number;
            return number;
        }
        default:
            return variant();   // object or array - handled by the caller
        }
    }

    /*************************************************************************
     \brief  Fill a sequential container (vector, array) from a JSON array.
     *************************************************************************/
    void ReadSequence(variant_sequential_view& view, const Value& array)
    {
        view.set_size(array.Size());

        for (SizeType i = 0; i < array.Size(); ++i)
        {
            const Value& element = array[i];

            if (element.IsArray())
            {
                auto inner = view.get_value(i).create_sequential_view();
                ReadSequence(inner, element);
            }
            else if (element.IsObject())
            {
                variant slot = view.get_value(i);
                variant wrapped = slot.extract_wrapped_value();
                ReadInstance(element, wrapped);
                view.set_value(i, wrapped);
            }
            else
            {
                const type elementType = view.get_rank_type(1);
                variant converted = ExtractBasic(element, elementType);
                if (converted.convert(elementType))
                    view.set_value(i, converted);
            }
        }
    }

    /*************************************************************************
     \brief  Fill an associative container from the {key,value} array form.
     *************************************************************************/
    void ReadAssociative(variant_associative_view& view, const Value& array)
    {
        for (SizeType i = 0; i < array.Size(); ++i)
        {
            const Value& entry = array[i];
            if (!entry.IsObject() || !entry.HasMember("key") || !entry.HasMember("value"))
                continue;

            variant key    = ExtractBasic(entry["key"],   view.get_key_type());
            variant mapped = ExtractBasic(entry["value"], view.get_value_type());

            if (key && mapped)
                view.insert(key, mapped);
        }
    }

    /*************************************************************************
     \brief  Apply every matching JSON member to an object's properties.
     *************************************************************************/
    void ReadInstance(const Value& value, const instance& object)
    {
        const instance target = object.get_type().get_raw_type().is_wrapper()
            ? object.get_wrapped_instance()
            : object;

        const auto properties = target.get_derived_type().get_properties();

        for (const property& prop : properties)
        {
            if (IsSkipped(prop))
                continue;

            auto member = value.FindMember(prop.get_name().data());
            if (member == value.MemberEnd())
                continue;   // absent in the file: keep the current value

            const type propType = prop.get_type();
            const Value& jsonValue = member->value;

            if (jsonValue.IsArray())
            {
                variant existing = prop.get_value(target);
                if (existing.is_sequential_container())
                {
                    auto view = existing.create_sequential_view();
                    ReadSequence(view, jsonValue);
                }
                else if (existing.is_associative_container())
                {
                    auto view = existing.create_associative_view();
                    ReadAssociative(view, jsonValue);
                }
                prop.set_value(target, existing);
            }
            else if (jsonValue.IsObject())
            {
                variant existing = prop.get_value(target);
                ReadInstance(jsonValue, existing);
                prop.set_value(target, existing);
            }
            else
            {
                variant converted = ExtractBasic(jsonValue, propType);
                if (converted.convert(propType))
                    prop.set_value(target, converted);
            }
        }
    }
}

namespace Crucible::Reflection {

    void ToJsonValue(rttr::instance object, Value& outValue, Document::AllocatorType& allocator)
    {
        if (!object.is_valid())
        {
            outValue.SetNull();
            return;
        }

        WriteInstance(object, outValue, allocator);
    }

    bool FromJsonValue(const Value& value, rttr::instance object)
    {
        if (!object.is_valid() || !value.IsObject())
            return false;

        try
        {
            ReadInstance(value, object);
            return true;
        }
        catch (const std::exception& e)
        {
            // The boundary: recursion inside may throw, the engine never sees it.
            CRU_ERROR("Serialization: failed to read object - {}", e.what());
            return false;
        }
    }

    std::string ToJson(rttr::instance object, bool prettyPrint)
    {
        if (!object.is_valid())
            return "{}";

        Document document;
        document.SetObject();

        try
        {
            WriteInstance(object, document, document.GetAllocator());
        }
        catch (const std::exception& e)
        {
            CRU_ERROR("Serialization: failed to write object - {}", e.what());
            return "{}";
        }

        StringBuffer buffer;
        if (prettyPrint)
        {
            PrettyWriter<StringBuffer> writer(buffer);
            document.Accept(writer);
        }
        else
        {
            Writer<StringBuffer> writer(buffer);
            document.Accept(writer);
        }

        return std::string(buffer.GetString(), buffer.GetSize());
    }

    bool FromJson(const std::string& json, rttr::instance object)
    {
        if (!object.is_valid())
            return false;

        Document document;
        if (document.Parse(json.c_str()).HasParseError())
        {
            CRU_ERROR("Serialization: JSON parse error at offset {}", document.GetErrorOffset());
            return false;
        }

        return FromJsonValue(document, object);
    }

}
