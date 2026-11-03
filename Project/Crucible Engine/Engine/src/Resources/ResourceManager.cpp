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
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string_view>
#include <unordered_set>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include "AssetPipe/Assetmanager.h"
#include "Resources/CompiledModel.h"
#include "Resources/CompiledTexture.h"
#include "Resources/CompiledSky.h"
#include "Resources/ImageDecoders.h"
#include "Resources/AnimatorControllerIO.h"
#include "AI/BehaviorGraphIO.h"        // kMaxBehaviorGraphBytes (GetStateGraph)
#include "AI/StateGraphIO.h"           // DecodeStateGraph (GetStateGraph)
#include "AssetPipe/AssetRegistry.h"   // HashFileContents (which version of a prefab file this is)
#include "Scene/Prefab.h"
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

        // An image file that is not compiled yet, decoded by the editor's image decoder
        // (Resources/ImageDecoders.h) and uploaded with mipmaps made now. Invalid where no decoder
        // is installed - the game, which reads compiled textures only.
        TextureHandle UploadSourceImage(Renderer& renderer, std::span<const unsigned char> file, bool isSRGB) {
            const auto decode = GetImageDecoders().decode;
            DecodedImage image;
            std::string error;
            if (!decode || !decode(file, 4, image, error)) { return {}; }
            return renderer.CreateTextureFromPixels(image.pixels.data(), image.width, image.height,
                isSRGB ? TexturePixelFormat::RGBA8_sRGB : TexturePixelFormat::RGBA8, true);
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
            WarnOnce(id, "Source model cannot be loaded at runtime; build CompiledModels to make its .cmodel in Resources");
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

    std::shared_ptr<const StateGraph> ResourceManager::GetStateGraph(AssetId id) {
        // The same shape as GetAnimatorController: cache first, a failure is cached
        // as null (and warned once) so a broken file isn't re-read every frame.
        if (!id) return {};
        if (auto found = m_stateGraphs.find(id); found != m_stateGraphs.end()) return found->second;
        m_stateGraphs[id] = nullptr;
        const auto* entry = m_assets.FindByGuid(ToGuid(id));
        std::vector<unsigned char> bytes;
        if (!entry || entry->type != AssetType::StateGraph || entry->sizeBytes > kMaxBehaviorGraphBytes ||
            !m_assets.ReadBytes(entry->virtualPath, bytes)) {
            WarnOnce(id, "Could not read State Graph (limit 4 MiB)"); return {};
        }
        auto decoded = DecodeStateGraph(bytes);
        if (!decoded.ok) { WarnOnce(id, "State Graph load failed: " + decoded.error); return {}; }
        std::shared_ptr<const StateGraph> graph = std::move(decoded.graph);
        m_stateGraphs[id] = graph;
        return graph;
    }

    AssetId ResourceManager::SaveTextAsset(const std::string& path, const std::string& text, bool createNew, std::string* error) {
        // WriteAnimatorControllerFile's steps, for any text asset: stay inside Assets,
        // never half-write (temporary file, then an atomic rename), then re-index.
        namespace fs = std::filesystem;
        const auto fail = [&](const std::string& reason) -> AssetId { if (error) *error = reason; return 0; };
        const fs::path relative(path);
        if (relative.empty() || relative.is_absolute() || relative.has_root_name() || relative.extension().empty())
            return fail("Choose a relative path with an extension, inside Assets");
        for (const auto& part : relative) if (part == "..") return fail("The path cannot leave Assets");
        std::error_code ec;
        const auto root = fs::weakly_canonical(m_assets.GetAssetRoot(), ec);
        if (ec) return fail("Could not resolve Assets root");
        const auto target = fs::weakly_canonical(root / relative, ec);
        if (ec) return fail("Could not resolve the path");
        auto inRoot = target.lexically_relative(root);
        if (inRoot.empty() || inRoot.is_absolute()) return fail("The path cannot leave Assets");
        for (const auto& part : inRoot) if (part == "..") return fail("The path cannot leave Assets");
        const bool exists = fs::exists(target, ec);
        if (ec) return fail("Could not check the destination");
        if (createNew && (exists || fs::exists(fs::path(target.string() + ".meta")))) return fail("Save As needs a new filename; existing assets are not overwritten");
        if (!createNew && !exists) return fail("The file disappeared from disk; use Save As");
        fs::create_directories(target.parent_path(), ec);   // Save As into a folder that doesn't exist yet
        const std::string temporary = path + ".write.tmp";
        const auto temporaryFile = fs::path(target.string() + ".write.tmp");
        if (fs::exists(temporaryFile, ec) || ec) return fail("The temporary save path is already in use");
        if (!m_assets.WriteText(temporary, text)) {
            fs::remove(temporaryFile, ec); return fail("Could not write the temporary file");
        }
        fs::rename(temporaryFile, target, ec);
        if (ec) { fs::remove(temporaryFile, ec); return fail("Could not replace the file"); }
        if (!m_assets.Refresh()) return fail("Written, but the Assets refresh failed");
        RefreshCatalog();
        const auto id = GetIdForPath(path);
        if (!id) return fail("Written, but missing from the refreshed index");
        m_stateGraphs.erase(id);   // an enemy picking it next reads the saved version
        m_warned.erase(id);
        if (error) error->clear();
        return id;
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
        if (entry->extension == CompiledSkyExtension) {
            WarnOnce(id, "This image is a skybox (its name has _skybox), so it is compiled to a .sky, not a texture");
            return {};
        }

        std::vector<unsigned char> bytes;
        if (!m_assets.ReadBytes(entry->virtualPath, bytes) || bytes.empty()) {
            WarnOnce(id, "Could not read texture");
            return {};
        }

        try {
            const std::string cacheKey = "asset:" + ToHex(id) + (isSRGB ? ":srgb" : ":linear");
            if (entry->extension == CompiledTextureExtension) {
                // Compiled by the texture compiler: GPU blocks + mipmaps, uploaded as they are.
                CompiledTexture compiled;
                std::string error;
                if (!DecodeCompiledTexture(bytes, id, compiled, error)) {
                    WarnOnce(id, "Compiled texture load failed: " + error);
                    return {};
                }
                handle = m_renderer.CreateTextureCompressed(compiled, isSRGB, cacheKey);
                CRU_INFO("[Resources] Loaded compiled texture {} ({}x{} {}, {} mips)", entry->virtualPath,
                         compiled.width, compiled.height, ToString(compiled.format), compiled.mips.size());
            } else {
                // Not compiled yet (added since the last build): only the editor can read the image itself.
                handle = UploadSourceImage(m_renderer, bytes, isSRGB);
                if (!handle.IsValid()) {
                    WarnOnce(id, "Image is not compiled yet - build the project (CompiledTextures) to make its .bill");
                    return {};
                }
            }
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
        if (entry && entry->extension == CompiledSkyExtension) {
            // Baked by the texture compiler: only an upload.
            std::vector<unsigned char> bytes;
            CompiledSky sky;
            std::string error;
            if (!m_assets.ReadBytes(entry->virtualPath, bytes) || !DecodeCompiledSky(bytes, id, sky, error)) {
                WarnOnce(id, "Compiled skybox load failed: " + (error.empty() ? std::string("could not read the file") : error));
            } else {
                try {
                    handle = m_renderer.CreateCubemapCompiled(sky, std::filesystem::path(entry->virtualPath).stem().string());
                    CRU_INFO("[Resources] Loaded compiled skybox {} ({} per face {}, {} reflection levels)", entry->virtualPath,
                             sky.sky.width, ToString(sky.sky.format), sky.environment.mips.size());
                } catch (const std::exception& e) {
                    WarnOnce(id, std::string("Skybox load failed: ") + e.what());
                }
            }
            m_cubemaps[id] = handle;
            return handle;
        }

        // Not compiled as a sky (its name has no "_skybox", or it was added since the last build).
        // Bake it from the image itself - only where an image decoder is installed (the editor);
        // a packaged game shows the default sky (and Cook reports it).
        if (entry && entry->extension == CompiledTextureExtension) {
            entry = nullptr;
            for (const AssetEntry* candidate : m_assets.GetByType(AssetType::Texture)) {
                if (candidate->guid.m_Value == id && !AssetManager::IsCompiledExtension(candidate->extension)) { entry = candidate; break; }
            }
        }
        const auto decodeHdr = GetImageDecoders().decodeHdr;
        std::vector<unsigned char> bytes;
        DecodedImageHdr image;
        std::string error;
        if (!entry || entry->type != AssetType::Texture || !decodeHdr) {
            WarnOnce(id, "Skybox image is not compiled as a .sky - add _skybox to its file name and build the project");
        } else if (!m_assets.ReadBytes(entry->virtualPath, bytes) || !decodeHdr(bytes, image, error)) {
            WarnOnce(id, "Skybox load failed: " + (error.empty() ? std::string("could not read the image") : error));
        } else {
            if (!IsSkyboxSource(entry->virtualPath)) {
                WarnOnce(id, "Skybox image's name has no _skybox, so it is not compiled to a .sky - baked from the image now, and missing in a packaged game");
            }
            try {
                handle = m_renderer.CreateCubemapFromEquirect(image.rgba.data(), image.width, image.height, 512, entry->virtualPath);
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

    // ------------------------------------------------------------------------------------------------------------- //
    //  FONTS
    // ------------------------------------------------------------------------------------------------------------- //

    namespace {
        constexpr const char* kCompiledFontExtension = ".cfont";
    }

    AssetId ResourceManager::ResolveFontId(AssetId id) const {

        const AssetEntry* entry = m_assets.FindByGuid(ToGuid(id));
        if (!entry || entry->type != AssetType::Font) { return 0; }
        if (entry->extension == kCompiledFontExtension) { return id; }

        // A source font (.ttf/.otf): its compiled font sits next to it with the same name.
        const std::string& path = entry->virtualPath;
        const std::string compiled = path.substr(0, path.size() - entry->extension.size()) + kCompiledFontExtension;
        const AssetEntry* compiledEntry = m_assets.Find(compiled);
        return compiledEntry ? compiledEntry->guid.m_Value : 0;

    }   // ResourceManager::ResolveFontId()

    const FontAsset* ResourceManager::GetFont(AssetId id) {

        if (id == 0) { return nullptr; }

        const AssetId fontId = ResolveFontId(id);
        if (fontId == 0) {
            const AssetEntry* entry = m_assets.FindByGuid(ToGuid(id));
            if (entry && entry->type == AssetType::Font) {
                WarnOnce(id, "Font is not compiled yet - select it in the Asset Browser and press Compile");
            } else {
                WarnOnce(id, "Not a font asset");
            }
            return nullptr;
        }

        auto it = m_fonts.find(fontId);
        if (it == m_fonts.end()) {
            it = m_fonts.emplace(fontId, LoadFont(fontId)).first;   // null is cached too: no retry every frame
        }
        return it->second.get();

    }   // ResourceManager::GetFont()

    const FontAsset* ResourceManager::GetDefaultFont() {

        const AssetId id = GetIdForPath(m_defaultFontPath);
        if (id == 0) {
            static bool s_warned = false;
            if (!s_warned) {
                s_warned = true;
                CRU_USER_WARN("[Resources] The default font '{}' is missing from the Assets folder - text without a font won't draw",
                              m_defaultFontPath);
            }
            return nullptr;
        }
        return GetFont(id);

    }   // ResourceManager::GetDefaultFont()

    void ResourceManager::ReloadFont(AssetId id) {

        const AssetId fontId = ResolveFontId(id);
        if (fontId != 0) {
            // Free the old atlas (a few frames later - text drawn this frame still has it).
            if (auto it = m_fonts.find(fontId); it != m_fonts.end()) {
                if (it->second) { m_renderer.DestroyTexture(it->second->texture); }
                m_fonts.erase(it);
            }
            m_warned.erase(fontId);
        }
        m_warned.erase(id);

    }   // ResourceManager::ReloadFont()

    void ResourceManager::ReloadTexture(AssetId id) {

        m_warned.erase(id);
        const auto found = m_textures.find(id);
        if (found == m_textures.end()) { return; }   // not loaded: the next GetTexture reads the new file anyway
        const TextureSlots old = found->second;
        m_textures.erase(found);

        // Which materials sample the old texture, and in which slots.
        struct Use { MaterialHandle material; MaterialData data; };
        std::vector<Use> uses;
        for (std::uint32_t i = 0; i < m_renderer.GetMaterialCount(); ++i) {
            MaterialHandle material;
            material.id = i;
            if (!m_renderer.IsMaterialAlive(material)) { continue; }
            const MaterialData& data = m_renderer.GetMaterialData(material);
            for (const SlotDesc& slot : kSlots) {
                const TextureHandle texture = data.*slot.member;
                if ((old.srgb.IsValid() && texture == old.srgb) || (old.linear.IsValid() && texture == old.linear)) {
                    uses.push_back({ material, data });
                    break;
                }
            }
        }

        // Free the old one first: that also forgets its cache key, so the loads below make new ones.
        for (const TextureHandle texture : { old.srgb, old.linear }) {
            if (!texture.IsValid()) { continue; }
            m_textureIds.erase(texture.id);
            m_renderer.DestroyTexture(texture);
        }

        for (Use& use : uses) {
            for (const SlotDesc& slot : kSlots) {
                TextureHandle& texture = use.data.*slot.member;
                if (old.srgb.IsValid() && texture == old.srgb)          { texture = GetTexture(id, true); }
                else if (old.linear.IsValid() && texture == old.linear) { texture = GetTexture(id, false); }
            }
            m_renderer.UpdateMaterial(use.material, use.data);
        }

    }   // ResourceManager::ReloadTexture()

    std::size_t ResourceManager::ReloadChanged() {

        // A failed load is remembered so it isn't retried every frame - but a refresh is exactly
        // when the missing or broken file may have been fixed.
        ForgetFailures();

        std::size_t reloaded = 0;
        for (const xresource::instance_guid guid : m_assets.GetChangedOnLastRefresh()) {
            const AssetId id = guid.m_Value;
            if (m_textures.count(id) != 0) { ReloadTexture(id); ++reloaded; }
            if (auto it = m_materials.find(id); it != m_materials.end() && IsMaterialFile(id) && ReloadMaterialFile(id)) { ++reloaded; }
            if (m_fonts.count(id) != 0)       { ReloadFont(id); ++reloaded; }
            if (m_prefabs.count(id) != 0)     { ReloadPrefab(id); ++reloaded; }
            if (m_controllers.erase(id) != 0) { m_warned.erase(id); ++reloaded; }
            if (m_stateGraphs.erase(id) != 0) { m_warned.erase(id); ++reloaded; }   // next GetStateGraph reads the new file
            if (ReloadAudioClip(id))          { ++reloaded; }
            if (auto it = m_cubemaps.find(id); it != m_cubemaps.end()) {
                // Skybox components ask by id every frame: the next frame loads the new one.
                if (it->second.IsValid()) { m_renderer.DestroyCubemap(it->second); }
                m_cubemaps.erase(it);
                ++reloaded;
            }
            if (auto it = m_models.find(id); it != m_models.end() && it->second) {
                CRU_USER_INFO("[Resources] {} changed on disk - open the scene again to see the new model", GetName(id));
            }
        }
        if (reloaded != 0) { CRU_USER_INFO("[Resources] Hot reload: {} asset(s) loaded again", reloaded); }
        return reloaded;

    }   // ResourceManager::ReloadChanged()

    bool ResourceManager::ReloadMaterialFile(AssetId id) {

        // Read into a new material, then copy its data into the existing handle: every object
        // holding that handle sees the change, nothing has to be resolved again.
        const MaterialHandle current = m_materials[id];
        if (!current.IsValid()) { m_materials.erase(id); return false; }
        const MaterialHandle fresh = LoadMaterialFile(id);
        if (!fresh.IsValid()) { return false; }
        m_renderer.UpdateMaterial(current, m_renderer.GetMaterialData(fresh));
        m_renderer.DestroyMaterial(fresh);
        return true;

    }   // ResourceManager::ReloadMaterialFile()

    void ResourceManager::ForgetFailures() {

        m_warned.clear();
        for (auto it = m_textures.begin(); it != m_textures.end();) {
            TextureSlots& slots = it->second;
            if (slots.triedSrgb && !slots.srgb.IsValid())     { slots.triedSrgb = false; }
            if (slots.triedLinear && !slots.linear.IsValid()) { slots.triedLinear = false; }
            it = (!slots.srgb.IsValid() && !slots.linear.IsValid()) ? m_textures.erase(it) : std::next(it);
        }
        std::erase_if(m_meshes,      [](const auto& e) { return !e.second.IsValid(); });
        std::erase_if(m_materials,   [](const auto& e) { return !e.second.IsValid(); });
        std::erase_if(m_cubemaps,    [](const auto& e) { return !e.second.IsValid(); });
        std::erase_if(m_models,      [](const auto& e) { return !e.second; });
        std::erase_if(m_renderedModels, [](const auto& e) { return !e.second; });   // else GetRenderedModel/GetMesh never retry
        std::erase_if(m_modelRestPoses, [](const auto& e) { return !e.second; });
        std::erase_if(m_fonts,       [](const auto& e) { return !e.second; });
        std::erase_if(m_prefabs,     [](const auto& e) { return !e.second; });
        std::erase_if(m_controllers, [](const auto& e) { return !e.second; });
        std::erase_if(m_stateGraphs, [](const auto& e) { return !e.second; });
        std::erase_if(m_audioClips,  [](const auto& e) { return !e.second.IsValid(); });

    }   // ResourceManager::ForgetFailures()

    // ------------------------------------------------------------------------------------------------------------- //
    //  AUDIO
    // ------------------------------------------------------------------------------------------------------------- //

    void ResourceManager::AttachAudio(Audio* audio) {

        // Clips belong to the engine they were loaded into: free them there before switching.
        if (m_audio != nullptr) {
            for (const auto& [key, clip] : m_audioClips) { m_audio->UnloadClip(clip); }
        }
        m_audioClips.clear();
        m_audio = audio;

    }   // ResourceManager::AttachAudio()

    AudioClip ResourceManager::GetAudioClip(AssetId id, AudioClipLoadType loadType) {

        if (id == 0 || m_audio == nullptr) { return {}; }

        const auto key = std::make_pair(id, loadType);
        if (const auto found = m_audioClips.find(key); found != m_audioClips.end()) { return found->second; }

        AudioClip clip;
        if (!IsAudio(id)) {
            WarnOnce(id, "Not an audio file in the project - deleted, its .meta lost, or another kind of asset");
        } else {
            // FMOD opens the file itself (a stream keeps reading it while it plays), so it gets a
            // real path - as UTF-8, so an accented folder name doesn't hide the file.
            const std::u8string utf8 = GetFilePath(id).u8string();
            clip = m_audio->LoadClip(std::string(utf8.begin(), utf8.end()), loadType);   // logs why on failure
        }
        m_audioClips.emplace(key, clip);   // a failure is remembered too: reported once, not on every Play
        return clip;

    }   // ResourceManager::GetAudioClip()

    bool ResourceManager::ReloadAudioClip(AssetId id) {

        // Unload every load type of the file: the next GetAudioClip (each Play asks again) opens
        // the new one. A voice still playing the old clip stops.
        bool any = false;
        for (auto it = m_audioClips.begin(); it != m_audioClips.end();) {
            if (it->first.first != id) { ++it; continue; }
            if (m_audio && it->second.IsValid()) { m_audio->UnloadClip(it->second); }
            it = m_audioClips.erase(it);
            any = true;
        }
        if (any) { m_warned.erase(id); }
        return any;

    }   // ResourceManager::ReloadAudioClip()

    // ------------------------------------------------------------------------------------------------------------- //
    //  UNLOADING
    // ------------------------------------------------------------------------------------------------------------- //

    namespace {

        // Every value in a scene document that could be an asset id: unsigned integers (how
        // components save their *Asset fields) and 16-digit hex strings (how .mat files write
        // them). Collecting too much is harmless - only ids that are loaded here matter.
        void CollectIds(const rapidjson::Value& value, std::unordered_set<AssetId>& out) {
            if (value.IsObject()) {
                for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it) { CollectIds(it->value, out); }
            } else if (value.IsArray()) {
                for (const rapidjson::Value& element : value.GetArray()) { CollectIds(element, out); }
            } else if (value.IsUint64()) {
                if (value.GetUint64() != 0) { out.insert(value.GetUint64()); }
            } else if (value.IsString() && value.GetStringLength() == 16) {
                const char* text = value.GetString();
                if (std::all_of(text, text + 16, [](unsigned char c) { return std::isxdigit(c) != 0; })) {
                    out.insert(FromHex(text));
                }
            }
        }

    }   // anonymous namespace

    UnloadSummary ResourceManager::UnloadUnused(const std::string& sceneDocument) {

        UnloadSummary summary;

        rapidjson::Document document;
        document.Parse(sceneDocument.c_str(), sceneDocument.size());
        if (document.HasParseError()) {
            CRU_WARN("[Resources] UnloadUnused: could not read the scene document - nothing unloaded");
            return summary;
        }

        // 1. What stays: what the scene mentions, plus what that implies.
        std::unordered_set<AssetId> keep;
        CollectIds(document, keep);
        for (const auto& [id, name] : m_builtinNames) { keep.insert(id); }
        for (const auto& [id, count] : m_pins)        { keep.insert(id); }
        if (const AssetId font = GetIdForPath(m_defaultFontPath); font != 0) { keep.insert(font); }

        // A kept prefab keeps what it uses too - a script may spawn it later, and its meshes,
        // materials and sounds are nowhere in the scene document. Prefabs inside prefabs as well.
        std::vector<AssetId> prefabsToRead(keep.begin(), keep.end());
        std::unordered_set<AssetId> prefabsRead;
        while (!prefabsToRead.empty()) {
            const AssetId id = prefabsToRead.back();
            prefabsToRead.pop_back();
            const AssetEntry* entry = m_assets.FindByGuid(ToGuid(id));
            if (!entry || entry->type != AssetType::Prefab || !prefabsRead.insert(id).second) { continue; }
            std::string text;
            rapidjson::Document prefab;
            if (!m_assets.ReadText(entry->virtualPath, text)) { continue; }
            prefab.Parse(text.c_str(), text.size());
            if (prefab.HasParseError()) { continue; }
            std::unordered_set<AssetId> uses;
            CollectIds(prefab, uses);
            for (const AssetId used : uses) {
                if (keep.insert(used).second) { prefabsToRead.push_back(used); }
            }
        }

        std::vector<AssetId> implied;
        for (const AssetId id : keep) {
            if (auto sub = m_subAssets.find(id); sub != m_subAssets.end()) { implied.push_back(sub->second.parent); }   // model material -> its model
            if (const AssetId font = ResolveFontId(id); font != 0 && font != id) { implied.push_back(font); }          // .ttf -> its .cfont
        }
        keep.insert(implied.begin(), implied.end());
        const auto kept = [&](AssetId id) { return keep.count(id) != 0; };

        // 2. Models: their meshes and materials go with them; their textures are candidates (step 4).
        std::vector<TextureHandle> modelTextures;
        for (auto it = m_renderedModels.begin(); it != m_renderedModels.end();) {
            const AssetId modelId = it->first;
            if (kept(modelId) || !it->second) { ++it; continue; }   // a failed load stays remembered as failed

            for (const auto& primitives : it->second->meshes) {
                for (const MeshHandle mesh : primitives) { m_meshIds.erase(mesh.id); m_renderer.DestroyMesh(mesh); }
            }
            for (const MaterialHandle material : it->second->materials) {
                m_materialIds.erase(material.id);
                m_renderer.DestroyMaterial(material);
            }
            m_meshes.erase(modelId);                                    // the first-primitive alias (GetMesh)
            m_materials.erase(DeriveSubAssetId(modelId, "material"));   // the embedded-material alias (GetMaterial)
            for (const auto& [textureId, source] : m_modelTextureSources) {
                if (source.model == modelId) { TextureHandle texture; texture.id = textureId; modelTextures.push_back(texture); }
            }
            ++summary.models;
            it = m_renderedModels.erase(it);
        }

        // CPU-only definitions: shared_ptrs, so anyone still holding one keeps their copy.
        const auto dropUnkept = [&](auto& cache) {
            for (auto it = cache.begin(); it != cache.end();) {
                if (kept(it->first)) { ++it; continue; }
                if (it->second) { ++summary.cached; }
                it = cache.erase(it);
            }
        };
        dropUnkept(m_models);
        dropUnkept(m_modelRestPoses);
        dropUnkept(m_controllers);
        dropUnkept(m_stateGraphs);
        dropUnkept(m_prefabs);

        // 3. Material files (.mat). Built-ins and model materials are handled above.
        for (auto it = m_materials.begin(); it != m_materials.end();) {
            const AssetId id = it->first;
            if (kept(id) || m_builtinNames.count(id) != 0 || m_subAssets.count(id) != 0 || !it->second.IsValid()) { ++it; continue; }
            m_materialIds.erase(it->second.id);
            if (m_renderer.DestroyMaterial(it->second)) { ++summary.materials; }
            it = m_materials.erase(it);
        }

        // 4. Textures - but never one a live material still samples, whoever made that material
        //    (a kept .mat, a kept model, or one made in the Inspector).
        std::unordered_set<std::uint32_t> usedTextures;
        for (std::uint32_t i = 0; i < m_renderer.GetMaterialCount(); ++i) {
            MaterialHandle material;
            material.id = i;
            if (!m_renderer.IsMaterialAlive(material)) { continue; }
            const MaterialData& data = m_renderer.GetMaterialData(material);
            for (const SlotDesc& slot : kSlots) {
                const TextureHandle texture = data.*slot.member;
                if (texture.IsValid()) { usedTextures.insert(texture.id); }
            }
        }
        const auto inUse = [&](TextureHandle texture) { return texture.IsValid() && usedTextures.count(texture.id) != 0; };
        const auto freeTexture = [&](TextureHandle texture) {
            if (!texture.IsValid() || inUse(texture)) { return; }
            m_textureIds.erase(texture.id);
            m_modelTextureSources.erase(texture.id);
            if (m_renderer.DestroyTexture(texture)) { ++summary.textures; }
        };

        for (auto it = m_textures.begin(); it != m_textures.end();) {
            if (kept(it->first) || inUse(it->second.srgb) || inUse(it->second.linear)) { ++it; continue; }
            freeTexture(it->second.srgb);
            freeTexture(it->second.linear);
            it = m_textures.erase(it);
        }
        for (const TextureHandle texture : modelTextures) { freeTexture(texture); }

        // 5. Fonts (their atlas is a texture of their own).
        for (auto it = m_fonts.begin(); it != m_fonts.end();) {
            if (kept(it->first)) { ++it; continue; }
            if (it->second) {
                freeTexture(it->second->texture);
                ++summary.fonts;
            }
            it = m_fonts.erase(it);
        }

        // 6. Audio clips. Unloading one also stops any voice still playing it - only voices of
        //    the old scene's sources, which the audio system silences anyway.
        for (auto it = m_audioClips.begin(); it != m_audioClips.end();) {
            if (kept(it->first.first) || !it->second.IsValid()) { ++it; continue; }
            if (m_audio) { m_audio->UnloadClip(it->second); }
            ++summary.audioClips;
            it = m_audioClips.erase(it);
        }

        // 7. Skyboxes (cubemap + its lighting maps). Skybox components ask by id every frame.
        for (auto it = m_cubemaps.begin(); it != m_cubemaps.end();) {
            if (kept(it->first) || !it->second.IsValid()) { ++it; continue; }   // a failed load stays remembered as failed
            if (m_renderer.DestroyCubemap(it->second)) { ++summary.skyboxes; }
            it = m_cubemaps.erase(it);
        }

        if (summary.Total() != 0) {
            CRU_INFO("[Resources] Unloaded unused: {} models, {} materials, {} textures, {} fonts, {} skyboxes, {} audio clips, {} cached definitions",
                     summary.models, summary.materials, summary.textures, summary.fonts, summary.skyboxes, summary.audioClips, summary.cached);
        }
        return summary;

    }   // ResourceManager::UnloadUnused()

    void ResourceManager::KeepLoaded(AssetId id) {

        if (id != 0) { ++m_pins[id]; }

    }   // ResourceManager::KeepLoaded()

    void ResourceManager::ReleaseKeepLoaded(AssetId id) {

        auto it = m_pins.find(id);
        if (it == m_pins.end()) { return; }
        if (--it->second == 0) { m_pins.erase(it); }

    }   // ResourceManager::ReleaseKeepLoaded()

    std::vector<LoadedResourceInfo> ResourceManager::ListLoaded() const {

        std::vector<LoadedResourceInfo> out;
        const auto add = [&](AssetId id, const char* kind, bool ok) {
            out.push_back({ id, kind, GetName(id), m_builtinNames.count(id) != 0, !ok, m_pins.count(id) != 0 });
        };

        for (const auto& [id, mesh] : m_meshes) {
            if (m_builtinNames.count(id) != 0) { add(id, "Mesh", mesh.IsValid()); }   // model meshes are listed as their model
        }
        for (const auto& [id, model] : m_renderedModels) { add(id, "Model", model != nullptr); }
        for (const auto& [id, model] : m_models) {
            if (m_renderedModels.count(id) == 0) { add(id, "Model (CPU only)", model != nullptr); }
        }
        for (const auto& [id, material] : m_materials) {
            if (m_subAssets.count(id) == 0) { add(id, "Material", material.IsValid()); }   // model materials belong to their model
        }
        for (const auto& [id, slots] : m_textures)       { add(id, "Texture", slots.srgb.IsValid() || slots.linear.IsValid()); }
        for (const auto& [id, cubemap] : m_cubemaps)     { add(id, "Skybox", cubemap.IsValid()); }
        for (const auto& [id, font] : m_fonts)           { add(id, "Font", font != nullptr); }
        for (const auto& [id, controller] : m_controllers) { add(id, "Animator Controller", controller != nullptr); }
        for (const auto& [id, prefab] : m_prefabs)       { add(id, "Prefab", prefab != nullptr); }
        for (const auto& [key, clip] : m_audioClips) {
            add(key.first, key.second == AudioClipLoadType::Streaming ? "Audio (streamed)" : "Audio", clip.IsValid());
        }

        std::sort(out.begin(), out.end(), [](const LoadedResourceInfo& a, const LoadedResourceInfo& b) {
            const int byKind = std::string_view{ a.kind }.compare(b.kind);
            return byKind != 0 ? byKind < 0 : a.name < b.name;
        });
        return out;

    }   // ResourceManager::ListLoaded()

    // ------------------------------------------------------------------------------------------------------------- //
    //  SCENES / PREFABS
    // ------------------------------------------------------------------------------------------------------------- //

    bool ResourceManager::IsScene(AssetId id) const {

        const AssetEntry* entry = id != 0 ? m_assets.FindByGuid(ToGuid(id)) : nullptr;
        return entry != nullptr && entry->type == AssetType::Scene;

    }   // ResourceManager::IsScene()

    bool ResourceManager::IsAudio(AssetId id) const {

        const AssetEntry* entry = id != 0 ? m_assets.FindByGuid(ToGuid(id)) : nullptr;
        return entry != nullptr && entry->type == AssetType::Audio;

    }   // ResourceManager::IsAudio()

    bool ResourceManager::ReadScene(AssetId id, std::string& text) {

        if (id == 0) { return false; }

        const AssetEntry* entry = m_assets.FindByGuid(ToGuid(id));
        if (!entry || entry->type != AssetType::Scene) {
            CRU_USER_WARN("[Resources] {} is not a scene in the project", GetName(id));
            return false;
        }

        // Not cached (see the header): every load reads the file as it is now.
        if (!m_assets.ReadText(entry->virtualPath, text)) {
            CRU_USER_WARN("[Resources] Could not read scene '{}'", entry->virtualPath);
            return false;
        }
        return true;

    }   // ResourceManager::ReadScene()

    std::shared_ptr<Prefab> ResourceManager::ReadPrefab(AssetId id) {

        if (id == 0) { return nullptr; }

        const AssetEntry* entry = m_assets.FindByGuid(ToGuid(id));
        if (!entry || entry->type != AssetType::Prefab) {
            WarnOnce(id, "Not a prefab in the project");
            return nullptr;
        }

        std::string text;
        if (!m_assets.ReadText(entry->virtualPath, text)) {
            WarnOnce(id, "Could not read prefab");
            return nullptr;
        }

        // The same hash Prefab::Load(file) records, so an instance can tell which version of
        // the file it was made from (the editor's prefab overrides compare against it).
        const std::uint64_t hash = AssetRegistry::HashFileContents(std::filesystem::path{ entry->absolutePath });

        auto prefab = std::make_shared<Prefab>();
        if (!prefab->LoadFromAsset(text, id, hash, entry->absolutePath)) {
            return nullptr;   // Prefab logged why
        }
        return prefab;

    }   // ResourceManager::ReadPrefab()

    std::shared_ptr<const Prefab> ResourceManager::GetPrefab(AssetId id) {

        if (id == 0) { return nullptr; }
        if (auto found = m_prefabs.find(id); found != m_prefabs.end()) { return found->second; }

        std::shared_ptr<const Prefab> prefab = ReadPrefab(id);
        m_prefabs[id] = prefab;   // a failed load is remembered as null until ReloadPrefab
        return prefab;

    }   // ResourceManager::GetPrefab()

    void ResourceManager::ReloadPrefab(AssetId id) {

        m_prefabs.erase(id);
        m_warned.erase(id);

    }   // ResourceManager::ReloadPrefab()

    std::filesystem::path ResourceManager::GetFilePath(AssetId id) const {

        const AssetEntry* entry = id != 0 ? m_assets.FindByGuid(ToGuid(id)) : nullptr;
        return entry ? std::filesystem::path{ entry->absolutePath } : std::filesystem::path{};

    }   // ResourceManager::GetFilePath()

    std::unique_ptr<FontAsset> ResourceManager::LoadFont(AssetId fontId) {

        const AssetEntry* entry = m_assets.FindByGuid(ToGuid(fontId));
        if (!entry) { WarnOnce(fontId, "Font asset not found"); return nullptr; }

        std::vector<unsigned char> bytes;
        if (!m_assets.ReadBytes(entry->virtualPath, bytes) || bytes.empty()) {
            WarnOnce(fontId, "Could not read font");
            return nullptr;
        }

        auto font = std::make_unique<FontAsset>();
        std::string error;
        if (!ReadFont(std::span<const std::uint8_t>(bytes.data(), bytes.size()), font->data, error)) {
            WarnOnce(fontId, "Font load failed: " + error);
            return nullptr;
        }

        const FontAtlas& atlas = font->data.m_atlas;
        const TexturePixelFormat format = atlas.channels == 4 ? TexturePixelFormat::RGBA8   // MTSDF: data, not colour
                                                              : TexturePixelFormat::Alpha8; // SDF / Bitmap
        try {
            // No mipmaps: averaging neighbouring glyphs would make them bleed into each other.
            font->texture = m_renderer.CreateTextureFromPixels(atlas.pixels.data(), atlas.width, atlas.height, format, false);
        } catch (const std::exception& e) {
            WarnOnce(fontId, std::string("Font atlas upload failed: ") + e.what());
            return nullptr;
        }
        font->drawMode = DrawModeFor(font->data.m_outputType);

        // The pixels now live on the GPU (copied before CreateTextureFromPixels returned).
        font->data.m_atlas.pixels.clear();
        font->data.m_atlas.pixels.shrink_to_fit();

        CRU_INFO("[Resources] Font {} loaded: {}, {} glyphs, {}x{} atlas", entry->virtualPath,
                 ToString(font->data.m_outputType), font->data.m_glyphs.size(), atlas.width, atlas.height);
        return font;

    }   // ResourceManager::LoadFont()

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
                case ResourceKind::Audio: {
                    for (const auto& [key, clip] : m_audioClips) { if (key.first == id && clip.IsValid()) { return true; } }
                    return false;
                }
                case ResourceKind::Font:     { auto it = m_fonts.find(id);     return it != m_fonts.end()     && it->second != nullptr; }
                case ResourceKind::Model:    { auto it = m_models.find(id); return it != m_models.end() && bool(it->second); }
                case ResourceKind::AnimatorController: { auto it = m_controllers.find(id); return it != m_controllers.end() && bool(it->second); }
                case ResourceKind::StateGraph:         { auto it = m_stateGraphs.find(id); return it != m_stateGraphs.end() && bool(it->second); }
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
                // A source (.glb, .png) shares its guid with its compiled file (.cmodel, .bill):
                // list the one the id loads from, once.
                if (m_assets.FindByGuid(entry->guid) != entry) { continue; }
                const AssetId id = derived ? DeriveSubAssetId(fileId, "material") : fileId;
                assets.push_back({ id, kind, entry->virtualPath + suffix, false, isLoaded(id) });
            }
        };

        switch (kind) {
            case ResourceKind::AnimatorController:
                addAssets(AssetType::AnimatorController, "", false);
                break;
            case ResourceKind::StateGraph:
                addAssets(AssetType::StateGraph, "", false);
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
            case ResourceKind::Font:
                // Only compiled fonts can be drawn; a .ttf is picked by compiling it first.
                for (const AssetEntry* entry : m_assets.GetByType(AssetType::Font)) {
                    const AssetId id = entry->guid.m_Value;
                    if (id == 0 || entry->extension != kCompiledFontExtension) { continue; }
                    assets.push_back({ id, kind, entry->virtualPath, false, isLoaded(id) });
                }
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
                             "(failed to load earlier; fix the file, then Refresh the Asset Browser to retry)",
                             GetName(renderer.meshAsset));
                }
            }
            renderer.mesh = mesh;
        }

        if (renderer.materialAsset != 0) {
            const MaterialHandle material = GetMaterial(renderer.materialAsset);
            if (material.id != renderer.material.id && !material.IsValid()) {
                CRU_USER_WARN("[Resources] Resolve: material {} is not usable, drawing with the default "
                         "material (failed to load earlier; fix the file, then Refresh the Asset Browser to retry)",
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
            const std::string cacheKey = "model:" + ToHex(id) + ":texture:" + std::to_string(texture) + (isSRGB ? ":srgb" : ":linear");
            TextureHandle handle;
            if (image.mimeType == CompiledTextureMimeType) {
                // Compressed by the model compiler: uploaded as it is, no decoding.
                CompiledTexture compiled;
                std::string error;
                if (!DecodeCompiledTexture(image.bytes, id, compiled, error)) {
                    WarnOnce(id, "Embedded texture is damaged: " + error);
                    return {};
                }
                handle = m_renderer.CreateTextureCompressed(compiled, isSRGB, cacheKey);
                CRU_INFO("[Resources] Model {} texture {}: compiled ({}x{} {}, {} mips)", id, texture,
                         compiled.width, compiled.height, ToString(compiled.format), compiled.mips.size());
            } else {
                // A model compiled before textures were compressed: only the editor can read its PNG/JPEG.
                handle = UploadSourceImage(m_renderer, image.bytes, isSRGB);
                if (!handle.IsValid()) {
                    WarnOnce(id, "Embedded texture is an image file - build the project to compile the model again");
                    return {};
                }
            }
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
