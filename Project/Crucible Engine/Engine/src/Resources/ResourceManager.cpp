/*************************************************************************
* \file:         ResourceManager.cpp
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                See ResourceManager.h for the class's role and public API.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include "ResourceManager.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include "AssetPipe/Assetmanager.h"
#include "AssetPipe/GLTFLoader.h"
#include "ECS/Components/Components.h"
#include "Graphics/RenderSystem.h"
#include "Diagnostics/Logger.h"

namespace Crucible {

    namespace {

        // FNV-1a, 64-bit. Used only to derive stable ids (built-ins, embedded sub-assets), never
        // for security - collisions with real .meta GUIDs are astronomically unlikely.
        constexpr std::uint64_t kFnvOffset = 0xcbf29ce484222325ull;
        constexpr std::uint64_t kFnvPrime  = 0x100000001b3ull;

        std::uint64_t Fnv1a(std::uint64_t hash, const void* data, size_t size) {
            const unsigned char* bytes = static_cast<const unsigned char*>(data);
            for (size_t i = 0; i < size; ++i) { hash = (hash ^ bytes[i]) * kFnvPrime; }
            return hash;
        }

        std::string ToHex(AssetId id) {
            char buffer[32] = {};
            std::snprintf(buffer, sizeof(buffer), "%016llX", static_cast<unsigned long long>(id));
            return buffer;
        }

        AssetId FromHex(const char* text) {
            if (!text || !*text) { return 0; }
            return static_cast<AssetId>(std::strtoull(text, nullptr, 16));
        }

        xresource::instance_guid ToGuid(AssetId id) { return xresource::instance_guid{ id }; }

        // .mat texture slot names, in MaterialData order, with the colour space each slot is
        // sampled in (albedo/emissive are colour data -> sRGB; the rest are linear data). Same
        // convention as Renderer::CreateTexture's doc comment and the Inspector's slot table.
        struct SlotDesc { const char* key; TextureHandle MaterialData::* member; bool isSRGB; };
        const SlotDesc kSlots[] = {
            { "albedo",            &MaterialData::albedo,            true  },
            { "normal",            &MaterialData::normal,            false },
            { "metallicRoughness", &MaterialData::metallicRoughness, false },
            { "emissive",          &MaterialData::emissive,          true  },
            { "occlusion",         &MaterialData::occlusion,         false },
        };

    }   // anonymous namespace

    // ------------------------------------------------------------------------------------------------------------- //
    //  SETUP
    // ------------------------------------------------------------------------------------------------------------- //

    ResourceManager::ResourceManager(AssetManager& assets, Renderer& renderer)
        : m_assets{ assets }, m_renderer{ renderer } {}

    void ResourceManager::Init() {

        RegisterBuiltins();
        RefreshCatalog();

    }   // ResourceManager::Init()

    AssetId ResourceManager::BuiltinId(const char* name) {

        static constexpr char kPrefix[] = "Crucible/Builtin/";
        std::uint64_t hash = Fnv1a(kFnvOffset, kPrefix, sizeof(kPrefix) - 1);
        hash = Fnv1a(hash, name, std::char_traits<char>::length(name));
        return hash | 1ull;   // instance GUIDs always have the LSB set

    }   // ResourceManager::BuiltinId()

    AssetId ResourceManager::DeriveSubAssetId(AssetId parent, const char* tag) {

        std::uint64_t hash = Fnv1a(kFnvOffset, &parent, sizeof(parent));
        hash = Fnv1a(hash, "#", 1);
        hash = Fnv1a(hash, tag, std::char_traits<char>::length(tag));
        return hash | 1ull;

    }   // ResourceManager::DeriveSubAssetId()

    void ResourceManager::RegisterBuiltins() {

        // Primitive meshes: the Renderer already created them (Renderer::CreatePrimitiveMeshes),
        // named "Cube", "Sphere", ... - give each a fixed id derived from that name.
        for (MeshHandle mesh : m_renderer.GetPrimitiveMeshes()) {
            const std::string& name = m_renderer.GetMeshName(mesh);
            const AssetId id = BuiltinId(name.c_str());
            m_builtinNames[id]    = name;
            m_meshes[id]          = mesh;
            m_meshIds[mesh.id]    = id;
        }

        const AssetId defaultMaterial = BuiltinId("Default Material");
        m_builtinNames[defaultMaterial] = "Default Material";
        m_materials[defaultMaterial]    = m_renderer.GetDefaultMaterial();
        m_materialIds[m_renderer.GetDefaultMaterial().id] = defaultMaterial;

    }   // ResourceManager::RegisterBuiltins()

    void ResourceManager::RefreshCatalog() {

        // Every Model asset exposes its embedded material as a derived sub-asset, so it can be
        // listed and picked (and saved in a scene) before the model has ever been loaded.
        for (const AssetEntry* entry : m_assets.GetByType(AssetType::Model)) {
            const AssetId modelId = entry->guid.m_Value;
            if (modelId == 0) { continue; }
            m_subAssets[DeriveSubAssetId(modelId, "material")] = SubAsset{ modelId, ResourceKind::Material };
        }

    }   // ResourceManager::RefreshCatalog()

    void ResourceManager::WarnOnce(AssetId id, const std::string& message) {

        if (m_warned[id]) { return; }
        m_warned[id] = true;
        CRU_WARN("[Resources] {} ({})", message, GetName(id));

    }   // ResourceManager::WarnOnce()

    // ------------------------------------------------------------------------------------------------------------- //
    //  GET
    // ------------------------------------------------------------------------------------------------------------- //

    MeshHandle ResourceManager::GetMesh(AssetId id) {

        if (id == 0) { return {}; }
        if (auto it = m_meshes.find(id); it != m_meshes.end()) { return it->second; }

        const AssetEntry* entry = m_assets.FindByGuid(ToGuid(id));
        if (!entry || entry->type != AssetType::Model) {
            WarnOnce(id, "Not a mesh asset");
            m_meshes[id] = MeshHandle{};
            return {};
        }

        LoadModel(id);   // fills m_meshes[id] either way (Invalid on failure)
        return m_meshes[id];

    }   // ResourceManager::GetMesh()

    MaterialHandle ResourceManager::GetMaterial(AssetId id) {

        if (id == 0) { return {}; }
        if (auto it = m_materials.find(id); it != m_materials.end()) { return it->second; }

        // Embedded in a model? (Re-read the index once in case the model is new.)
        auto sub = m_subAssets.find(id);
        if (sub == m_subAssets.end()) {
            RefreshCatalog();
            sub = m_subAssets.find(id);
        }
        if (sub != m_subAssets.end()) {
            if (m_meshes.find(sub->second.parent) == m_meshes.end()) { LoadModel(sub->second.parent); }
            if (auto it = m_materials.find(id); it != m_materials.end()) { return it->second; }
            m_materials[id] = MaterialHandle{};
            return {};
        }

        const AssetEntry* entry = m_assets.FindByGuid(ToGuid(id));
        if (!entry || entry->type != AssetType::Material) {
            WarnOnce(id, "Not a material asset");
            m_materials[id] = MaterialHandle{};
            return {};
        }

        const MaterialHandle handle = LoadMaterialFile(id);
        m_materials[id] = handle;
        if (handle.IsValid()) { m_materialIds[handle.id] = id; }
        return handle;

    }   // ResourceManager::GetMaterial()

    TextureHandle ResourceManager::GetTexture(AssetId id, bool isSRGB) {

        if (id == 0) { return {}; }

        TextureSlots& slots = m_textures[id];
        TextureHandle& handle = isSRGB ? slots.srgb : slots.linear;
        bool& tried           = isSRGB ? slots.triedSrgb : slots.triedLinear;
        if (tried) { return handle; }
        tried = true;

        const AssetEntry* entry = m_assets.FindByGuid(ToGuid(id));
        if (!entry || entry->type != AssetType::Texture) {
            WarnOnce(id, "Not a texture asset");
            return {};
        }

        std::vector<unsigned char> bytes;
        if (!m_assets.ReadBytes(entry->virtualPath, bytes) || bytes.empty()) {
            WarnOnce(id, "Could not read texture");
            return {};
        }

        try {
            handle = m_renderer.CreateTextureFromMemory(bytes.data(), bytes.size(), isSRGB,
                "asset:" + ToHex(id) + (isSRGB ? ":srgb" : ":linear"));
            m_textureIds[handle.id] = id;
        } catch (const std::exception& e) {
            WarnOnce(id, std::string("Texture decode failed: ") + e.what());
            handle = {};
        }
        return handle;

    }   // ResourceManager::GetTexture()

    CubemapHandle ResourceManager::GetCubemap(AssetId id) {

        if (id == 0) { return {}; }
        if (auto it = m_cubemaps.find(id); it != m_cubemaps.end()) { return it->second; }

        CubemapHandle handle{};
        const AssetEntry* entry = m_assets.FindByGuid(ToGuid(id));
        if (!entry || entry->type != AssetType::Texture) {
            WarnOnce(id, "Not a texture asset (skyboxes are loaded from equirectangular images)");
        } else {
            try {
                handle = m_renderer.CreateCubemapFromEquirect(m_assets.Resolve(entry->virtualPath).string());
            } catch (const std::exception& e) {
                WarnOnce(id, std::string("Skybox load failed: ") + e.what());
            }
        }
        m_cubemaps[id] = handle;
        return handle;

    }   // ResourceManager::GetCubemap()

    TextureHandle ResourceManager::GetTextureByPath(const std::string& virtualPath, bool isSRGB) {

        return GetTexture(GetIdForPath(virtualPath), isSRGB);

    }   // ResourceManager::GetTextureByPath()

    AssetId ResourceManager::GetIdForPath(const std::string& virtualPath) const {

        return m_assets.GetGuid(virtualPath).m_Value;

    }   // ResourceManager::GetIdForPath()

    // ------------------------------------------------------------------------------------------------------------- //
    //  REVERSE LOOKUP / NAMES / LISTING
    // ------------------------------------------------------------------------------------------------------------- //

    AssetId ResourceManager::FindId(MeshHandle handle) const {
        auto it = m_meshIds.find(handle.id);
        return it == m_meshIds.end() ? 0 : it->second;
    }

    AssetId ResourceManager::FindId(MaterialHandle handle) const {
        auto it = m_materialIds.find(handle.id);
        return it == m_materialIds.end() ? 0 : it->second;
    }

    AssetId ResourceManager::FindId(TextureHandle handle) const {
        auto it = m_textureIds.find(handle.id);
        return it == m_textureIds.end() ? 0 : it->second;
    }

    std::string ResourceManager::GetName(AssetId id) const {

        if (id == 0) { return "None"; }
        if (auto it = m_builtinNames.find(id); it != m_builtinNames.end()) { return it->second; }
        if (auto it = m_subAssets.find(id); it != m_subAssets.end()) {
            return GetName(it->second.parent) + " (material)";
        }
        if (const AssetEntry* entry = m_assets.FindByGuid(ToGuid(id))) { return entry->virtualPath; }
        return "(missing " + ToHex(id) + ")";

    }   // ResourceManager::GetName()

    std::vector<ResourceInfo> ResourceManager::List(ResourceKind kind) const {

        std::vector<ResourceInfo> builtins;
        std::vector<ResourceInfo> assets;

        auto isLoaded = [&](AssetId id) -> bool {
            switch (kind) {
                case ResourceKind::Mesh:     { auto it = m_meshes.find(id);    return it != m_meshes.end()    && it->second.IsValid(); }
                case ResourceKind::Material: { auto it = m_materials.find(id); return it != m_materials.end() && it->second.IsValid(); }
                default:                     { auto it = m_textures.find(id);  return it != m_textures.end()  && (it->second.srgb.IsValid() || it->second.linear.IsValid()); }
            }
        };

        // Built-ins (in registration order is fine; they're few and hand-named).
        for (const auto& [id, name] : m_builtinNames) {
            const bool matches = (kind == ResourceKind::Mesh     && m_meshes.count(id) != 0)
                              || (kind == ResourceKind::Material && m_materials.count(id) != 0);
            if (matches) { builtins.push_back({ id, kind, name, true, true }); }
        }

        auto addAssets = [&](AssetType type, const char* suffix, bool derived) {
            for (const AssetEntry* entry : m_assets.GetByType(type)) {
                const AssetId fileId = entry->guid.m_Value;
                if (fileId == 0) { continue; }
                const AssetId id = derived ? DeriveSubAssetId(fileId, "material") : fileId;
                assets.push_back({ id, kind, entry->virtualPath + suffix, false, isLoaded(id) });
            }
        };

        switch (kind) {
            case ResourceKind::Mesh:
                addAssets(AssetType::Model, "", false);
                break;
            case ResourceKind::Material:
                addAssets(AssetType::Material, "", false);
                addAssets(AssetType::Model, " (material)", true);
                break;
            case ResourceKind::Texture:
                addAssets(AssetType::Texture, "", false);
                break;
        }

        auto byName = [](const ResourceInfo& a, const ResourceInfo& b) { return a.name < b.name; };
        std::sort(builtins.begin(), builtins.end(), byName);
        std::sort(assets.begin(), assets.end(), byName);
        builtins.insert(builtins.end(), assets.begin(), assets.end());
        return builtins;

    }   // ResourceManager::List()

    // ------------------------------------------------------------------------------------------------------------- //
    //  MESHRENDERER HELPERS
    // ------------------------------------------------------------------------------------------------------------- //

    void ResourceManager::AssignMesh(MeshRenderer& renderer, AssetId id) {

        renderer.meshAsset = id;
        renderer.mesh      = GetMesh(id);

    }   // ResourceManager::AssignMesh()

    void ResourceManager::AssignMaterial(MeshRenderer& renderer, AssetId id) {

        renderer.materialAsset = id;
        renderer.material      = GetMaterial(id);

    }   // ResourceManager::AssignMaterial()

    void ResourceManager::Resolve(MeshRenderer& renderer) {

        if (renderer.meshAsset != 0)     { renderer.mesh     = GetMesh(renderer.meshAsset); }
        if (renderer.materialAsset != 0) { renderer.material = GetMaterial(renderer.materialAsset); }

    }   // ResourceManager::Resolve()

    // ------------------------------------------------------------------------------------------------------------- //
    //  LOADERS (the only code that knows file formats - swap these when engine formats land)
    // ------------------------------------------------------------------------------------------------------------- //

    bool ResourceManager::LoadModel(AssetId modelId) {

        const AssetId materialId = DeriveSubAssetId(modelId, "material");
        m_subAssets[materialId]  = SubAsset{ modelId, ResourceKind::Material };
        m_meshes[modelId]        = MeshHandle{};   // pessimistic: stays Invalid unless we succeed

        const AssetEntry* entry = m_assets.FindByGuid(ToGuid(modelId));
        std::vector<unsigned char> bytes;
        if (!entry || !m_assets.ReadBytes(entry->virtualPath, bytes)) {
            WarnOnce(modelId, "Could not read model");
            return false;
        }

        // TEMPORARY source-format path: .glb parsed at load time. Replace with the engine mesh
        // format's reader once the importer produces one.
        GLTFAsset glb = GLTFLoader::LoadGLB(bytes);
        if (!glb.ok) {
            WarnOnce(modelId, "Model parse failed: " + glb.error);
            return false;
        }

        try {
            const MeshHandle mesh = m_renderer.CreateMesh(glb.mesh, entry->virtualPath);
            m_meshes[modelId]     = mesh;
            m_meshIds[mesh.id]    = modelId;

            if (glb.material) {
                const GLTFMaterial& gm = *glb.material;
                const std::string keyPrefix = "asset:" + ToHex(modelId) + ":";

                MaterialData data;
                data.factors.albedoTint      = gm.baseColorFactor;
                data.factors.emissiveFactor  = gm.emissiveFactor;
                data.factors.metallicFactor  = gm.metallicFactor;
                data.factors.roughnessFactor = gm.roughnessFactor;

                auto embedded = [&](bool has, std::size_t image, bool isSRGB, const char* slot) -> TextureHandle {
                    if (!has || image >= glb.images.size()) { return {}; }
                    const auto& img = glb.images[image];
                    return m_renderer.CreateTextureFromMemory(img.bytes.data(), img.bytes.size(), isSRGB, keyPrefix + slot);
                };
                data.albedo            = embedded(gm.hasBaseColor,         gm.baseColorImage,         true,  "albedo");
                data.normal            = embedded(gm.hasNormal,            gm.normalImage,            false, "normal");
                data.metallicRoughness = embedded(gm.hasMetallicRoughness, gm.metallicRoughnessImage, false, "metallicRoughness");
                data.emissive          = embedded(gm.hasEmissive,          gm.emissiveImage,          true,  "emissive");
                data.occlusion         = embedded(gm.hasOcclusion,         gm.occlusionImage,         false, "occlusion");

                const MaterialHandle material = m_renderer.CreateMaterial(data);
                m_materials[materialId]   = material;
                m_materialIds[material.id] = materialId;
            } else {
                // No material in the file: the embedded-material id means "the default material".
                m_materials[materialId] = m_renderer.GetDefaultMaterial();
            }
        } catch (const std::exception& e) {
            WarnOnce(modelId, std::string("Model upload failed: ") + e.what());
            return false;
        }

        CRU_INFO("[Resources] Loaded model {}", entry->virtualPath);
        return true;

    }   // ResourceManager::LoadModel()

    MaterialHandle ResourceManager::LoadMaterialFile(AssetId id) {

        const AssetEntry* entry = m_assets.FindByGuid(ToGuid(id));
        std::string text;
        if (!entry || !m_assets.ReadText(entry->virtualPath, text)) {
            WarnOnce(id, "Could not read material");
            return {};
        }

        // .mat format (JSON, version 1) - see SerializeMaterial() for the writer. An empty or
        // unparsable file loads as a default-valued material (with a warning) rather than failing,
        // so a freshly created blank .mat is still usable and can be saved over.
        MaterialData data;
        rapidjson::Document doc;
        doc.Parse(text.c_str(), text.size());
        if (text.empty() || doc.HasParseError() || !doc.IsObject()) {
            WarnOnce(id, "Material file empty or not valid JSON; using default values");
        } else {
            auto readFloats = [&](const char* key, float* out, rapidjson::SizeType count) {
                auto it = doc.FindMember(key);
                if (it == doc.MemberEnd() || !it->value.IsArray() || it->value.Size() != count) { return; }
                for (rapidjson::SizeType i = 0; i < count; ++i) {
                    if (it->value[i].IsNumber()) { out[i] = it->value[i].GetFloat(); }
                }
            };
            auto readFloat = [&](const char* key, float& out) {
                auto it = doc.FindMember(key);
                if (it != doc.MemberEnd() && it->value.IsNumber()) { out = it->value.GetFloat(); }
            };

            readFloats("albedoTint",     &data.factors.albedoTint.x,     4);
            readFloats("emissiveFactor", &data.factors.emissiveFactor.x, 3);
            readFloat("metallic",  data.factors.metallicFactor);
            readFloat("roughness", data.factors.roughnessFactor);

            if (auto textures = doc.FindMember("textures"); textures != doc.MemberEnd() && textures->value.IsObject()) {
                for (const SlotDesc& slot : kSlots) {
                    auto it = textures->value.FindMember(slot.key);
                    if (it == textures->value.MemberEnd() || !it->value.IsString()) { continue; }
                    const AssetId textureId = FromHex(it->value.GetString());
                    data.*slot.member = GetTexture(textureId, slot.isSRGB);   // Invalid -> renderer placeholder
                }
            }
        }

        try {
            return m_renderer.CreateMaterial(data);
        } catch (const std::exception& e) {
            WarnOnce(id, std::string("Material creation failed: ") + e.what());
            return {};
        }

    }   // ResourceManager::LoadMaterialFile()

    std::string ResourceManager::SerializeMaterial(const MaterialData& data) const {

        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        writer.SetIndent(' ', 2);
        writer.SetFormatOptions(rapidjson::kFormatSingleLineArray);   // [1.0, 0.5, 0.25, 1.0] on one line

        auto floats = [&](const char* key, const float* values, int count) {
            writer.Key(key);
            writer.StartArray();
            for (int i = 0; i < count; ++i) { writer.Double(values[i]); }
            writer.EndArray();
        };

        writer.StartObject();
        writer.Key("version");   writer.Int(1);
        floats("albedoTint",     &data.factors.albedoTint.x,     4);
        floats("emissiveFactor", &data.factors.emissiveFactor.x, 3);
        writer.Key("metallic");  writer.Double(data.factors.metallicFactor);
        writer.Key("roughness"); writer.Double(data.factors.roughnessFactor);

        // Textures by asset GUID (hex, same format as .meta files). A slot whose texture didn't
        // come from this ResourceManager (e.g. embedded in a .glb) has no GUID and is saved empty.
        writer.Key("textures");
        writer.StartObject();
        for (const SlotDesc& slot : kSlots) {
            const TextureHandle handle = data.*slot.member;
            const AssetId textureId = handle.IsValid() ? FindId(handle) : 0;
            writer.Key(slot.key);
            writer.String(textureId ? ToHex(textureId).c_str() : "");
        }
        writer.EndObject();
        writer.EndObject();

        return std::string(buffer.GetString(), buffer.GetSize()) + "\n";

    }   // ResourceManager::SerializeMaterial()

    // ------------------------------------------------------------------------------------------------------------- //
    //  MATERIAL ASSETS
    // ------------------------------------------------------------------------------------------------------------- //

    AssetId ResourceManager::CreateMaterialAsset(const std::string& virtualPath, const MaterialData& data) {

        std::string path = virtualPath;
        if (path.size() < 4 || path.compare(path.size() - 4, 4, ".mat") != 0) { path += ".mat"; }

        if (!m_assets.WriteText(path, SerializeMaterial(data))) {
            CRU_WARN("[Resources] Could not write material {}", path);
            return 0;
        }

        // Re-index so the new file gets its .meta (and GUID) like any other asset.
        m_assets.Refresh();
        RefreshCatalog();
        const AssetId id = GetIdForPath(path);
        if (id == 0) {
            CRU_WARN("[Resources] Wrote {} but it did not appear in the asset index", path);
            return 0;
        }

        // Overwriting a material that's already loaded: update it in place so every entity using
        // it sees the change and the handle stays the same.
        if (auto it = m_materials.find(id); it != m_materials.end() && it->second.IsValid()) {
            m_renderer.UpdateMaterial(it->second, data);
            return id;
        }

        const MaterialHandle handle = m_renderer.CreateMaterial(data);
        m_materials[id]           = handle;
        m_materialIds[handle.id]  = id;
        m_warned.erase(id);
        return id;

    }   // ResourceManager::CreateMaterialAsset()

    bool ResourceManager::SaveMaterial(AssetId id) {

        const AssetEntry* entry = m_assets.FindByGuid(ToGuid(id));
        auto it = m_materials.find(id);
        if (!entry || entry->type != AssetType::Material || it == m_materials.end() || !it->second.IsValid()) {
            CRU_WARN("[Resources] SaveMaterial: {} is not a loaded .mat material", GetName(id));
            return false;
        }

        const std::string path = entry->virtualPath;   // copy: entry pointers die on Refresh()
        const bool ok = m_assets.WriteText(path, SerializeMaterial(m_renderer.GetMaterialData(it->second)));
        if (!ok) { CRU_WARN("[Resources] Could not write material {}", path); }
        return ok;

    }   // ResourceManager::SaveMaterial()

}   // NAMESPACE CRUCIBLE
