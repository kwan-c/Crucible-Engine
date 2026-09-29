/*************************************************************************
  \file       JsonWrapper.tpp
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       29 September 2026
  \version    1.0
  \brief
    Template definitions for JsonWrapper.
  \details
    Included at the bottom of JsonWrapper.h. Kept apart so the header
    stays readable as an interface, matching Logger.h / Logger.tpp.
  \see
    JsonWrapper.h
  \history
    - 1.0 (29 September 2026): Initial version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#pragma once

#include <type_traits>

namespace Crucible {

    template<typename T>
    T JsonWrapper::Get(const std::string& path, const T& fallback) const
    {
        const rapidjson::Value* value = Find(path);
        if (value == nullptr)
        {
            return fallback;
        }

        if constexpr (std::is_same_v<T, std::string>)
        {
            return value->IsString() ? std::string(value->GetString(), value->GetStringLength())
                                     : fallback;
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            return value->IsBool() ? value->GetBool() : fallback;
        }
        else if constexpr (std::is_floating_point_v<T>)
        {
            return value->IsNumber() ? static_cast<T>(value->GetDouble()) : fallback;
        }
        else if constexpr (std::is_integral_v<T>)
        {
            if (!value->IsNumber())
            {
                return fallback;
            }
            if constexpr (std::is_signed_v<T>)
            {
                return static_cast<T>(value->GetInt64());
            }
            else
            {
                return static_cast<T>(value->GetUint64());
            }
        }
        else
        {
            static_assert(sizeof(T) == 0,
                "JsonWrapper::Get supports bool, integers, floating point and std::string. "
                "For a registered class use GetObject().");
            return fallback;
        }
    }

    template<typename T>
    std::vector<T> JsonWrapper::GetList(const std::string& path) const
    {
        std::vector<T> out;

        const rapidjson::Value* array = Find(path);
        if (array == nullptr || !array->IsArray())
        {
            return out;
        }

        out.reserve(array->Size());
        for (rapidjson::SizeType i = 0; i < array->Size(); ++i)
        {
            const rapidjson::Value& element = (*array)[i];

            if constexpr (std::is_same_v<T, std::string>)
            {
                out.push_back(element.IsString()
                    ? std::string(element.GetString(), element.GetStringLength())
                    : std::string{});
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                out.push_back(element.IsBool() ? element.GetBool() : false);
            }
            else if constexpr (std::is_floating_point_v<T>)
            {
                out.push_back(element.IsNumber() ? static_cast<T>(element.GetDouble()) : T{});
            }
            else if constexpr (std::is_integral_v<T>)
            {
                out.push_back(element.IsNumber() ? static_cast<T>(element.GetInt64()) : T{});
            }
            else
            {
                static_assert(sizeof(T) == 0,
                    "JsonWrapper::GetList supports bool, integers, floating point and std::string.");
            }
        }

        return out;
    }

    template<typename T>
    void JsonWrapper::Set(const std::string& path, const T& value)
    {
        rapidjson::Value* slot = FindOrCreate(path);
        if (slot == nullptr)
        {
            return;
        }

        auto& allocator = m_document.GetAllocator();

        if constexpr (std::is_same_v<T, std::string>)
        {
            slot->SetString(value.c_str(), static_cast<rapidjson::SizeType>(value.size()), allocator);
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            slot->SetBool(value);
        }
        else if constexpr (std::is_floating_point_v<T>)
        {
            slot->SetDouble(static_cast<double>(value));
        }
        else if constexpr (std::is_integral_v<T>)
        {
            if constexpr (std::is_signed_v<T>)
            {
                slot->SetInt64(static_cast<std::int64_t>(value));
            }
            else
            {
                slot->SetUint64(static_cast<std::uint64_t>(value));
            }
        }
        else
        {
            static_assert(sizeof(T) == 0,
                "JsonWrapper::Set supports bool, integers, floating point and std::string. "
                "For a registered class use SetObject().");
        }
    }

    template<typename T>
    void JsonWrapper::SetList(const std::string& path, const std::vector<T>& values)
    {
        rapidjson::Value* slot = FindOrCreate(path);
        if (slot == nullptr)
        {
            return;
        }

        auto& allocator = m_document.GetAllocator();

        slot->SetArray();
        slot->Reserve(static_cast<rapidjson::SizeType>(values.size()), allocator);

        for (const T& value : values)
        {
            rapidjson::Value element;

            if constexpr (std::is_same_v<T, std::string>)
            {
                element.SetString(value.c_str(), static_cast<rapidjson::SizeType>(value.size()), allocator);
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                element.SetBool(value);
            }
            else if constexpr (std::is_floating_point_v<T>)
            {
                element.SetDouble(static_cast<double>(value));
            }
            else if constexpr (std::is_integral_v<T>)
            {
                if constexpr (std::is_signed_v<T>)
                {
                    element.SetInt64(static_cast<std::int64_t>(value));
                }
                else
                {
                    element.SetUint64(static_cast<std::uint64_t>(value));
                }
            }
            else
            {
                static_assert(sizeof(T) == 0,
                    "JsonWrapper::SetList supports bool, integers, floating point and std::string.");
            }

            slot->PushBack(element, allocator);
        }
    }

}
