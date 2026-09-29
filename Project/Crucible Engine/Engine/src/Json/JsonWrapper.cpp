/*************************************************************************
  \file       JsonWrapper.cpp
  \author     Tok Duan Jie Nelson
  \par        Email: t.duanjienelson@digipen.edu
  \date       29 September 2026
  \version    1.0
  \brief
    Path-based JSON document implementation.
  \details
    The whole class rests on two private helpers. Find() walks a path and
    returns nullptr the moment a segment is missing; FindOrCreate() walks
    the same path but builds whatever is not there yet.

    Everything public is a thin layer over one of those two, which is why
    nothing here can throw.
  \dependencies
    JsonWrapper.h, Diagnostics/Logger.h, rapidjson, <fstream>
  \see
    JsonWrapper.h, JsonWrapper.tpp
  \history
    - 1.0 (29 September 2026): Initial version.
  \copyright
    Copyright (C) 2026 DigiPen Institute of Technology.
*************************************************************************/
#include "Json/JsonWrapper.h"

#include <cctype>
#include <fstream>

#include <rapidjson/error/en.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "Diagnostics/Logger.h"

namespace {

    /*************************************************************************
     \brief  Whether a path segment is an array index rather than a name.
     \note   An empty string is not an index, so "a//b" cannot be mistaken
             for one.
     *************************************************************************/
    bool IsIndex(const std::string& segment)
    {
        if (segment.empty())
        {
            return false;
        }

        for (const char c : segment)
        {
            if (std::isdigit(static_cast<unsigned char>(c)) == 0)
            {
                return false;
            }
        }
        return true;
    }
}

namespace Crucible {

    JsonWrapper::JsonWrapper()
    {
        m_document.SetObject();
    }

    void JsonWrapper::CreateEmpty()
    {
        m_document.SetObject();
        // A fresh allocator, so memory from the previous contents is released
        // rather than accumulating across reloads.
        rapidjson::Document fresh;
        fresh.SetObject();
        m_document.Swap(fresh);
    }

    // =======================================================================
    //  Path handling
    // =======================================================================

    std::vector<std::string> JsonWrapper::SplitPath(const std::string& path)
    {
        std::vector<std::string> segments;

        std::size_t start = 0;
        while (start <= path.size())
        {
            const std::size_t slash = path.find('/', start);
            const std::size_t end = (slash == std::string::npos) ? path.size() : slash;

            if (end > start)
            {
                segments.push_back(path.substr(start, end - start));
            }

            if (slash == std::string::npos)
            {
                break;
            }
            start = slash + 1;
        }

        return segments;
    }

    const rapidjson::Value* JsonWrapper::Find(const std::string& path) const
    {
        const std::vector<std::string> segments = SplitPath(path);
        if (segments.empty())
        {
            return &m_document;
        }

        const rapidjson::Value* node = &m_document;

        for (const std::string& segment : segments)
        {
            if (IsIndex(segment))
            {
                if (!node->IsArray())
                {
                    return nullptr;
                }

                const auto index = static_cast<rapidjson::SizeType>(std::stoul(segment));
                if (index >= node->Size())
                {
                    return nullptr;
                }
                node = &(*node)[index];
            }
            else
            {
                if (!node->IsObject())
                {
                    return nullptr;
                }

                const auto member = node->FindMember(segment.c_str());
                if (member == node->MemberEnd())
                {
                    return nullptr;
                }
                node = &member->value;
            }
        }

        return node;
    }

    rapidjson::Value* JsonWrapper::FindOrCreate(const std::string& path)
    {
        const std::vector<std::string> segments = SplitPath(path);
        if (segments.empty())
        {
            return &m_document;
        }

        auto& allocator = m_document.GetAllocator();
        rapidjson::Value* node = &m_document;

        for (std::size_t i = 0; i < segments.size(); ++i)
        {
            const std::string& segment = segments[i];

            // The NEXT segment decides what this one has to be: a numeric
            // next segment means this node must be an array, anything else
            // means an object. The final segment holds the value itself, so
            // its kind is left to the caller.
            const bool lastSegment = (i + 1 == segments.size());
            const bool childIsIndex = !lastSegment && IsIndex(segments[i + 1]);

            if (IsIndex(segment))
            {
                if (!node->IsArray())
                {
                    node->SetArray();
                }

                const auto index = static_cast<rapidjson::SizeType>(std::stoul(segment));

                // Grow the array to reach the index, filling with nulls.
                while (node->Size() <= index)
                {
                    node->PushBack(rapidjson::Value(), allocator);
                }

                node = &(*node)[index];
            }
            else
            {
                if (!node->IsObject())
                {
                    node->SetObject();
                }

                auto member = node->FindMember(segment.c_str());
                if (member == node->MemberEnd())
                {
                    rapidjson::Value key(segment.c_str(),
                                         static_cast<rapidjson::SizeType>(segment.size()),
                                         allocator);
                    node->AddMember(key, rapidjson::Value(), allocator);
                    member = node->FindMember(segment.c_str());
                }

                node = &member->value;
            }

            if (!lastSegment)
            {
                if (childIsIndex && !node->IsArray())
                {
                    node->SetArray();
                }
                else if (!childIsIndex && !node->IsObject())
                {
                    node->SetObject();
                }
            }
        }

        return node;
    }

    // =======================================================================
    //  Loading and saving
    // =======================================================================

    bool JsonWrapper::ReadFromFile(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            CRU_ERROR("JsonWrapper: could not open '{}'", path.string());
            return false;
        }

        rapidjson::IStreamWrapper stream(file);

        rapidjson::Document parsed;
        parsed.ParseStream(stream);

        if (parsed.HasParseError())
        {
            CRU_ERROR("JsonWrapper: parse error in '{}' at offset {}: {}",
                      path.string(),
                      parsed.GetErrorOffset(),
                      rapidjson::GetParseError_En(parsed.GetParseError()));
            return false;
        }

        m_document.Swap(parsed);
        return true;
    }

    bool JsonWrapper::ReadFromString(const std::string& json)
    {
        rapidjson::Document parsed;
        parsed.Parse(json.c_str());

        if (parsed.HasParseError())
        {
            CRU_ERROR("JsonWrapper: parse error at offset {}: {}",
                      parsed.GetErrorOffset(),
                      rapidjson::GetParseError_En(parsed.GetParseError()));
            return false;
        }

        m_document.Swap(parsed);
        return true;
    }

    std::string JsonWrapper::ToString(bool pretty) const
    {
        rapidjson::StringBuffer buffer;

        if (pretty)
        {
            rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
            m_document.Accept(writer);
        }
        else
        {
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            m_document.Accept(writer);
        }

        return std::string(buffer.GetString(), buffer.GetSize());
    }

    bool JsonWrapper::SaveToFile(const std::filesystem::path& path, bool pretty) const
    {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        std::ofstream file(path, std::ios::trunc);
        if (!file.is_open())
        {
            CRU_ERROR("JsonWrapper: could not write '{}'", path.string());
            return false;
        }

        const std::string text = ToString(pretty);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));

        return file.good();
    }

    // =======================================================================
    //  Querying
    // =======================================================================

    bool JsonWrapper::Has(const std::string& path) const
    {
        return Find(path) != nullptr;
    }

    std::size_t JsonWrapper::GetListSize(const std::string& path) const
    {
        const rapidjson::Value* value = Find(path);
        if (value == nullptr || !value->IsArray())
        {
            return 0;
        }
        return value->Size();
    }

    std::vector<std::string> JsonWrapper::GetMemberNames(const std::string& path) const
    {
        std::vector<std::string> names;

        const rapidjson::Value* value = Find(path);
        if (value == nullptr || !value->IsObject())
        {
            return names;
        }

        names.reserve(value->MemberCount());
        for (auto member = value->MemberBegin(); member != value->MemberEnd(); ++member)
        {
            names.emplace_back(member->name.GetString(), member->name.GetStringLength());
        }

        return names;
    }

    // =======================================================================
    //  Objects
    // =======================================================================

    bool JsonWrapper::GetObject(const std::string& path, rttr::instance object) const
    {
        const rapidjson::Value* value = Find(path);
        if (value == nullptr || !value->IsObject())
        {
            return false;
        }

        return Reflection::FromJsonValue(*value, object);
    }

    void JsonWrapper::SetObject(const std::string& path, rttr::instance object)
    {
        rapidjson::Value* slot = FindOrCreate(path);
        if (slot == nullptr)
        {
            return;
        }

        rapidjson::Value written;
        Reflection::ToJsonValue(object, written, m_document.GetAllocator());

        *slot = written;   // rapidjson values move on assignment
    }

    void JsonWrapper::EnsureObject(const std::string& path)
    {
        rapidjson::Value* slot = FindOrCreate(path);
        if (slot != nullptr && !slot->IsObject())
        {
            slot->SetObject();
        }
    }

    void JsonWrapper::EnsureArray(const std::string& path)
    {
        rapidjson::Value* slot = FindOrCreate(path);
        if (slot != nullptr && !slot->IsArray())
        {
            slot->SetArray();
        }
    }

    std::size_t JsonWrapper::PushObject(const std::string& path)
    {
        rapidjson::Value* array = FindOrCreate(path);
        if (array == nullptr)
        {
            return 0;
        }

        if (!array->IsArray())
        {
            array->SetArray();
        }

        rapidjson::Value element(rapidjson::kObjectType);
        array->PushBack(element, m_document.GetAllocator());

        return array->Size() - 1;
    }

    bool JsonWrapper::Remove(const std::string& path)
    {
        const std::vector<std::string> segments = SplitPath(path);
        if (segments.empty())
        {
            return false;
        }

        // Walk to the parent, then remove the final segment from it.
        std::string parentPath;
        for (std::size_t i = 0; i + 1 < segments.size(); ++i)
        {
            if (i > 0)
            {
                parentPath += '/';
            }
            parentPath += segments[i];
        }

        const rapidjson::Value* constParent = Find(parentPath);
        if (constParent == nullptr)
        {
            return false;
        }

        auto* parent = const_cast<rapidjson::Value*>(constParent);
        const std::string& last = segments.back();

        if (IsIndex(last))
        {
            if (!parent->IsArray())
            {
                return false;
            }

            const auto index = static_cast<rapidjson::SizeType>(std::stoul(last));
            if (index >= parent->Size())
            {
                return false;
            }

            parent->Erase(parent->Begin() + index);
            return true;
        }

        if (!parent->IsObject())
        {
            return false;
        }

        return parent->RemoveMember(last.c_str());
    }

}
