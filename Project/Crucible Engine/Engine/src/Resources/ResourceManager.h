#pragma once
/*************************************************************************
* \file:         ResourceManager.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                Turns asset GUIDs into loaded engine resources (Renderer
*                handles), and remembers which GUID every handle came from.
*
* \details
*                Sits between AssetManager (files, GUIDs, bytes - owned by
*                the asset pipeline) and Renderer (GPU handles - knows
*                nothing about files):
*
*                  AssetManager            ResourceManager           Renderer
*                  GUID -> path -> bytes   GUID -> handle (cache)    CreateMesh/...
*
*                Only uses AssetManager's read-only public API (FindByGuid,
*                GetByType, ReadBytes, Resolve) plus WriteText/Refresh for
*                saving .mat files, so the asset pipeline can change freely
*                behind it.
*
*                Identity is the asset's GUID from its .meta file, stored as
*                a plain uint64 (AssetId == xresource::instance_guid::
*                m_Value) so ECS components can hold and serialize it with
*                no extra dependency. 0 means "none".
*
*                What this version supports (enough for the Inspector's mesh
*                and material pickers):
*                  - Built-in resources with fixed ids: the primitive meshes
*                    ("Cube", "Sphere", ...) and "Default Material".
*                  - Meshes: Model assets (.glb) - the model's own GUID is
*                    its mesh.
*                  - Materials: .mat assets (JSON, see SaveMaterial), and
*                    each .glb's embedded material as a derived sub-asset
*                    (id = DeriveSubAssetId(modelId, "material")).
*                  - Textures: Texture assets (.png/.jpg/...), sRGB or linear.
*                  - Cubemaps: Texture assets loaded as an equirectangular
*                    panorama (.hdr or LDR) for Skybox.
*                  - Fonts: compiled .cfont files (see Text/FontData.h),
*                    with their atlas uploaded. A .ttf/.otf id resolves to
*                    the .cfont next to it, once it has been compiled.
*                Loads are synchronous and on demand (first Get). Nothing is
*                unloaded yet - the Renderer has no Destroy* API - so
*                resources live until shutdown.
*
*                Source formats (PNG, GLB) are read here TEMPORARILY: once
*                the importer produces engine formats, only the per-type
*                Load* functions in the .cpp change; the public API and
*                every caller stay as they are.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "EngineExport.h"
#include "Graphics/MeshHandle.h"
#include "Graphics/MaterialHandle.h"
#include "Graphics/TextureHandle.h"
#include "Graphics/CubemapHandle.h"
#include "Text/FontData.h"

namespace Crucible {

    class AssetManager;
    class Renderer;
    struct MeshRenderer;
    struct MaterialData;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251)   // STL members in an exported class; Engine.dll and its clients share one toolchain
#endif

    /*************************************************************************
     \brief  An asset's identity: its .meta GUID (instance_guid::m_Value).
             0 = no asset.
     *************************************************************************/
    using AssetId = std::uint64_t;

    /*************************************************************************
     \brief  What kind of engine resource an AssetId resolves to.
     *************************************************************************/
    enum class ResourceKind : std::uint8_t {
        Mesh,
        Material,
        Texture,
        Audio,     // audio files; listed for the Inspector's picker only - the renderer never loads them
        Font       // compiled fonts (.cfont) - see GetFont()
    };

    /*************************************************************************
     \brief  One pickable entry, as returned by ResourceManager::List().
     \details
        - id:      what to store in a component (MeshRenderer::meshAsset...).
        - name:    display label ("Cube", "Models/Cannon.glb", ...).
        - builtin: true for engine-provided resources with no file.
        - loaded:  true if already resident (picking it costs nothing).
     *************************************************************************/
    struct ResourceInfo {
        AssetId      id = 0;
        ResourceKind kind = ResourceKind::Mesh;
        std::string  name;
        bool         builtin = false;
        bool         loaded = false;
    };

    /*************************************************************************
     \brief
        GUID -> loaded Renderer handle cache, with per-type loaders,
        built-in resources, reverse (handle -> GUID) lookup, and listing
        for editor pickers.

     \details
        Owned by Core (Core::GetResources()); constructed after the
        Renderer and AssetManager and holds references to both.

        Every Get* is "load on first use, cached afterwards". A load that
        fails is logged once and cached as a failure (Invalid handle), so a
        broken asset doesn't retry every frame; callers fall back (no draw
        for a mesh, the default material for a material).

     \par Inspector usage
     \code
        ResourceManager& rm = ...;   // EditorManager::GetInstance().GetResources()
        rm.Resolve(meshRenderer);    // fill handles for scene-loaded entities
        if (ImGui::BeginCombo("Mesh", rm.GetName(meshRenderer.meshAsset).c_str())) {
            for (const ResourceInfo& info : rm.List(ResourceKind::Mesh))
                if (ImGui::Selectable(info.name.c_str(), info.id == meshRenderer.meshAsset))
                    rm.AssignMesh(meshRenderer, info.id);
            ImGui::EndCombo();
        }
     \endcode
     *************************************************************************/
    class ENGINE_API ResourceManager {
    public:
        /*********************************************************************
         \brief  Stores the two systems this bridges. Call Init() before use.
         \param[in] assets   The engine's AssetManager (already Init()ed).
         \param[in] renderer The engine's Renderer.
         *********************************************************************/
        ResourceManager(AssetManager& assets, Renderer& renderer);

        ResourceManager(const ResourceManager&)            = delete;
        ResourceManager& operator=(const ResourceManager&) = delete;

        /*********************************************************************
         \brief  Registers the built-in resources (primitive meshes, default
                 material) and builds the sub-asset catalogue from the
                 AssetManager's index.
         *********************************************************************/
        void Init();

        /*********************************************************************
         \brief  Re-reads the AssetManager's index into the sub-asset
                 catalogue (call after AssetManager::Refresh(), e.g. when
                 new models were added). Already-loaded resources are kept.
         *********************************************************************/
        void RefreshCatalog();

        // ---- Resolve an id to a handle (loads on first use) --------------------------------

        /*********************************************************************
         \brief  The mesh for a built-in primitive or a Model asset.
         \param[in] id Asset id (0 -> Invalid).
         \return The mesh handle, or Invalid if id is 0/unknown/failed.
         *********************************************************************/
        MeshHandle     GetMesh(AssetId id);

        /*********************************************************************
         \brief  The material for "Default Material", a .mat asset, or a
                 model's embedded material (DeriveSubAssetId(model,
                 "material")).
         \param[in] id Asset id (0 -> Invalid).
         \return The material handle, or Invalid if id is 0/unknown/failed.
         \note   Materials are shared: every entity using the same id gets
                 the SAME handle, so editing it (Renderer::UpdateMaterial)
                 changes all of them - like any material asset.
         *********************************************************************/
        MaterialHandle GetMaterial(AssetId id);

        /*********************************************************************
         \brief  A Texture asset uploaded as sRGB (colour data: albedo,
                 emissive) or linear (normal, metallic-roughness, occlusion).
                 Cached separately per colour space.
         \param[in] id     Asset id (0 -> Invalid).
         \param[in] isSRGB Colour space to upload as.
         \return The texture handle, or Invalid if id is 0/unknown/failed.
         *********************************************************************/
        TextureHandle  GetTexture(AssetId id, bool isSRGB = true);

        /*********************************************************************
         \brief  A Texture asset (ideally a 2:1 .hdr panorama) as a skybox
                 cubemap - see Renderer::CreateCubemapFromEquirect().
         \param[in] id Asset id (0 -> Invalid).
         \return The cubemap handle, or Invalid if id is 0/unknown/failed.
         *********************************************************************/
        CubemapHandle  GetCubemap(AssetId id);

        /*********************************************************************
         \brief  Convenience for drag-and-drop payloads that carry a virtual
                 path: looks up the path's GUID, then GetTexture().
         \param[in] virtualPath e.g. "Textures/brick.png".
         \param[in] isSRGB      See GetTexture().
         \return The texture handle, or Invalid.
         *********************************************************************/
        TextureHandle  GetTextureByPath(const std::string& virtualPath, bool isSRGB = true);

        /*********************************************************************
         \brief  A compiled font with its atlas uploaded, for drawing text.
         \param[in] id A .cfont asset id, or a .ttf/.otf id (which resolves
                       to the .cfont with the same name next to it, e.g.
                       "Fonts/Arial.ttf" -> "Fonts/Arial.cfont").
         \return The font, or null if id is 0, unknown, not compiled yet,
                 or failed to load (logged once). Owned here: valid until
                 ReloadFont() for that font or this manager is destroyed.
         *********************************************************************/
        const FontAsset* GetFont(AssetId id);

        /// The font UIText uses when it names none (or its own can't be loaded).
        /// Whatever kDefaultFontPath is compiled to in the Font Importer is what you get.
        static constexpr const char* kDefaultFontPath = "Fonts/LiberationSans-Regular.ttf";

        /*********************************************************************
         \brief  GetFont() for kDefaultFontPath.
         \return Null if that font is missing or not compiled (warned once).
         *********************************************************************/
        const FontAsset* GetDefaultFont();

        /*********************************************************************
         \brief  Forgets a loaded (or failed) font so the next GetFont()
                 reads its .cfont again - call after the editor recompiles
                 it. Pointers from earlier GetFont() calls become invalid.
         \param[in] id Same ids as GetFont().
         \note   The old atlas stays on the GPU until shutdown (the Renderer
                 has no DestroyTexture yet).
         *********************************************************************/
        void ReloadFont(AssetId id);

        /*********************************************************************
         \brief  The asset id for a virtual path (0 if not indexed).
         \param[in] virtualPath e.g. "Models/Cannon.glb".
         \return The id.
         *********************************************************************/
        AssetId        GetIdForPath(const std::string& virtualPath) const;

        // ---- Reverse lookup (handle -> id), for saving and display -------------------------

        /*********************************************************************
         \brief  Which asset a handle was loaded from.
         \param[in] handle A handle this ResourceManager handed out.
         \return Its id, or 0 if the handle didn't come from here (e.g. a
                 procedural mesh or an Inspector-created material).
         *********************************************************************/
        AssetId FindId(MeshHandle handle) const;
        AssetId FindId(MaterialHandle handle) const;   //!< \copydoc FindId(MeshHandle) const
        AssetId FindId(TextureHandle handle) const;    //!< \copydoc FindId(MeshHandle) const

        /*********************************************************************
         \brief  Display name for an id: built-in name, asset virtual path,
                 or "<model path> (material)" for an embedded material.
         \param[in] id Asset id.
         \return "None" for 0, "(missing <hex>)" for an unknown id.
         *********************************************************************/
        std::string GetName(AssetId id) const;

        /*********************************************************************
         \brief  Whether an id is a .mat file on disk - i.e. a material whose
                 edits can be saved with SaveMaterial. False for 0, built-ins
                 ("Default Material") and materials embedded in a model.
         *********************************************************************/
        bool IsMaterialFile(AssetId id) const;

        /*********************************************************************
         \brief  Every resource of a kind that can be picked: built-ins
                 first, then indexed assets (loaded or not), sorted by name.
         \param[in] kind Which kind to list.
         \return The entries. Cheap - nothing is loaded by listing.
         *********************************************************************/
        std::vector<ResourceInfo> List(ResourceKind kind) const;

        // ---- MeshRenderer helpers for the Inspector ----------------------------------------

        /*********************************************************************
         \brief  Points `renderer` at a mesh asset: sets meshAsset AND the
                 resolved mesh handle together (so both stay consistent).
         \param[in,out] renderer The component to change.
         \param[in]     id       Mesh id (0 clears it).
         *********************************************************************/
        void AssignMesh(MeshRenderer& renderer, AssetId id);

        /*********************************************************************
         \brief  Same as AssignMesh(), for materialAsset/material.
         \param[in,out] renderer The component to change.
         \param[in]     id       Material id (0 clears it -> default
                                 material at draw time).
         *********************************************************************/
        void AssignMaterial(MeshRenderer& renderer, AssetId id);

        /*********************************************************************
         \brief  Fills in `renderer`'s mesh/material handles from its asset
                 ids (for entities loaded from a scene file, which only
                 carry ids). No-op for ids that are 0.
         \param[in,out] renderer The component to resolve.
         *********************************************************************/
        void Resolve(MeshRenderer& renderer);

        // ---- Material assets -----------------------------------------------------------------

        /*********************************************************************
         \brief  Writes a new .mat file, indexes it, and loads it.
         \param[in] virtualPath Where to write it, e.g.
                                "Materials/Brick.mat" (".mat" appended if
                                missing). Overwrites an existing file.
         \param[in] data        Initial contents. Texture slots are saved
                                only if their handles came from this
                                ResourceManager (FindId != 0).
         \return The new material's id, or 0 on failure.
         *********************************************************************/
        AssetId CreateMaterialAsset(const std::string& virtualPath, const MaterialData& data);

        /*********************************************************************
         \brief  Writes a loaded .mat material's CURRENT data (as edited via
                 Renderer::UpdateMaterial) back to its file.
         \param[in] id A .mat material id (not built-in, not embedded).
         \return false if id isn't a loaded .mat material or the write
                 failed.
         *********************************************************************/
        bool SaveMaterial(AssetId id);

        // ---- Ids ------------------------------------------------------------------------------

        /*********************************************************************
         \brief  Deterministic id for a resource embedded in another asset
                 (e.g. a .glb's material), stable across runs and machines.
         \param[in] parent The containing asset's id.
         \param[in] tag    Which sub-resource ("material").
         \return The derived id (LSB set, like every instance GUID).
         *********************************************************************/
        static AssetId DeriveSubAssetId(AssetId parent, const char* tag);

        /*********************************************************************
         \brief  Fixed id of a built-in resource, from its name ("Cube",
                 "Default Material", ...). Stable across runs, so scenes can
                 save it like any other id.
         \param[in] name Built-in resource name.
         \return The id.
         *********************************************************************/
        static AssetId BuiltinId(const char* name);

    private:
        struct SubAsset { AssetId parent = 0; ResourceKind kind = ResourceKind::Material; };
        struct TextureSlots { TextureHandle srgb{}; TextureHandle linear{}; bool triedSrgb = false; bool triedLinear = false; };

        // Loaders - the only functions that understand file formats.
        bool LoadModel(AssetId modelId);                         // fills m_meshes[modelId] + embedded material
        MaterialHandle LoadMaterialFile(AssetId id);             // .mat (JSON)
        std::string SerializeMaterial(const MaterialData& data) const;
        AssetId ResolveFontId(AssetId id) const;                 // .ttf/.otf -> its .cfont; .cfont -> itself; else 0
        std::unique_ptr<FontAsset> LoadFont(AssetId fontId);    // .cfont -> FontData + uploaded atlas

        void RegisterBuiltins();
        void WarnOnce(AssetId id, const std::string& message);

        AssetManager& m_assets;
        Renderer&     m_renderer;

        std::unordered_map<AssetId, std::string>    m_builtinNames;
        std::unordered_map<AssetId, SubAsset>       m_subAssets;

        std::unordered_map<AssetId, MeshHandle>     m_meshes;      // Invalid value = failed load
        std::unordered_map<AssetId, MaterialHandle> m_materials;   // Invalid value = failed load
        std::unordered_map<AssetId, TextureSlots>   m_textures;
        std::unordered_map<AssetId, CubemapHandle>  m_cubemaps;    // Invalid value = failed load
        std::unordered_map<AssetId, std::unique_ptr<FontAsset>> m_fonts;   // keyed by .cfont id; null = failed load

        std::unordered_map<std::uint32_t, AssetId>  m_meshIds;      // handle.id -> asset
        std::unordered_map<std::uint32_t, AssetId>  m_materialIds;
        std::unordered_map<std::uint32_t, AssetId>  m_textureIds;

        std::unordered_map<AssetId, bool>           m_warned;
    };

#ifdef _MSC_VER
#pragma warning(pop)
#endif

}   // NAMESPACE CRUCIBLE
