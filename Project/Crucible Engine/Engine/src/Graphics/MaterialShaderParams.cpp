/*************************************************************************
  \file       MaterialShaderParams.cpp
  \brief
    See MaterialShaderParams.h.
*************************************************************************/
#include "Graphics/MaterialShaderParams.h"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <algorithm>
#include <array>
#include <utility>

namespace Crucible {

    namespace {

        struct TypeName { MaterialParamType type; const char* name; uint32_t components; };
        constexpr std::array<TypeName, 5> kTypes{ {
            { MaterialParamType::Float,  "float",  1 },
            { MaterialParamType::Vec2,   "vec2",   2 },
            { MaterialParamType::Vec3,   "vec3",   3 },
            { MaterialParamType::Vec4,   "vec4",   4 },
            { MaterialParamType::Colour, "colour", 3 },
        } };

        struct DefaultName { MaterialTextureDefault fallback; const char* name; };
        constexpr std::array<DefaultName, 3> kTextureDefaults{ {
            { MaterialTextureDefault::White,  "white"  },
            { MaterialTextureDefault::Black,  "black"  },
            { MaterialTextureDefault::Normal, "normal" },
        } };

        // A name the shader, the .mat file and the Inspector can all use as it is.
        bool IsIdentifier(std::string_view name) {
            if (name.empty() || (name.front() >= '0' && name.front() <= '9')) { return false; }
            return std::all_of(name.begin(), name.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
            });
        }

        // The "name" of entry `index` of the list called `list`, checked. Empty (with `error` set) if it is unusable.
        std::string ReadName(const rapidjson::Value& entry, const char* list, rapidjson::SizeType index, std::string& error) {
            const std::string where = std::string("\"") + list + "\" entry " + std::to_string(index + 1);
            if (!entry.IsObject()) {
                error = where + " is not an object ({ \"name\": ... })";
                return {};
            }
            const auto name = entry.FindMember("name");
            if (name == entry.MemberEnd() || !name->value.IsString()) {
                error = where + " has no \"name\"";
                return {};
            }
            std::string text(name->value.GetString(), name->value.GetStringLength());
            if (!IsIdentifier(text)) {
                error = where + ": \"" + text + "\" is not a usable name (a letter or '_', then letters, digits and '_')";
                return {};
            }
            return text;
        }

    }   // anonymous namespace

    const char* MaterialParamTypeName(MaterialParamType type) {
        for (const TypeName& known : kTypes) {
            if (known.type == type) { return known.name; }
        }
        return "float";
    }

    uint32_t MaterialParamComponents(MaterialParamType type) {
        for (const TypeName& known : kTypes) {
            if (known.type == type) { return known.components; }
        }
        return 1;
    }

    int MaterialShaderParams::FindParam(std::string_view name) const {
        for (size_t i = 0; i < params.size(); ++i) {
            if (params[i].name == name) { return static_cast<int>(i); }
        }
        return -1;
    }

    int MaterialShaderParams::FindTexture(std::string_view name) const {
        for (size_t i = 0; i < textures.size(); ++i) {
            if (textures[i].name == name) { return static_cast<int>(i); }
        }
        return -1;
    }

    void RemapMaterialShaderValues(const MaterialShaderParams& before, const MaterialShaderParams& now,
                                   std::vector<glm::vec4>& params, std::vector<TextureHandle>& textures) {

        std::vector<glm::vec4> newParams(now.params.size());
        for (size_t i = 0; i < now.params.size(); ++i) {
            const int old = before.FindParam(now.params[i].name);
            const bool kept = old >= 0 && static_cast<size_t>(old) < params.size()
                           && before.params[static_cast<size_t>(old)].type == now.params[i].type;
            newParams[i] = kept ? params[static_cast<size_t>(old)] : now.params[i].defaultValue;
        }

        std::vector<TextureHandle> newTextures(now.textures.size());
        for (size_t i = 0; i < now.textures.size(); ++i) {
            const int old = before.FindTexture(now.textures[i].name);
            if (old >= 0 && static_cast<size_t>(old) < textures.size()) { newTextures[i] = textures[static_cast<size_t>(old)]; }
        }

        params   = std::move(newParams);
        textures = std::move(newTextures);

    }   // RemapMaterialShaderValues()

    bool MaterialShaderParams::Parse(std::string_view json, MaterialShaderParams& out, std::string& error) {

        rapidjson::Document doc;
        doc.Parse(json.data(), json.size());
        if (doc.HasParseError()) {
            error = "not valid JSON (near character " + std::to_string(doc.GetErrorOffset()) + ")";
            return false;
        }
        if (!doc.IsObject()) {
            error = "the file is not a JSON object ({ \"params\": [...], \"textures\": [...] })";
            return false;
        }

        MaterialShaderParams list;

        // ---- "params" ----
        if (const auto params = doc.FindMember("params"); params != doc.MemberEnd()) {
            if (!params->value.IsArray()) {
                error = "\"params\" is not a list ([ ... ])";
                return false;
            }
            if (params->value.Size() > kMaxParams) {
                error = "more than " + std::to_string(kMaxParams) + " parameters";
                return false;
            }
            for (rapidjson::SizeType i = 0; i < params->value.Size(); ++i) {
                const rapidjson::Value& entry = params->value[i];
                MaterialParamDesc desc;
                desc.name = ReadName(entry, "params", i, error);
                if (desc.name.empty()) { return false; }
                if (list.FindParam(desc.name) >= 0) {
                    error = "the parameter \"" + desc.name + "\" is listed twice";
                    return false;
                }

                if (const auto type = entry.FindMember("type"); type != entry.MemberEnd()) {
                    const std::string text = type->value.IsString() ? type->value.GetString() : "";
                    const auto known = std::find_if(kTypes.begin(), kTypes.end(), [&](const TypeName& t) { return text == t.name; });
                    if (known == kTypes.end()) {
                        error = "parameter \"" + desc.name + "\": unknown type \"" + text + "\" (float, vec2, vec3, vec4 or colour)";
                        return false;
                    }
                    desc.type = known->type;
                }
                const uint32_t components = MaterialParamComponents(desc.type);

                // A colour that is not given a default is white; a number, 0.
                if (desc.type == MaterialParamType::Colour) { desc.defaultValue = glm::vec4(1.0f); }
                if (const auto value = entry.FindMember("default"); value != entry.MemberEnd()) {
                    if (value->value.IsNumber() && components == 1) {
                        desc.defaultValue.x = value->value.GetFloat();
                    } else if (value->value.IsArray() && value->value.Size() >= components && value->value.Size() <= 4) {
                        for (rapidjson::SizeType c = 0; c < value->value.Size(); ++c) {
                            if (!value->value[c].IsNumber()) {
                                error = "parameter \"" + desc.name + "\": its default is not all numbers";
                                return false;
                            }
                            desc.defaultValue[static_cast<glm::length_t>(c)] = value->value[c].GetFloat();
                        }
                    } else {
                        error = "parameter \"" + desc.name + "\": its default must be " +
                                (components == 1 ? std::string("a number") : "a list of " + std::to_string(components) + " numbers");
                        return false;
                    }
                }

                if (const auto low = entry.FindMember("min"); low != entry.MemberEnd() && low->value.IsNumber()) { desc.minValue = low->value.GetFloat(); }
                if (const auto high = entry.FindMember("max"); high != entry.MemberEnd() && high->value.IsNumber()) { desc.maxValue = high->value.GetFloat(); }
                list.params.push_back(std::move(desc));
            }
        }

        // ---- "textures" ----
        if (const auto textures = doc.FindMember("textures"); textures != doc.MemberEnd()) {
            if (!textures->value.IsArray()) {
                error = "\"textures\" is not a list ([ ... ])";
                return false;
            }
            if (textures->value.Size() > kMaxTextures) {
                error = "more than " + std::to_string(kMaxTextures) + " textures (a material has " + std::to_string(kMaxTextures) + " texture slots)";
                return false;
            }
            for (rapidjson::SizeType i = 0; i < textures->value.Size(); ++i) {
                const rapidjson::Value& entry = textures->value[i];
                MaterialTextureDesc desc;
                desc.name = ReadName(entry, "textures", i, error);
                if (desc.name.empty()) { return false; }
                if (list.FindTexture(desc.name) >= 0) {
                    error = "the texture \"" + desc.name + "\" is listed twice";
                    return false;
                }

                if (const auto colour = entry.FindMember("colour"); colour != entry.MemberEnd() && colour->value.IsBool()) {
                    desc.srgb = colour->value.GetBool();
                }
                if (const auto fallback = entry.FindMember("default"); fallback != entry.MemberEnd()) {
                    const std::string text = fallback->value.IsString() ? fallback->value.GetString() : "";
                    const auto known = std::find_if(kTextureDefaults.begin(), kTextureDefaults.end(), [&](const DefaultName& d) { return text == d.name; });
                    if (known == kTextureDefaults.end()) {
                        error = "texture \"" + desc.name + "\": unknown default \"" + text + "\" (white, black or normal)";
                        return false;
                    }
                    desc.fallback = known->fallback;
                }
                list.textures.push_back(std::move(desc));
            }
        }

        out = std::move(list);
        return true;

    }   // MaterialShaderParams::Parse()

    std::string MaterialShaderParams::ToJson() const {

        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        writer.SetIndent(' ', 2);
        writer.SetFormatOptions(rapidjson::kFormatSingleLineArray);

        writer.StartObject();
        writer.Key("version"); writer.Int(1);

        writer.Key("params");
        writer.SetFormatOptions(rapidjson::kFormatDefault);   // one entry per line...
        writer.StartArray();
        for (const MaterialParamDesc& desc : params) {
            const uint32_t components = MaterialParamComponents(desc.type);
            writer.StartObject();
            writer.Key("name"); writer.String(desc.name.c_str());
            writer.Key("type"); writer.String(MaterialParamTypeName(desc.type));
            writer.Key("default");
            if (components == 1) {
                writer.Double(desc.defaultValue.x);
            } else {
                writer.SetFormatOptions(rapidjson::kFormatSingleLineArray);   // ...but [1.0, 0.5, 0.25] on one
                writer.StartArray();
                for (uint32_t c = 0; c < components; ++c) { writer.Double(desc.defaultValue[static_cast<glm::length_t>(c)]); }
                writer.EndArray();
                writer.SetFormatOptions(rapidjson::kFormatDefault);
            }
            if (desc.type != MaterialParamType::Colour) {
                writer.Key("min"); writer.Double(desc.minValue);
                writer.Key("max"); writer.Double(desc.maxValue);
            }
            writer.EndObject();
        }
        writer.EndArray();

        writer.Key("textures");
        writer.StartArray();
        for (const MaterialTextureDesc& desc : textures) {
            const auto fallback = std::find_if(kTextureDefaults.begin(), kTextureDefaults.end(),
                                               [&](const DefaultName& d) { return d.fallback == desc.fallback; });
            writer.StartObject();
            writer.Key("name");    writer.String(desc.name.c_str());
            writer.Key("colour");  writer.Bool(desc.srgb);
            writer.Key("default"); writer.String(fallback != kTextureDefaults.end() ? fallback->name : "white");
            writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();

        return std::string(buffer.GetString(), buffer.GetSize()) + "\n";

    }   // MaterialShaderParams::ToJson()

}   // NAMESPACE CRUCIBLE
