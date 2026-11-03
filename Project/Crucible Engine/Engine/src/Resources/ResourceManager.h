#pragma once
/*************************************************************************
* \file:         ResourceManager.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                Turns asset GUIDs into shared CPU model definitions or
*                loaded Renderer handles, remembering each handle's GUID.
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
*                  - Meshes: Model assets (.cmodel) - the model's own GUID is
*                    its mesh.
*                  - Models: GetModel returns the complete immutable CPU
*                    definition. GetMesh keeps the first-primitive view.
*                  - Materials: .mat assets (JSON, see SaveMaterial), and
*                    each .cmodel's embedded material as a derived sub-asset
*                    (id = DeriveSubAssetId(modelId, "material")).
*                  - Textures: Texture assets (.png/.jpg/...), sRGB or linear.
*                  - Cubemaps: Texture assets loaded as an equirectangular
*                    panorama (.hdr or LDR) for Skybox.
*                  - Fonts: compiled .cfont files (see Text/FontData.h),
*                    with their atlas uploaded. A .ttf/.otf id resolves to
*                    the .cfont next to it, once it has been compiled.
*                Loads are synchronous and on demand (first Get). After
*                every scene change Core calls UnloadUnused(), which frees
*                whatever the new scene no longer refers to (models,
*                materials, textures, fonts and CPU caches); asking for one
*                again simply loads it again.
*
*                Models load only compiled .cmodel resources. Source GLB parsing
*                belongs to CrucibleModelCompiler; callers retain their GUIDs.
*                Image and material loading retain their existing formats.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#include <cstdint>
#include <filesystem>
#include <map>
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
#include "Audio/AudioSystem.h"   // AudioClip, AudioClipLoadType (no FMOD types)

namespace Crucible {

    class AssetManager;
    class Renderer;
    struct MeshRenderer;
    struct MaterialData;
    struct ModelResource;
    struct ModelPose;
    struct RenderedModel;
    struct AnimatorController;
    struct StateGraph;
    class Prefab;

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
        Audio,     // audio files - see GetAudioClip()
        Font,       // compiled fonts (.cfont) - see GetFont()
        Model,     // complete CPU model definition; GetModel does not upload GPU resources
        AnimatorController,
        StateGraph // an enemy's state machine (.stategraph) - see GetStateGraph()
    };

    /*************************************************************************
     \brief  One pickable entry, as returned by ResourceManager::List().
     \details
        - id:      what to store in a component (MeshRenderer::meshAsset...).
        - name:    display label ("Cube", "Models/Cannon.cmodel", ...).
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
     \brief  One resource currently held in memory, as returned by
             ResourceManager::ListLoaded() (the editor's Resources tab).
     \details
        - kind:    "Model", "Material", "Texture", "Font", ... (display only).
        - failed:  the load failed; remembered so it is not retried every
                   frame.
        - pinned:  kept loaded by KeepLoaded() even if no scene uses it.
     *************************************************************************/
    struct LoadedResourceInfo {
        AssetId     id = 0;
        const char* kind = "";
        std::string name;
        bool        builtin = false;
        bool        failed = false;
        bool        pinned = false;
    };

    /*************************************************************************
     \brief  What one ResourceManager::UnloadUnused() call freed.
     *************************************************************************/
    struct UnloadSummary {
        std::size_t models = 0;      //!< models freed with their meshes and materials
        std::size_t materials = 0;   //!< .mat materials
        std::size_t textures = 0;    //!< texture assets and model-embedded textures
        std::size_t fonts = 0;       //!< fonts (and their atlases)
        std::size_t skyboxes = 0;    //!< skybox cubemaps (with their lighting maps)
        std::size_t audioClips = 0;  //!< audio clips (one per file per load type)
        std::size_t cached = 0;      //!< CPU-only definitions (models, prefabs, animator controllers)
        std::size_t Total() const { return models + materials + textures + fonts + skyboxes + audioClips + cached; }
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

        // Complete, shared CPU definition from this one asset. Null on failure;
        // failed loads are cached like GetMesh. Own a shared_ptr while using it.
        // No node poses, GPU uploads or animation playback are created here.
        // Existing GetMesh/GetMaterial still expose the first primitive only.
        std::shared_ptr<const ModelResource> GetModel(AssetId id);
        std::shared_ptr<const ModelPose> GetModelRestPose(AssetId id);
        std::shared_ptr<const AnimatorController> GetAnimatorController(AssetId id);
        // A .stategraph, loaded and cached like an Animator Controller (failures warn
        // once and are cached). Own the shared_ptr while using it: an enemy's running
        // copy keeps its graph alive even if the cache drops it.
        std::shared_ptr<const StateGraph> GetStateGraph(AssetId id);
        // Saves an engine-owned text asset (.behaviorgraph, .stategraph) the safe way
        // WriteAnimatorControllerFile does: a temporary file swapped in, then the index
        // refreshed. createNew refuses to overwrite (Save As); otherwise the file must
        // already exist (Save). The caller checks the contents are valid first. Drops any
        // cached state graph for it, so the next GetStateGraph reads the new file.
        // Returns the asset id, or 0 with the reason in `error`.
        AssetId SaveTextAsset(const std::string& virtualPath, const std::string& text, bool createNew, std::string* error = nullptr);
        // Editor asset saves preserve GUIDs and publish a new immutable definition.
        // Existing instances retain the previous definition until their next reset.
        bool SaveAnimatorController(AssetId id, const std::string& json, std::string* error = nullptr);
        AssetId CreateAnimatorControllerAsset(const std::string& virtualPath, const std::string& json,
                                             std::string* error = nullptr);
        std::shared_ptr<const RenderedModel> GetRenderedModel(AssetId id);
        TextureHandle GetModelTexture(AssetId model, uint32_t texture, bool isSRGB);

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

        // ---- Audio ------------------------------------------------------------------------

        /*********************************************************************
         \brief  Lends the ResourceManager the audio engine clips are loaded
                 into. ECSAudioSystem calls it with its Audio after Init(),
                 and with nullptr before Audio::Shutdown() - which first
                 unloads every clip loaded here.
         \param[in] audio The audio engine, or nullptr.
         *********************************************************************/
        void AttachAudio(Audio* audio);

        /*********************************************************************
         \brief  An audio clip, loaded the first time it is asked for and
                 shared by every AudioSource that names the same file and
                 load type.
         \param[in] id       The audio file's asset id.
         \param[in] loadType DecompressOnLoad (short effects, decoded into
                             memory) or Streaming (music, read from disk
                             while it plays - FMOD opens the file itself).
         \return The clip, or an invalid handle if id is 0, not an audio
                 file, failed to load (warned once), or no audio engine is
                 attached. Freed by UnloadUnused() like everything else; ask
                 again each time you play (handles of freed clips just stop
                 working, they never play another clip).
         *********************************************************************/
        AudioClip GetAudioClip(AssetId id, AudioClipLoadType loadType);

        /// The font UIText uses when it names none (or its own can't be loaded): the compiled
        /// font itself, so a packaged game does not need the .ttf (compile
        /// Fonts/LiberationSans-Regular.ttf in the Font Importer to rebuild it).
        static constexpr const char* m_defaultFontPath = "Fonts/LiberationSans-Regular.cfont";

        /*********************************************************************
         \brief  GetFont() for m_defaultFontPath.
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
         \brief  Loads a texture again from its file - call after it was
                 compiled again (the editor's Texture Importer). Materials
                 that sampled the old texture switch to the new one.
         \param[in] id The image's asset id.
         *********************************************************************/
        void ReloadTexture(AssetId id);

        /*********************************************************************
         \brief  Hot reload: after AssetManager::Refresh(), loads again every
                 loaded asset whose file changed on disk
                 (AssetManager::GetChangedOnLastRefresh), and lets every
                 earlier failed load try again. The editor's Asset Browser
                 calls it after each refresh.
         \return How many assets were reloaded.
         \details
            Textures: reloaded; materials using them switch over.
            .mat materials: updated in place, so every object using one
            changes at once. Fonts, prefabs, animator controllers, skyboxes:
            forgotten, so the next use loads the new file. Models: only
            reported - their meshes are held by the objects using them, so
            the scene must be opened again.
         *********************************************************************/
        std::size_t ReloadChanged();

        // ---- Unloading ------------------------------------------------------------------------

        /*********************************************************************
         \brief  Frees every loaded resource the scene does not refer to.
                 Core calls this after each scene change (OnSceneChanged).
         \param[in] sceneDocument The scene as JSON (SceneManager::
                     CaptureSnapshot of the open scene - unsaved edits too).
         \return What was freed.
         \details
            Kept: every asset id the scene mentions anywhere (any component
            field), the model behind a kept model material, the compiled
            font behind a kept .ttf, built-ins, the default font, ids pinned
            with KeepLoaded(), and any texture a live material still uses
            (including materials made in the Inspector). Everything else is
            freed: GPU memory a few frames later (Renderer::Destroy*), CPU
            caches at once. A freed resource is loaded again the next time
            anything asks for it - nothing breaks, it only costs the load.
            If the document can't be read nothing is freed.
         \note   Handles from before the call may now be dead: a dead mesh
                 draws nothing, a dead texture or material draws the
                 default, a dead skybox shows the built-in sky. Code that needs a resource asks by id (every Get*
                 here), so it gets a live one again.
         *********************************************************************/
        UnloadSummary UnloadUnused(const std::string& sceneDocument);

        /*********************************************************************
         \brief  Keeps a resource loaded through UnloadUnused() even when no
                 scene uses it - for editor tools showing it (e.g. the Font
                 Importer's atlas preview). Counted: call ReleaseKeepLoaded()
                 once per KeepLoaded().
         \param[in] id The id the tool loaded (for a font, the .cfont id).
         *********************************************************************/
        void KeepLoaded(AssetId id);
        void ReleaseKeepLoaded(AssetId id);   //!< \copydoc KeepLoaded

        /*********************************************************************
         \brief  Everything currently in memory here (loaded or remembered
                 as failed), sorted by kind then name - for the editor's
                 Resources tab.
         *********************************************************************/
        std::vector<LoadedResourceInfo> ListLoaded() const;

        // ---- Scenes and prefabs ------------------------------------------------------------
        //
        // Loaded by GUID like everything else, read through the AssetManager - so a renamed
        // or moved file still loads, and nothing outside here needs a path.

        /*********************************************************************
         \brief  Whether an id is a .scene in the project.
         \param[in] id Asset id.
         \return False for 0, an unknown id, or any other kind of asset.
         *********************************************************************/
        bool IsScene(AssetId id) const;

        /*********************************************************************
         \brief  Whether an id is an audio file in the project.
         \param[in] id Asset id.
         \return False for 0, an unknown id, or any other kind of asset.
         \details GetAudioClip() loads one; the editor's audio preview uses
                  this with GetFilePath(id) for its own private copy.
         *********************************************************************/
        bool IsAudio(AssetId id) const;

        /*********************************************************************
         \brief  The text of a .scene asset, read through the AssetManager.
         \param[in]  id   The scene's asset id (its .meta GUID).
         \param[out] text Replaced with the file's contents on success.
         \return False if id is 0, not a scene, or the file can't be read
                 (logged).
         \details Not cached: a scene is built into the World once per load
                  (SceneManager::RequestLoadAsset), not shared between
                  users, and the editor re-saves it all the time - a cached
                  copy would go stale.
         *********************************************************************/
        bool ReadScene(AssetId id, std::string& text);

        /*********************************************************************
         \brief  A prefab, loaded once and shared by every caller.
         \param[in] id The .cprefab's asset id.
         \return The prefab - copies made from it link back to id - or null
                 if id is 0, not a prefab, or failed to load (warned once).
         \note   Cached: call ReloadPrefab(id) after the file is re-saved.
                 A shared_ptr already handed out stays valid either way.
         *********************************************************************/
        std::shared_ptr<const Prefab> GetPrefab(AssetId id);

        /*********************************************************************
         \brief  A prefab read fresh from its file, bypassing the cache.
         \param[in] id The .cprefab's asset id.
         \return A new Prefab, or null on failure (logged).
         \details For editor tools that compare versions of a prefab file
                  (prefab overrides) and must see it as it is on disk now.
         *********************************************************************/
        std::shared_ptr<Prefab> ReadPrefab(AssetId id);

        /*********************************************************************
         \brief  Forgets the cached prefab, so the next GetPrefab() reads the
                 file again - call after the prefab file is re-saved.
         \param[in] id The .cprefab's asset id.
         *********************************************************************/
        void ReloadPrefab(AssetId id);

        /*********************************************************************
         \brief  The file an asset lives in, on this machine.
         \param[in] id Asset id.
         \return The absolute path, or empty if id is 0 or unknown.
         \details For editor tools that write a resource back to its own
                  file - SceneManager's Save after RequestLoadAsset. Loading
                  never needs it.
         *********************************************************************/
        std::filesystem::path GetFilePath(AssetId id) const;

        /*********************************************************************
         \brief  The asset id for a virtual path (0 if not indexed).
         \param[in] virtualPath e.g. "Models/Cannon.cmodel".
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
                 (e.g. a .cmodel's material), stable across runs and machines.
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
        AssetId WriteAnimatorControllerFile(const std::string& path, const std::string& json,
                                           bool createNew, std::string* error);
        struct SubAsset { AssetId parent = 0; ResourceKind kind = ResourceKind::Material; };
        struct TextureSlots { TextureHandle srgb{}; TextureHandle linear{}; bool triedSrgb = false; bool triedLinear = false; };

        // Loaders - the only functions that understand file formats.
        bool LoadModel(AssetId modelId);                         // fills m_meshes[modelId] + embedded material
        MaterialHandle LoadMaterialFile(AssetId id);             // .mat (JSON)
        bool ReloadMaterialFile(AssetId id);                     // .mat read again into its existing handle
        void ForgetFailures();                                   // every remembered failed load may be tried again
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
        std::unordered_map<AssetId, std::shared_ptr<const ModelResource>> m_models; // null = failed CPU import
        std::unordered_map<AssetId, std::shared_ptr<const ModelPose>> m_modelRestPoses;
        std::unordered_map<AssetId, std::shared_ptr<const AnimatorController>> m_controllers;
        std::unordered_map<AssetId, std::shared_ptr<const StateGraph>> m_stateGraphs;   // null = failed load
        std::unordered_map<AssetId, std::shared_ptr<const RenderedModel>> m_renderedModels;
        std::unordered_map<AssetId, MaterialHandle> m_materials;   // Invalid value = failed load
        std::unordered_map<AssetId, TextureSlots>   m_textures;
        std::unordered_map<AssetId, CubemapHandle>  m_cubemaps;    // Invalid value = failed load
        std::unordered_map<AssetId, std::unique_ptr<FontAsset>> m_fonts;   // keyed by .cfont id; null = failed load
        std::unordered_map<AssetId, std::shared_ptr<const Prefab>> m_prefabs;   // null = failed load (until ReloadPrefab)

        std::unordered_map<std::uint32_t, AssetId>  m_meshIds;      // handle.id -> asset
        std::unordered_map<std::uint32_t, AssetId>  m_materialIds;
        std::unordered_map<std::uint32_t, AssetId>  m_textureIds;
        struct ModelTextureSource { AssetId model; uint32_t texture; };
        std::unordered_map<uint32_t, ModelTextureSource> m_modelTextureSources;

        std::unordered_map<AssetId, bool>           m_warned;
        std::unordered_map<AssetId, std::uint32_t>  m_pins;         // KeepLoaded counts

        Audio* m_audio = nullptr;                                   // lent by ECSAudioSystem (AttachAudio)
        std::map<std::pair<AssetId, AudioClipLoadType>, AudioClip> m_audioClips;   // invalid value = failed load
    };

#ifdef _MSC_VER
#pragma warning(pop)
#endif

}   // NAMESPACE CRUCIBLE
