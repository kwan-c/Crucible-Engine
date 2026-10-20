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
#include "Resources/RenderedModel.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include "AssetPipe/Assetmanager.h"
#include "Resources/CompiledModel.h"
#include "Resources/AnimatorControllerIO.h"
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

        // .mat "shading" values.
        const char* ShadingName(MaterialShading shading) {
            switch (shading) {
                case MaterialShading::Toon:      return "Toon";
                case MaterialShading::Realistic: return "Realistic";
                default:                         return "Scene";
            }
        }
        MaterialShading ShadingFromName(const char* name) {
            const std::string text = name ? name : "";
            if (text == "Toon")      { return MaterialShading::Toon; }
            if (text == "Realistic") { return MaterialShading::Realistic; }
            return MaterialShading::Scene;
        }

        // .mat readers: a missing or wrongly typed key leaves the value as it was (its default).
        void ReadFloats(const rapidjson::Value& object, const char* key, float* out, rapidjson::SizeType count) {
            auto it = object.FindMember(key);
            if (it == object.MemberEnd() || !it->value.IsArray() || it->value.Size() != count) { return; }
            for (rapidjson::SizeType i = 0; i < count; ++i) {
                if (it->value[i].IsNumber()) { out[i] = it->value[i].GetFloat(); }
            }
        }
        void ReadFloat(const rapidjson::Value& object, const char* key, float& out) {
            auto it = object.FindMember(key);
            if (it != object.MemberEnd() && it->value.IsNumber()) { out = it->value.GetFloat(); }
        }
        void ReadBool(const rapidjson::Value& object, const char* key, bool& out) {
            auto it = object.FindMember(key);
            if (it != object.MemberEnd() && it->value.IsBool()) { out = it->value.GetBool(); }
        }

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
        CRU_USER_WARN("[Resources] {} ({})", message, GetName(id));

    }   // ResourceManager::WarnOnce()

    // ------------------------------------------------------------------------------------------------------------- //
    //  GET
    // ------------------------------------------------------------------------------------------------------------- //

    std::shared_ptr<const ModelPose> ResourceManager::GetModelRestPose(AssetId id) {
        if (!id) return nullptr;
        if (auto found = m_modelRestPoses.find(id); found != m_modelRestPoses.end()) return found->second;
        auto model = GetModel(id);
        auto pose = std::make_shared<ModelPose>();
        if (!model || !EvaluateModelPose(*model, InvalidModelIndex, 0, *pose)) pose.reset();
        m_modelRestPoses[id] = pose;
        return pose;
    }

    std::shared_ptr<const ModelResource> ResourceManager::GetModel(AssetId id) {
        if (id == 0) { return {}; }
        if (auto found = m_models.find(id); found != m_models.end()) { return found->second; }
        m_models[id] = nullptr;
        const AssetEntry* entry = m_assets.FindByGuid(ToGuid(id));
        std::vector<unsigned char> bytes;
        if (!entry || entry->type != AssetType::Model) {
            WarnOnce(id, "Could not read model asset");
            return {};
        }
        if (entry->extension != ".cmodel") {
            WarnOnce(id, "Source model cannot be loaded at runtime; compile it from AssetsSource first");
            return {};
        }
        if (entry->sizeBytes > MaxCompiledModelBytes || !m_assets.ReadBytes(entry->virtualPath, bytes)) {
            WarnOnce(id, "Compiled model is unreadable or exceeds the 1 GiB limit");
            return {};
        }
        ModelImportResult imported = DecodeCompiledModel(bytes, id);
        if (!imported.ok) {
            WarnOnce(id, "Compiled model load failed: " + imported.error);
            return {};
        }
        for (const auto& warning : imported.model.warnings) {
            CRU_WARN("[Resources] {}: {}", entry->virtualPath, warning);
        }
        auto model = std::make_shared<ModelResource>(std::move(imported.model));
        m_models[id] = model;
        return model;
    }

    std::shared_ptr<const AnimatorController> ResourceManager::GetAnimatorController(AssetId id) {
        if (!id) return {};
        if (auto found = m_controllers.find(id); found != m_controllers.end()) return found->second;
        m_controllers[id] = nullptr;
        const auto* entry = m_assets.FindByGuid(ToGuid(id));
        std::vector<unsigned char> bytes;
        if (!entry || entry->type != AssetType::AnimatorController || entry->sizeBytes > MaxAnimatorControllerBytes ||
            !m_assets.ReadBytes(entry->virtualPath, bytes)) {
            WarnOnce(id, "Could not read Animator Controller (limit 4 MiB)"); return {};
        }
        auto decoded = DecodeAnimatorController(bytes);
        if (!decoded.ok) { WarnOnce(id, "Animator Controller load failed: " + decoded.error); return {}; }
        auto definition = std::make_shared<AnimatorController>(std::move(decoded.controller));
        m_controllers[id] = definition;
        return definition;
    }

    bool ResourceManager::SaveAnimatorController(AssetId id, const std::string& json, std::string* error) {
        const auto* entry = m_assets.FindByGuid(ToGuid(id));
        if (!entry || entry->type != AssetType::AnimatorController) {
            if (error) *error = "Controller asset is missing from the index";
            return false;
        }
        const std::string path = entry->virtualPath; // Refresh invalidates index pointers
        return WriteAnimatorControllerFile(path, json, false, error) == id;
    }

    AssetId ResourceManager::CreateAnimatorControllerAsset(const std::string& path, const std::string& json, std::string* error) {
        return WriteAnimatorControllerFile(path, json, true, error);
    }

    AssetId ResourceManager::WriteAnimatorControllerFile(const std::string& path, const std::string& json,
                                                        bool createNew, std::string* error) {
        namespace fs = std::filesystem;
        const auto fail = [&](const std::string& reason) -> AssetId { if (error) *error = reason; return 0; };
        auto decoded = DecodeAnimatorController({reinterpret_cast<const unsigned char*>(json.data()), json.size()});
        if (!decoded.ok) return fail(decoded.error);
        const fs::path relative(path);
        if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.extension() != ".animcontroller")
            return fail("Choose a relative .animcontroller path inside Assets");
        for (const auto& part : relative) if (part == "..") return fail("Controller path cannot leave Assets");
        std::error_code ec;
        const auto root = fs::weakly_canonical(m_assets.GetAssetRoot(), ec);
        if (ec) return fail("Could not resolve Assets root");
        const auto target = fs::weakly_canonical(root / relative, ec);
        if (ec) return fail("Could not resolve controller path");
        auto inRoot = target.lexically_relative(root);
        if (inRoot.empty() || inRoot.is_absolute()) return fail("Controller path cannot leave Assets");
        for (const auto& part : inRoot) if (part == "..") return fail("Controller path cannot leave Assets");
        const bool exists = fs::exists(target, ec);
        if (ec) return fail("Could not check controller destination");
        if (createNew && (exists || fs::exists(fs::path(target.string() + ".meta")))) return fail("Save As needs a new filename; existing assets are not overwritten");
        if (!createNew && !exists) return fail("Controller disappeared from disk; use Save As");
        const std::string temporary = path + ".animator-write.tmp";
        const auto temporaryFile = fs::path(target.string() + ".animator-write.tmp");
        if (fs::exists(temporaryFile, ec) || ec) return fail("Controller temporary save path is already in use");
        if (!m_assets.WriteText(temporary, json)) {
            fs::remove(temporaryFile, ec); return fail("Could not write controller temporary file");
        }
        // Standard filesystem rename replaces an existing file atomically on the
        // same volume. A failed write/rename leaves the saved controller intact.
        fs::rename(temporaryFile, target, ec);
        if (ec) { fs::remove(temporaryFile, ec); return fail("Could not replace controller file"); }
        if (!m_assets.Refresh()) return fail("Controller written but Assets refresh failed");
        RefreshCatalog();
        const auto id = GetIdForPath(path);
        if (!id) return fail("Controller written but missing from refreshed index");
        const auto previous = m_controllers.find(id);
        if (previous == m_controllers.end() || !previous->second || !SameAnimatorControllerDefinition(*previous->second, decoded.controller))
            m_controllers[id] = std::make_shared<AnimatorController>(std::move(decoded.controller));
        m_warned.erase(id);
        if (error) error->clear();
        return id;
    }

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

    bool ResourceManager::IsMaterialFile(AssetId id) const {

        if (id == 0) { return false; }
        const AssetEntry* entry = m_assets.FindByGuid(ToGuid(id));
        return entry && entry->type == AssetType::Material;

    }   // ResourceManager::IsMaterialFile()

    std::vector<ResourceInfo> ResourceManager::List(ResourceKind kind) const {

        std::vector<ResourceInfo> builtins;
        std::vector<ResourceInfo> assets;

        auto isLoaded = [&](AssetId id) -> bool {
            switch (kind) {
                case ResourceKind::Mesh:     { auto it = m_meshes.find(id);    return it != m_meshes.end()    && it->second.IsValid(); }
                case ResourceKind::Material: { auto it = m_materials.find(id); return it != m_materials.end() && it->second.IsValid(); }
                case ResourceKind::Audio:    return false;   // never loaded here; ECSAudioSystem loads audio itself
                case ResourceKind::Model:    { auto it = m_models.find(id); return it != m_models.end() && bool(it->second); }
                case ResourceKind::AnimatorController: { auto it = m_controllers.find(id); return it != m_controllers.end() && bool(it->second); }
                default:                    { auto it = m_textures.find(id);  return it != m_textures.end()  && (it->second.srgb.IsValid() || it->second.linear.IsValid()); }
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
            case ResourceKind::AnimatorController:
                addAssets(AssetType::AnimatorController, "", false);
                break;
            case ResourceKind::Model:
                addAssets(AssetType::Model, "", false);
                break;
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
            case ResourceKind::Audio:
                addAssets(AssetType::Audio, "", false);
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
        renderer.modelRoot = InvalidModelIndex;

        renderer.meshAsset = id;
        renderer.modelAsset = 0;
        renderer.materialOverrides.clear();
        renderer.mesh      = GetMesh(id);
        CRU_DEBUG("[Resources] AssignMesh: {} -> handle {}", GetName(id),
                  renderer.mesh.IsValid() ? std::to_string(renderer.mesh.id) : std::string("INVALID"));

    }   // ResourceManager::AssignMesh()

    void ResourceManager::AssignMaterial(MeshRenderer& renderer, AssetId id) {

        renderer.materialAsset = id;
        renderer.material      = GetMaterial(id);

    }   // ResourceManager::AssignMaterial()

    void ResourceManager::Resolve(MeshRenderer& renderer) {

        // Called every frame by the Inspector, so log only when the handle actually changes.
        if (renderer.meshAsset != 0) {
            const MeshHandle mesh = GetMesh(renderer.meshAsset);
            if (mesh.id != renderer.mesh.id) {
                if (mesh.IsValid()) {
                    CRU_DEBUG("[Resources] Resolve: mesh {} -> handle {}", GetName(renderer.meshAsset), mesh.id);
                } else {
                    // GetMesh caches failures, and WarnOnce only reports the FIRST failure, so
                    // picking a broken asset a second time would otherwise be completely silent.
                    CRU_USER_WARN("[Resources] Resolve: mesh {} has no usable mesh, entity will not draw "
                             "(failed to load earlier; failures are cached until restart)",
                             GetName(renderer.meshAsset));
                }
            }
            renderer.mesh = mesh;
        }

        if (renderer.materialAsset != 0) {
            const MaterialHandle material = GetMaterial(renderer.materialAsset);
            if (material.id != renderer.material.id && !material.IsValid()) {
                CRU_USER_WARN("[Resources] Resolve: material {} is not usable, drawing with the default "
                         "material (failed to load earlier; failures are cached until restart)",
                         GetName(renderer.materialAsset));
            }
            renderer.material = material;
        }

    }   // ResourceManager::Resolve()

    // ------------------------------------------------------------------------------------------------------------- //
    //  LOADERS (compiled model resources and existing image/material formats)
    // ------------------------------------------------------------------------------------------------------------- //

    TextureHandle ResourceManager::GetModelTexture(AssetId id, uint32_t texture, bool isSRGB) {
        const auto model = GetModel(id);
        if (!model || texture >= model->textures.size()) {
            WarnOnce(id, "Embedded texture reference is not valid for this compiled model");
            return {};
        }
        const auto& image = model->images[model->textures[texture].image];
        try {
            const auto handle = m_renderer.CreateTextureFromMemory(image.bytes.data(), image.bytes.size(), isSRGB,
                "model:" + ToHex(id) + ":texture:" + std::to_string(texture) + (isSRGB ? ":srgb" : ":linear"));
            m_modelTextureSources[handle.id] = {id, texture};
            return handle;
        } catch (const std::exception& error) {
            WarnOnce(id, std::string("Embedded texture could not be uploaded: ") + error.what());
            return {};
        }
    }

    std::shared_ptr<const RenderedModel> ResourceManager::GetRenderedModel(AssetId id) {
        if (auto it = m_renderedModels.find(id); it != m_renderedModels.end()) return it->second;
        m_renderedModels[id] = nullptr;
        auto definition = GetModel(id);
        if (!definition) return {};
        try {
            auto result = std::make_shared<RenderedModel>();
            result->definition = definition;
            result->materials.reserve(definition->materials.size());
            const std::string prefix = "model:" + ToHex(id) + ":";
            for (const auto& source : definition->materials) {
                MaterialData material;
                material.factors.albedoTint = source.baseColorFactor;
                material.factors.emissiveFactor = source.emissiveFactor;
                material.factors.metallicFactor = source.metallicFactor;
                material.factors.roughnessFactor = source.roughnessFactor;
                material.doubleSided = source.doubleSided;
                material.alphaCutoff = source.alphaMode == ModelAlphaMode::Mask ? std::clamp(source.alphaCutoff, 0.0f, 1.0f) :
                    source.alphaMode == ModelAlphaMode::Blend ? 0.5f : 0.0f;
                if (source.alphaMode == ModelAlphaMode::Blend) {
                    CRU_INFO("[Resources] Model {}: alphaMode BLEND rendered as cut-out at 0.5 (no blended materials yet)", id);
                }
                auto texture = [&](const ModelTextureInfo& info, bool srgb) -> TextureHandle {
                    if (info.texture == InvalidModelIndex) return {};
                    return GetModelTexture(id, info.texture, srgb);
                };
                material.albedo = texture(source.baseColor, true);
                material.normal = texture(source.normal, false);
                material.metallicRoughness = texture(source.metallicRoughness, false);
                material.emissive = texture(source.emissive, true);
                material.occlusion = texture(source.occlusion, false);
                result->materials.push_back(m_renderer.CreateMaterial(material));
            }
            result->meshes.resize(definition->meshes.size());
            result->bounds.resize(definition->meshes.size());
            for (size_t mesh = 0; mesh < definition->meshes.size(); ++mesh) {
                for (size_t primitive = 0; primitive < definition->meshes[mesh].primitives.size(); ++primitive) {
                    const auto& source = definition->meshes[mesh].primitives[primitive];
                    result->bounds[mesh].push_back(BuildModelPrimitiveBounds(source));
                    MeshData geometry;
                    geometry.indices = source.indices;
                    geometry.vertices.reserve(source.vertices.size());
                    for (const auto& vertex : source.vertices) {
                        Vertex converted{vertex.position, vertex.normal, vertex.uv, glm::vec3(vertex.color), vertex.tangent};
                        converted.joints = vertex.joints; converted.weights = vertex.weights;
                        geometry.vertices.push_back(converted);
                    }
                    result->meshes[mesh].push_back(m_renderer.CreateMesh(geometry,
                        prefix + std::to_string(mesh) + ":" + std::to_string(primitive)));
                }
            }
            m_renderedModels[id] = result;
            return result;
        } catch (const std::exception& error) {
            WarnOnce(id, std::string("Complete model upload failed: ") + error.what());
            return {};
        }
    }

    bool ResourceManager::LoadModel(AssetId modelId) {

        const AssetId materialId = DeriveSubAssetId(modelId, "material");
        m_subAssets[materialId]  = SubAsset{ modelId, ResourceKind::Material };
        m_meshes[modelId]        = MeshHandle{};   // pessimistic: stays Invalid unless we succeed

        // Keep the legacy first-primitive/material GUID aliases while sharing the
        // complete model's GPU resources with animated/full-model callers.
        const auto rendered = GetRenderedModel(modelId);
        if (!rendered || rendered->meshes.empty() || rendered->meshes[0].empty()) {
            WarnOnce(modelId, "Model has no first primitive for the static mesh view");
            return false;
        }
        const MeshHandle mesh = rendered->meshes[0][0];
        m_meshes[modelId] = mesh;
        m_meshIds[mesh.id] = modelId;
        const auto index = rendered->definition->meshes[0].primitives[0].material;
        const MaterialHandle material = index == InvalidModelIndex ?
            m_renderer.GetDefaultMaterial() : rendered->materials[index];
        m_materials[materialId] = material;
        if (index != InvalidModelIndex) m_materialIds[material.id] = materialId;
        CRU_INFO("[Resources] Loaded compiled model {}", modelId);
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
        // so a freshly created blank .mat is still usable and can be saved over. Every key is
        // optional (missing = default), so files from before shading/cut-out/toon still load.
        MaterialData data;
        rapidjson::Document doc;
        doc.Parse(text.c_str(), text.size());
        if (text.empty() || doc.HasParseError() || !doc.IsObject()) {
            WarnOnce(id, "Material file empty or not valid JSON; using default values");
        } else {
            ReadFloats(doc, "albedoTint",     &data.factors.albedoTint.x,     4);
            ReadFloats(doc, "emissiveFactor", &data.factors.emissiveFactor.x, 3);
            ReadFloat(doc, "metallic",  data.factors.metallicFactor);
            ReadFloat(doc, "roughness", data.factors.roughnessFactor);

            if (auto it = doc.FindMember("shading"); it != doc.MemberEnd() && it->value.IsString()) {
                data.shading = ShadingFromName(it->value.GetString());
            }
            ReadFloat(doc, "alphaCutoff", data.alphaCutoff);
            ReadBool(doc,  "doubleSided", data.doubleSided);
            if (auto toon = doc.FindMember("toon"); toon != doc.MemberEnd() && toon->value.IsObject()) {
                const rapidjson::Value& t = toon->value;
                ReadBool(t,   "ownEdge",        data.toon.ownEdge);
                ReadFloat(t,  "threshold",      data.toon.threshold);
                ReadFloat(t,  "softness",       data.toon.softness);
                ReadBool(t,   "ownShadeColour", data.toon.ownShadeColour);
                ReadFloats(t, "shadeColour",    &data.toon.shadeColour.x, 3);
                ReadFloats(t, "litColour",      &data.toon.litColour.x,   3);
                ReadFloat(t,  "outline",        data.toon.outline);
            }

            if (auto textures = doc.FindMember("textures"); textures != doc.MemberEnd() && textures->value.IsObject()) {
                for (const SlotDesc& slot : kSlots) {
                    auto it = textures->value.FindMember(slot.key);
                    if (it == textures->value.MemberEnd()) continue;
                    if (it->value.IsString()) {
                        data.*slot.member = GetTexture(FromHex(it->value.GetString()), slot.isSRGB);
                    } else if (it->value.IsObject()) {
                        const auto parent = it->value.FindMember("model");
                        const auto index = it->value.FindMember("texture");
                        if (parent != it->value.MemberEnd() && parent->value.IsString() &&
                            index != it->value.MemberEnd() && index->value.IsUint())
                            data.*slot.member = GetModelTexture(FromHex(parent->value.GetString()), index->value.GetUint(), slot.isSRGB);
                    }
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

        writer.Key("shading");     writer.String(ShadingName(data.shading));
        writer.Key("alphaCutoff"); writer.Double(data.alphaCutoff);
        writer.Key("doubleSided"); writer.Bool(data.doubleSided);
        writer.Key("toon");        // sRGB colours, as picked in the Inspector
        writer.StartObject();
        writer.Key("ownEdge");        writer.Bool(data.toon.ownEdge);
        writer.Key("threshold");      writer.Double(data.toon.threshold);
        writer.Key("softness");       writer.Double(data.toon.softness);
        writer.Key("ownShadeColour"); writer.Bool(data.toon.ownShadeColour);
        floats("shadeColour", &data.toon.shadeColour.x, 3);
        floats("litColour",   &data.toon.litColour.x,   3);
        writer.Key("outline");        writer.Double(data.toon.outline);
        writer.EndObject();

        // Standalone textures use asset GUIDs; embedded textures use a compiled-model GUID
        // and texture index so editable material copies retain their images after restart.
        writer.Key("textures");
        writer.StartObject();
        for (const SlotDesc& slot : kSlots) {
            const TextureHandle handle = data.*slot.member;
            const AssetId textureId = handle.IsValid() ? FindId(handle) : 0;
            writer.Key(slot.key);
            const auto embedded = m_modelTextureSources.find(handle.id);
            if (handle.IsValid() && textureId == 0 && embedded != m_modelTextureSources.end()) {
                writer.StartObject();
                writer.Key("model"); writer.String(ToHex(embedded->second.model).c_str());
                writer.Key("texture"); writer.Uint(embedded->second.texture);
                writer.EndObject();
                continue;
            }
            if (handle.IsValid() && textureId == 0) {
                // A procedural/untracked runtime texture has no reloadable asset reference.
                CRU_USER_WARN("[Resources] SerializeMaterial: '{}' texture has no asset id - saved as empty", slot.key);
            }
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
            CRU_USER_ERROR("[Resources] Could not write material {}", path);
            return 0;
        }

        // Re-index so the new file gets its .meta (and GUID) like any other asset.
        m_assets.Refresh();
        RefreshCatalog();
        const AssetId id = GetIdForPath(path);
        if (id == 0) {
            CRU_USER_ERROR("[Resources] Wrote {} but it did not appear in the asset index", path);
            return 0;
        }
        CRU_USER_INFO("[Resources] Created material asset {} ({:016X})", path, id);

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
            CRU_USER_WARN("[Resources] SaveMaterial: {} is not a loaded .mat material", GetName(id));
            return false;
        }

        const std::string path = entry->virtualPath;   // copy: entry pointers die on Refresh()
        const bool ok = m_assets.WriteText(path, SerializeMaterial(m_renderer.GetMaterialData(it->second)));
        if (!ok) { CRU_USER_ERROR("[Resources] Could not write material {}", path); }
        else     { CRU_USER_INFO("[Resources] Saved material {}", path); }
        return ok;

    }   // ResourceManager::SaveMaterial()

}   // NAMESPACE CRUCIBLE
