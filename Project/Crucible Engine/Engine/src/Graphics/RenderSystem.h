#pragma once
/*************************************************************************
* \file:         RenderSystem.h
* \author:       Chewn Thing Kwan
* \par:          Email: kwan.c@digipen.edu
* \date          30 September 2026
* \version       1.0
* \brief
*                The engine's Renderer: owns the Vulkan context/swapchain/
*                pipeline/frame manager and every GPU-side mesh, texture,
*                material, and offscreen viewport resource.
*
* \details
*                Declares Renderer, the single class the rest of the
*                engine (and the Editor) talks to for anything GPU-related:
*                uploading meshes/textures, creating/updating materials,
*                creating offscreen viewports for Editor panels, submitting
*                per-frame draw calls, and driving the frame loop itself.
*                Renderer stays entirely unaware the ECS exists - see
*                RenderExtractSystem.h for the one place that bridges the
*                two.
*
* \history
*    -v1.0       initial implementation
*
**************************************************************************/

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
    #include <vulkan/vulkan_raii.hpp>
#else
    import vulkan_hpp;
#endif
#include <GLFW/glfw3.h>

// STL
#include <vector>
#include <array>
#include <deque>
#include <unordered_map>
#include <functional>
#include <span>

// Crucible Engine
#include "Systems/WindowManager.h"
#include "Math/Math.h"
 
#include "Vulkan/VulkanContext.h"
#include "Vulkan/VulkanSwapchain.h"
#include "Vulkan/RenderLayouts.h"
#include "Vulkan/VulkanFrameManager.h"
#include "Vulkan/VulkanResource.h"
#include "Vulkan/VulkanUploadQueue.h"

#include "Camera.h"
#include "RenderTypes.h"
#include "ViewTargets.h"
#include "Passes/BloomPass.h"
#include "Passes/TonemapPass.h"
#include "Passes/AmbientOcclusionPass.h"
#include "Passes/ShadowPass.h"
#include "Passes/OutlinePass.h"
#include "Passes/ToonOutlinePass.h"
#include "Passes/ParticlePass.h"
#include "Passes/DebugLinePass.h"
#include "Passes/ScenePass.h"
#include "Passes/UIPass.h"
#include "Passes/VolumetricFogPass.h"
#include "Passes/WaterPass.h"
#include "DrawList.h"
#include "RenderResources.h"
#include "EnvironmentMaps.h"
#include "GpuProfiler.h"
#include "SceneLights.h"
#include "Viewports.h"
#include "RenderScene.h"
#include "DebugDraw.h"
#include "UITypes.h"
#include "UITestOverlay.h"
#include "Spatial/SpatialIndex.h"
#include "Threading/WorkerPool.h"

// Imgui
#include "Vulkan/ImGuiContext.h"
#include "EngineExport.h"

namespace Crucible {

    /*************************************************************************
     \brief
        The engine's Renderer: owns the Vulkan context/swapchain/pipeline/
        frame manager and every GPU-side mesh, texture, material, and
        offscreen viewport resource.

     \details
        See the file header for Renderer's role in the engine. Internally
        organized into Meshes, Textures, Materials, Viewports, and Draw
        submission - see the section banners below and in the .cpp.
     *************************************************************************/
    class Renderer {

    public:
        /*********************************************************************
         \brief  Constructs the Vulkan context/swapchain/pipeline/frame
                 manager, uploads the default textures/material/primitive
                 meshes, and loads the demo scene's sample model.
         \param[in] window The window to render into.
         *********************************************************************/
        Renderer(WindowManager& window);

        /*********************************************************************
         \brief  Waits for the device to go idle before teardown.
         *********************************************************************/
        void ShutDown();

        /*********************************************************************
         \brief  Drains any pending async mesh
                 uploads, populates/sorts the render queue, and draws the
                 frame.
         \param[in] dt Frame delta time, in seconds.
         *********************************************************************/
        void Update(float dt);

        /*********************************************************************
         \brief  Acquires a swapchain image, records the command buffer for
                 this frame, and submits/presents it.
         *********************************************************************/
        void DrawFrame();


        /*********************************************************************
         \brief
            The main (game) camera: what the Game view shows, and what is
            drawn straight to the window in a standalone build.
         \details
            It belongs to the scene: RenderExtractSystem sets it every frame
            from the active CameraComponent entity (SetMainCamera), or
            clears it when the scene has none (ClearMainCamera). Game-role
            viewports (ViewportRole::Game) look through it. While
            HasMainCamera() is false the window pass and Game views draw no
            scene, only the clear colour and the game's UI (SubmitUI), and
            the Game view says "No cameras rendering". GetCamera() then
            returns the last camera that was set. Aspect ratio is
            overwritten from the render target each frame.
         *********************************************************************/
        void          SetMainCamera(const Camera& camera) { m_camera = camera; m_hasMainCamera = true; }
        void          ClearMainCamera()                   { m_hasMainCamera = false; }
        bool          HasMainCamera() const               { return m_hasMainCamera; }
        const Camera& GetCamera() const                   { return m_camera; }

        /*********************************************************************
         \brief
            Creates the mesh's GPU buffers right away and returns a handle
            for use in an entity's MeshRenderer component. The copy of the
            vertex/index data into those buffers is queued (see
            VulkanUploadQueue) and runs at the start of the next frame's
            command buffer, ahead of every draw - so the mesh is drawable
            from that frame on, and this call never waits on the GPU.

         \param[in] data      The CPU-side mesh to upload.
         \param[in] debugName Optional, purely descriptive name (see
                               GetMeshName()) - pass the source asset's own
                               name when you have one (a glTF mesh's
                               "name", say), otherwise leave it empty and a
                               generic "Mesh <id>" is used.
         \return A handle for use in an entity's MeshRenderer component
                 (ECS/Components/Components.h; extracted into a Submit()
                 call every frame by Graphics/RenderExtractSystem.h).

         \details
            Use for meshes needed right away: built-in primitives, or a
            handful of assets loaded at startup.
         *********************************************************************/
        ENGINE_API MeshHandle CreateMesh(const MeshData& data, const std::string& debugName = {});

        /*********************************************************************
         \brief
            Returns a handle immediately; the mesh's buffers are created
            and filled a few meshes per frame (two, see RenderResources),
            so the CPU-side work of a burst of meshes is spread out.

         \param[in] data      The CPU-side mesh to upload.
         \param[in] debugName Optional, purely descriptive name - see
                               CreateMesh().
         \return A handle, usable immediately (see \\details).

         \details
            Use this for meshes streamed in at runtime (e.g. the Resource
            Manager's .glb loader) so a burst of loaded models doesn't
            stall a single frame. The mesh simply isn't drawn until its
            upload completes.
         *********************************************************************/
        ENGINE_API MeshHandle CreateMeshAsync(MeshData data, const std::string& debugName = {});

        /*********************************************************************
         \brief
            Creates an empty mesh whose geometry the CPU rewrites as often as
            every frame (UpdateDynamicMesh) - for procedural, animated
            geometry such as vines.

         \param[in] debugName Optional name (see CreateMesh()).
         \return A handle; nothing is drawn until UpdateDynamicMesh() gives
                 it geometry.

         \details
            Draw it with Submit(), with world-space vertices and an identity
            model matrix if the geometry is built in world space; it is
            culled (main view and shadow cascades) with the bounds of its
            latest geometry. Each frame in flight has its own host-visible
            copy, refilled at the start of that frame only when the
            geometry changed. Not meant for render proxies (they keep the
            bounds from their last SetRenderProxy() call), and editor
            picking does not see it. There is no destroy: reuse handles.
         *********************************************************************/
        ENGINE_API MeshHandle CreateDynamicMesh(const std::string& debugName = {});

        /*********************************************************************
         \brief  Replaces a dynamic mesh's geometry, from the next frame drawn
                 on. The data is copied; empty spans = draw nothing.
         \throws std::invalid_argument if the handle is not a dynamic mesh,
                 or an index is out of range.
         *********************************************************************/
        ENGINE_API void UpdateDynamicMesh(MeshHandle handle, std::span<const Vertex> vertices, std::span<const uint32_t> indices);

        /*********************************************************************
         \brief
            Returns a mesh's human-readable debug name.

         \param[in] handle The mesh to look up.
         \return Reference to the mesh's debug name (or a shared
                 "(unknown mesh)" string for an invalid/out-of-range
                 handle).

         \details
            The Renderer is the source of truth for "which mesh is id 3" -
            not any one Editor panel. A name is descriptive metadata about
            a GPU resource, so it lives right next to the resource
            (GpuMesh::debugName) instead of a separate map some panel
            keeps in sync by hand: there's no single place in the Editor
            where "a mesh was just created" happens (a primitive in
            Renderer's own constructor, a .glb load, gameplay code later),
            so any panel-owned map would inevitably drift, and every panel
            wanting to show a name (today just Inspector, maybe Hierarchy
            or a future asset browser tomorrow) would otherwise need its
            own copy kept in sync independently. Centralizing it here also
            means it's available to anything that isn't a panel at all - a
            debug log line, a future vkSetDebugUtilsObjectNameEXT call for
            RenderDoc/Nsight captures, and so on.

         \note
            Returns a reference into the mesh list - same lifetime caveat as
            GetMaterialData(): valid until the next structural change to
            the mesh list, so don't hold it across frames.
         *********************************************************************/
        ENGINE_API const std::string& GetMeshName(MeshHandle handle) const;

        /*********************************************************************
         \brief  Object-space bounding box of a mesh's vertices.
         \param[in]  handle The mesh.
         \param[out] outMin Minimum corner.
         \param[out] outMax Maximum corner.
         \return false (outputs untouched) if the handle is invalid or the
                 mesh hasn't finished uploading.
         *********************************************************************/
        ENGINE_API bool GetMeshBounds(MeshHandle handle, glm::vec3& outMin, glm::vec3& outMax) const;

        /*********************************************************************
         \brief
            Loads an image file from disk and uploads it to the GPU,
            returning a handle for use in a MaterialData.

         \param[in] path   Path to the image file on disk.
         \param[in] isSRGB Should be true for color data (albedo,
                            emissive) and false for data textures (normal
                            maps, metallic-roughness, occlusion) - getting
                            this wrong silently double-gamma-corrects those
                            maps.
         \return A texture handle.

         \details
            Cached by resolved path - calling this twice with the same
            path returns the same handle instead of decoding/uploading
            twice.
         *********************************************************************/
        ENGINE_API TextureHandle CreateTexture(const std::string& path, bool isSRGB = true);

        /*********************************************************************
         \brief
            Same as CreateTexture() but from an already-loaded image
            file's bytes in memory (still a file format stb_image
            understands - PNG/JPG/etc, not raw pixels).

         \param[in] fileBytes Pointer to the encoded image file's bytes.
         \param[in] byteCount Number of bytes at fileBytes.
         \param[in] isSRGB    See CreateTexture().
         \param[in] cacheKey  Optional; pass one (e.g. the glTF image
                               index) to dedupe repeated calls the same way
                               the path-based overload does.
         \return A texture handle.

         \details
            e.g. a glTF texture whose image lives in a binary buffer view
            rather than as a standalone file.
         *********************************************************************/
        ENGINE_API TextureHandle CreateTextureFromMemory(const unsigned char* fileBytes, size_t byteCount,
            bool isSRGB = true, const std::string& cacheKey = std::string());

        /*********************************************************************
         \brief
            Creates a texture from raw texels (not an image file), e.g. a
            font atlas the font system has rasterized.
         \param[in] pixels   Tightly packed rows, top row first; copied
                              before this returns.
         \param[in] width    In texels.
         \param[in] height   In texels.
         \param[in] format   RGBA8_sRGB (colour images), RGBA8 (data, MSDF
                              atlases) or Alpha8 (one byte per texel, read
                              as (1, 1, 1, value): coverage or SDF atlases).
         \param[in] mipmaps  Also make the smaller levels. Leave off for
                              font atlases: averaging neighbouring glyphs
                              makes them bleed into each other.
         \return A texture handle, usable in a UIDrawCommand (or a material,
                 for the RGBA formats).
         \details
            Not cached: every call makes a new texture. Uploaded at the
            start of the next frame, like CreateTexture().
         \throws std::runtime_error for no pixels, a zero size, or a size
                 past the GPU's limit (usually 16384 per side).
         *********************************************************************/
        ENGINE_API TextureHandle CreateTextureFromPixels(const void* pixels, uint32_t width, uint32_t height,
            TexturePixelFormat format, bool mipmaps = false);

        /*********************************************************************
         \brief
            Creates a new material from the given texture bindings +
            scalar factors.

         \param[in] data The material's texture bindings and scalar
                          factors.
         \return A handle for an entity's MeshRenderer::material.

         \details
            Any texture left Invalid falls back to a neutral placeholder -
            see CreateDefaultTextures().
         *********************************************************************/
        ENGINE_API MaterialHandle CreateMaterial(const MaterialData& data);

        /*********************************************************************
         \brief
            Rewrites an existing material in place.

         \param[in] handle The material to update.
         \param[in] data   The material's new texture bindings and scalar
                            factors.

         \details
            This is what lets a designer change a material's texture or
            tint while the Editor is running without every entity using
            that material needing a new handle. Cheap (no Vulkan sync) if
            only `factors` changed; if any texture handle differs from
            what this material currently has, does one graphics-queue
            wait-idle before rewriting the descriptor set (see the .cpp) -
            fine for an occasional designer edit, not something to call
            every frame.
         *********************************************************************/
        ENGINE_API void UpdateMaterial(MaterialHandle handle, const MaterialData& data);

        /*********************************************************************
         \brief  Reads back a material's current texture bindings/scalar
                 factors.
         \param[in] handle The material to look up.
         \return Const reference to the material's data.
         *********************************************************************/
        ENGINE_API const MaterialData& GetMaterialData(MaterialHandle handle) const;

        /*********************************************************************
         \brief  The default material handle.
         \return Handle to the neutral white-albedo/flat-normal/no-effect
                 material.
         \note  Substituted by RenderExtractSystem for any entity whose
                MeshRenderer::material is Invalid so every draw always has
                a material descriptor set bound (the shader never has to
                branch on "no material").
         *********************************************************************/
        MaterialHandle GetDefaultMaterial() const { return m_resources.DefaultMaterial(); }

        // ---- Lights & environment ------------------------------------------------------------

        /*********************************************************************
         \brief
            Queues one light for this frame.

         \param[in] light The light, in world space (see LightData in
                           RenderTypes.h for the field conventions).

         \details
            Same per-frame contract as Submit(): call once per light per
            frame (RenderExtractSystem does this for every enabled ECS
            Light component); the queue is consumed and cleared at the end
            of Update(). At most kMaxLights (RenderTypes.h) are used per
            frame - extras are dropped with a one-time warning.

            If NOTHING is submitted in a frame, the Renderer lights the
            scene with one built-in directional "sun" (the direction and
            strength of the old hardcoded shader light) so a scene with no
            Light entities is never black.
         *********************************************************************/
        ENGINE_API void SubmitLight(const LightData& light);

        /*********************************************************************
         \brief
            Sets this frame's sky, ambient and exposure.

         \param[in] settings The environment to render this frame with.

         \details
            Per-frame like SubmitLight(): reset to GetDefaultEnvironment()
            at the end of every Update(). RenderExtractSystem calls this
            from the scene's Skybox component; with no such entity the
            defaults apply (built-in procedural sky, default ambient,
            exposure 1). An Invalid settings.skybox also means "the
            built-in procedural sky".
         *********************************************************************/
        ENGINE_API void SetEnvironment(const EnvironmentSettings& settings);

        /*********************************************************************
         \brief  The environment used whenever SetEnvironment() wasn't
                 called this frame.
         \return The default EnvironmentSettings.
         *********************************************************************/
        const EnvironmentSettings& GetDefaultEnvironment() const { return m_defaultEnvironment; }

        /*********************************************************************
         \brief
            Creates a cubemap from six square face images of equal size.

         \param[in] facePaths Image file paths in Vulkan/OpenGL cube face
                                order: +X, -X, +Y, -Y, +Z, -Z (right, left,
                                top, bottom, front, back).
         \param[in] isSRGB    true for ordinary colour images (the usual
                                case for LDR skybox sets).

         \return A handle for Skybox::cubemap / EnvironmentSettings::skybox.

         \details
            Cached by the joined path list, like CreateTexture(). Throws if
            a face fails to load, isn't square, or doesn't match face 0's
            size.

         \note
            Face sets exported for other engines sometimes use a different
            handedness; if the sky looks mirrored or a face is rotated,
            swap +X/-X (or +Z/-Z) in facePaths.
         *********************************************************************/
        ENGINE_API CubemapHandle CreateCubemap(const std::array<std::string, 6>& facePaths, bool isSRGB = true);

        /*********************************************************************
         \brief
            Creates a cubemap from one equirectangular (latitude/longitude,
            2:1) panorama - the format HDRI sites such as Poly Haven ship.

         \param[in] path     Image path. .hdr files are loaded as true HDR
                               (values above 1.0 kept); LDR formats
                               (.png/.jpg/...) are converted to linear.
         \param[in] faceSize Resolution of each generated cube face.

         \return A handle for Skybox::cubemap / EnvironmentSettings::skybox.

         \details
            Resampled on the CPU into six faces (bilinear), stored as
            RGBA16F so HDR range survives. Cached by path + faceSize.
            Throws if the file can't be loaded.
         *********************************************************************/
        ENGINE_API CubemapHandle CreateCubemapFromEquirect(const std::string& path, uint32_t faceSize = 512);

        /*********************************************************************
         \brief  The built-in procedural sky (a blue gradient with a soft
                 sun glow matching the fallback sun light), generated at
                 startup. Used whenever no valid skybox is set.
         \return Handle to the built-in sky cubemap.
         *********************************************************************/
        CubemapHandle GetDefaultSkybox() const { return m_environmentMaps.DefaultSkybox(); }

        /*********************************************************************
         \brief  The diffuse light a surface facing `normal` gets from a sky:
                 what "Ambient From Sky" lights it with, before the Skybox's
                 Intensity and Ambient Intensity. The editor's "Pick from
                 Sky" turns a sky into two ambient colours with it.
         \param[in] sky     The sky; Invalid (or unknown) = the built-in sky.
         \param[in] normal  The surface's direction (normalized here).
         \return Linear RGB, never negative.
         *********************************************************************/
        ENGINE_API glm::vec3 GetSkyIrradiance(CubemapHandle sky, const glm::vec3& normal) const;

        /*********************************************************************
         \brief  The built-in primitive meshes (Cube, Capsule, Cone,
                 Cylinder, Disc, Plane, Pyramid, Sphere), created at
                 startup. Names via GetMeshName(). ResourceManager
                 registers each under a fixed built-in asset id.
         \return The handles, in creation order.
         *********************************************************************/
        const std::vector<MeshHandle>& GetPrimitiveMeshes() const { return m_primitiveMeshes; }

        /*********************************************************************
         \brief
            Creates an offscreen render target (color+depth image, own
            camera, own per-frame-in-flight uniform buffer/descriptor
            set).

         \param[in] width  Viewport width, in pixels.
         \param[in] height Viewport height, in pixels.
         \param[in] role   What it is for: Scene (own camera, no game UI),
                            Game (main camera + the game's UI) or UI (no
                            scene; only UI submitted for it). See
                            ViewportRole.
         \return A handle to the new viewport.

         \details
            The Engine/Game build never calls these, so the viewport list stays
            empty and RecordCommandBuffer takes the direct-to-window path
            with zero extra branching cost beyond a single empty-container
            check.

            Typical Editor-panel usage:
                if (!m_viewport.IsValid()) m_viewport = renderer.CreateViewport(w, h);
                if (panelResized) renderer.ResizeViewport(m_viewport, newW, newH);
                ImGui::Image((ImTextureID)myCachedDescriptorSet, size);   // see note on AddTexture below
                // on panel close / shutdown:
                renderer.DestroyViewport(m_viewport);
         *********************************************************************/
        ENGINE_API ViewportHandle CreateViewport(uint32_t width, uint32_t height, ViewportRole role = ViewportRole::Scene);
        /// The role a viewport was created with. Throws std::runtime_error for an unknown handle.
        ENGINE_API ViewportRole   GetViewportRole(ViewportHandle handle) const;

        /*********************************************************************
         \brief  Resizes an existing viewport's color/depth images.
         \param[in] handle The viewport to resize.
         \param[in] width  New width, in pixels.
         \param[in] height New height, in pixels.
         *********************************************************************/
        ENGINE_API void           ResizeViewport(ViewportHandle handle, uint32_t width, uint32_t height);

        /*********************************************************************
         \brief  Destroys a viewport's GPU resources.
         \param[in] handle The viewport to destroy.
         *********************************************************************/
        ENGINE_API void           DestroyViewport(ViewportHandle handle);

        /*********************************************************************
         \brief  Shows or hides DebugDraw lines in one viewport (off by
                 default: the Editor turns it on for the Scene view only).
         \param[in] handle  The viewport.
         \param[in] enabled true to draw debug lines into it.
         *********************************************************************/
        ENGINE_API void           SetViewportDebugDraw(ViewportHandle handle, bool enabled);

        /*********************************************************************
         \brief  Shows or hides the selection outline (SetOutlinedObject) in
                 one viewport. Off by default; the Editor turns it on for the
                 Scene view only.
         *********************************************************************/
        ENGINE_API void           SetViewportOutline(ViewportHandle handle, bool enabled);

        /*********************************************************************
         \brief  Pauses or resumes rendering into one viewport. An inactive
                 viewport costs nothing per frame and keeps its last image
                 (the Game view uses this while the scene has no camera).
         *********************************************************************/
        ENGINE_API void           SetViewportActive(ViewportHandle handle, bool active);

        /*********************************************************************
         \brief  The colour a viewport shows where no scene is drawn: always
                 for a UI viewport, and for a Game viewport while there is
                 no main camera. Default opaque black.
         \param[in] handle  The viewport.
         \param[in] srgb    sRGB colour, like UI colours. Alpha is kept in
                             the image, so a UI viewport cleared to alpha 0
                             can be drawn over a checkerboard by the editor.
         *********************************************************************/
        ENGINE_API void           SetViewportClearColour(ViewportHandle handle, const glm::vec4& srgb);

        /*********************************************************************
         \brief  How a viewport maps UI canvas pixels to its own pixels:
                 image = canvas * scale + offset (clip rectangles too).
                 Default scale 1, offset 0. For the UI editor's pan and zoom.
         \param[in] handle  The viewport.
         \param[in] scale   Pixels per canvas pixel, per axis (keep it above 0).
         \param[in] offset  Where canvas (0, 0) lands, in the viewport's pixels.
         *********************************************************************/
        ENGINE_API void           SetViewportUITransform(ViewportHandle handle, const glm::vec2& scale, const glm::vec2& offset);

        /*********************************************************************
         \brief
            Which object gets the orange selection outline, by the userData
            its render proxy was created with (RenderExtractSystem uses the
            packed Entity). 0 = none.
         \details
            Drawn only in viewports with SetViewportOutline(true): the
            object is rendered into a mask with no depth test (so it shows
            through walls), then the mask's edge is blended on top.
         *********************************************************************/
        ENGINE_API void           SetOutlinedObject(std::uint64_t userData);

        /*********************************************************************
         \brief  The same for several objects at once (the editor's
                 multi-selection). Replaces whatever was outlined before;
                 zeros are ignored, an empty list outlines nothing.
         *********************************************************************/
        ENGINE_API void           SetOutlinedObjects(const std::vector<std::uint64_t>& userData);

        /*********************************************************************
         \brief  Outline colour (sRGB, a = opacity) and thickness in pixels
                 (1..3).
         *********************************************************************/
        ENGINE_API void           SetOutlineStyle(const glm::vec4& color, float thicknessPixels);

        /*********************************************************************
         \brief  The sun's cascaded shadow settings (see ShadowSettings).
                 Changes apply next frame.
         *********************************************************************/
        ShadowSettings&           GetShadowSettings() { return m_shadowSettings; }

        /*********************************************************************
         \brief  Bloom settings (see PostSettings). Changes apply next frame.
         *********************************************************************/
        PostSettings&             GetPostSettings() { return m_postSettings; }
        /// Where the last frames' time went (CPU stages and GPU passes), smoothed.
        const RenderFrameStats&   GetFrameStats() const { return m_frameStats; }
        /// The built-in load test: set `count` above 0 to add that many extra objects.
        StressSettings&           GetStressSettings() { return m_stressSettings; }

        /*********************************************************************
         \brief
            Queues one batch of particles for this frame (normally one
            emitter - ParticleSystem does this). Like Submit(), forgotten
            after the frame.

         \param[in] desc      Texture, blend mode, flipbook layout and the
                              batch's world bounds.
         \param[in] particles The particles (copied).
         \param[in] count     How many.

         \details
            Per view, batches outside the frustum are skipped, the rest are
            drawn far to near, and alpha-blended batches have their
            particles sorted back to front. Particles are unlit; to light
            the scene, also SubmitLight() a point light for the emitter.
         *********************************************************************/
        ENGINE_API void SubmitParticles(const ParticleDrawDesc& desc, const GpuParticle* particles, uint32_t count);

        /*********************************************************************
         \brief
            Queues one water surface for this frame (normally one Water
            component - RenderExtractSystem does this). Like Submit(),
            forgotten after the frame.

         \param[in] mesh     The surface's shape. The built-in Plane (1 x 1 m,
                             facing +Y) scaled by `model` makes a flat pond or
                             lake; any mesh works (its top side is drawn).
         \param[in] model    Object -> world transform.
         \param[in] settings How the water looks (WaterSettings; colours in
                             sRGB).

         \details
            Drawn after the opaque meshes and the sky and before particles,
            seen from above only (back faces are culled). The water shows
            what is under it by reading a copy of the scene drawn so far, so
            a view with water in sight pays for two image copies and a scene
            pass in two halves. Not pickable in the editor, casts no shadow,
            and is not in the ambient occlusion pre-pass.
         *********************************************************************/
        ENGINE_API void SubmitWater(MeshHandle mesh, const glm::mat4& model, const WaterSettings& settings);

        /*********************************************************************
         \brief  Shows or hides DebugDraw lines when the scene is drawn
                 straight to the window (the Game build, where there are no
                 viewports). Off by default; turn on for gameplay debugging.
         \param[in] enabled true to draw debug lines into the window.
         *********************************************************************/
        void                      SetDebugDrawInWindow(bool enabled) { m_debugInWindow = enabled; }

        // ---- Screen-space UI (UITypes.h has the data and its conventions) ----------------------

        /*********************************************************************
         \brief
            Queues the game's UI for this frame: drawn over Game viewports
            and, in the standalone game, over the window. Never drawn in
            Scene or UI viewports.
         \param[in] list  Vertices in canvas pixels (origin top-left; one
                           canvas pixel = one screen pixel, so lay out
                           against GetGameScreenSize()), indices and draw
                           commands. Copied.
         \details
            Like Submit(), forgotten after the frame: submit every frame,
            before the Renderer's Update(). Several calls per frame are
            fine; each draws over the ones before. Drawn after tonemapping
            and debug lines, so exposure, bloom, fog and colour grading
            never affect it.
         *********************************************************************/
        ENGINE_API void SubmitUI(const UIDrawList& list);

        /*********************************************************************
         \brief  Queues UI for one viewport only (any role), mapped by its
                 SetViewportUITransform. For the UI editor tab, which lays
                 out at its own preview size. Ignored for an unknown handle.
         *********************************************************************/
        ENGINE_API void SubmitViewportUI(ViewportHandle viewport, const UIDrawList& list);

        /*********************************************************************
         \brief
            The size, in pixels, of the screen the game's UI is drawn on:
            the Game view's image in the editor (it changes with the panel
            and its aspect dropdown), the window in the standalone game.
         \return {0, 0} in the editor while no Game view exists (closed, or
                 not laid out yet): skip UI layout then.
         \note  One Game view is expected; with several, the first found.
         *********************************************************************/
        ENGINE_API glm::uvec2 GetGameScreenSize() const;

        /*********************************************************************
         \brief
            Where a world position appears on the game screen, through the
            main camera: for health bars, damage numbers and markers.
         \param[in]  worldPos   World-space point.
         \param[out] outPixels  Game-screen pixels (origin top-left, the same
                                 space as SubmitUI). May lie off-screen.
         \return false (outPixels untouched) when there is no main camera,
                 no game screen (GetGameScreenSize() is 0), or the point is
                 behind the camera.
         *********************************************************************/
        ENGINE_API bool ProjectToGameScreen(const glm::vec3& worldPos, glm::vec2& outPixels) const;

        /// Shows or hides the built-in UI test overlay (UITestOverlay.h), submitted as the game's UI.
        void SetUITestOverlay(bool enabled) { m_uiTestEnabled = enabled; }
        bool GetUITestOverlay() const       { return m_uiTestEnabled; }

        /*********************************************************************
         \brief  Accessor for a viewport's independent camera.
         \param[in] handle The viewport to look up.
         \return Mutable reference to that viewport's Camera.
         \note  A Game viewport's camera is overwritten with the main
                 camera every frame, so only Scene viewports keep changes.
         *********************************************************************/
        ENGINE_API Camera&        GetViewportCamera(ViewportHandle handle);

        /*********************************************************************
         \brief  Accessor for the shared viewport sampler.
         \return The sampler handle.
         \note  One sampler is shared by every viewport image (they're
                just being displayed, not filtered differently), so there
                is exactly one to hand to ImGui_ImplVulkan_AddTexture().
         *********************************************************************/
        ENGINE_API vk::Sampler    GetViewportSampler() const { return *m_viewTargets.LinearSampler(); }

        /*********************************************************************
         \brief  Accessor for a viewport's current color image view.
         \param[in] handle The viewport to look up.
         \return The color image view.
         *********************************************************************/
        ENGINE_API vk::ImageView  GetViewportColorView(ViewportHandle handle) const;

        /*********************************************************************
         \brief
            Queues one draw for this frame.

         \param[in] mesh     The mesh to draw.
         \param[in] material The material to draw it with.
         \param[in] model    The object's world-space model matrix.

         \details
            Call once per visible object per frame (the ECS extraction
            step). The queue is consumed and cleared at the end of
            Update(). Pass GetDefaultMaterial() (or let the caller's own
            fallback logic do so - see RenderExtractSystem) rather than an
            Invalid MaterialHandle; the scene pass always binds a
            material descriptor set per draw.
         *********************************************************************/
        ENGINE_API void Submit(MeshHandle mesh, MaterialHandle material, const glm::mat4& model,
                               const glm::vec4& params = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));

        // ---- Render proxies (persistent, culled objects) -------------------------------------

        /*********************************************************************
         \brief
            Registers a persistent renderable object (normally one per ECS
            entity with a MeshRenderer - RenderExtractSystem does this).

         \param[in] userData Returned by Raycast() hits (RenderExtractSystem
                             stores the packed Entity).
         \return A handle for SetRenderProxy/DestroyRenderProxy.

         \details
            Unlike Submit() (re-sent every frame, culled brute force), a
            proxy stays registered and lives in the Renderer's SpatialIndex,
            so each view's frustum culling and editor ray picking are tree/
            array queries instead of loops over everything. It starts hidden
            until SetRenderProxy() gives it a mesh.
         *********************************************************************/
        ENGINE_API RenderProxyHandle CreateRenderProxy(std::uint64_t userData);

        /*********************************************************************
         \brief
            Sets what a proxy draws and where. Cheap to call every frame:
            identical values are a no-op, and a move only touches the
            spatial index (see SpatialIndex for how moving objects are
            handled).

         \param[in] handle   The proxy.
         \param[in] mesh     Mesh to draw (Invalid = nothing).
         \param[in] material Material (Invalid = the default material).
         \param[in] model    World matrix.
         \param[in] visible  false hides it (removed from culling/picking).
         \param[in] params   Per-object material parameters (InstanceData::
                             params): x = emissive multiplier, yzw spare.
         *********************************************************************/
        ENGINE_API void SetRenderProxy(RenderProxyHandle handle, MeshHandle mesh, MaterialHandle material,
                                       const glm::mat4& model, bool visible,
                                       const glm::vec4& params = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));

        /*********************************************************************
         \brief  Unregisters a proxy; its handle may be reused.
         *********************************************************************/
        ENGINE_API void DestroyRenderProxy(RenderProxyHandle handle);

        /*********************************************************************
         \brief
            Exact ray pick against every visible proxy: candidates come from
            the spatial index (box tests), then the ray is tested against the
            mesh's actual triangles.

         \param[in]  origin      Ray start (world space).
         \param[in]  direction   Ray direction (any length).
         \param[in]  maxDistance Ignore hits farther than this.
         \param[out] outHit      Nearest hit (userData, distance, point).
         \return true if anything was hit.
         *********************************************************************/
        ENGINE_API bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RenderRaycastHit& outHit) const;

        /*********************************************************************
         \brief  Last frame's culling numbers for an editor viewport.
         *********************************************************************/
        ENGINE_API RenderCullStats GetViewportCullStats(ViewportHandle handle) const;

        /*********************************************************************
         \brief  Last frame's culling numbers for the window pass (Game build).
         *********************************************************************/
        const RenderCullStats& GetWindowCullStats() const { return m_windowCullStats; }

        /*********************************************************************
         \brief  The spatial index behind culling/picking (read-only).
         *********************************************************************/
        const SpatialIndex& GetSpatialIndex() const { return m_scene.Spatial(); }

        /*********************************************************************
         \brief  Registers a callback invoked while the frame's render
                 pass is still open (after the scene draw, before
                 endRendering).
         \param[in] cb The callback to invoke.
         \note  Lets Core forward to layers (e.g. the Editor's ImGui).
         *********************************************************************/
        void SetOverlayCallback(std::function<void()> cb) { m_overlayCallback = std::move(cb); }

        /*********************************************************************
         \brief  Builds the plain-data bundle ImGui's Vulkan backend needs
                 to initialize itself.
         \return A filled-in ImGuiVulkanContext.
         *********************************************************************/
        ENGINE_API ImGuiVulkanContext   GetImguiContext()       const;

        /*********************************************************************
         \brief  Accessor for the current frame's command buffer.
         \return The current command buffer.
         *********************************************************************/
        ENGINE_API vk::CommandBuffer    GetCurrentCommandBuff() const;

    private:
        WindowManager&              m_window;
        std::function<void()>       m_overlayCallback;

        VulkanContext               m_context;
        VulkanSwapchain             m_swapchain;
        RenderLayouts              m_layouts;
        // Render passes (Graphics/Passes/). Declared after m_layouts, which they hold a reference to.
        BloomPass                   m_bloomPass;
        TonemapPass                 m_tonemapPass;
        ToonOutlinePass             m_toonOutlinePass;   // toon outlines (not the selection outline)
        // These draw meshes, so they hold a reference to m_resources (declared below; only used once recording starts).
        AmbientOcclusionPass        m_aoPass{ m_resources };
        ShadowPass                  m_shadowPass{ m_resources };
        OutlinePass                 m_outlinePass{ m_resources };
        ParticlePass                m_particlePass{ m_context, m_resources };
        WaterPass                   m_waterPass{ m_resources };   // water surfaces, drawn inside the scene pass
        ScenePass                   m_scenePass{ m_resources, m_particlePass, m_waterPass };
        DebugLinePass               m_debugLinePass;
        UIPass                      m_uiPass{ m_context, m_resources };   // also holds m_resources (declared below; used once frames start)
        VolumetricFogPass           m_fogPass{ m_context };   // froxel fog (compute); its pool is BEFORE every view's FogVolumeTargets
        UITestOverlay               m_uiTestOverlay;
        bool                        m_uiTestEnabled = false;
        VulkanFrameManager          m_frameManager;

        // Pending mesh/texture/cubemap copies: recorded into each frame's command buffer ahead of
        // its passes (RecordCommandBuffer), their staging buffers freed once that frame slot's
        // fence has signalled (DrawFrame). Declared after m_context (its staging buffers are
        // suballocated from the context's allocator, so they must be destroyed first).
        VulkanUploadQueue           m_uploads;

        // Scene
        Camera                      m_camera;            // main (game) camera - see SetMainCamera
        bool                        m_hasMainCamera = false;
        WorkerPool                  m_workers;           // threads for the per-view culling and batching (BEFORE m_scene, which uses them)
        RenderResources             m_resources{ m_context, m_uploads };   // meshes, textures, materials
        RenderScene                 m_scene{ m_context, m_resources, m_workers };   // what there is to draw: proxies, culling, batches, instances
        ViewDrawList                m_windowDraws;      // the window pass's batches (no viewports)
        RenderCullStats             m_windowCullStats;
        std::vector<MeshHandle>     m_primitiveMeshes;
        void CreatePrimitiveMeshes();

        EnvironmentMaps                             m_environmentMaps{ m_context, m_uploads };   // skyboxes and their lighting data

        // Lights & environment (per frame, like m_renderQueue): filled by SubmitLight()/
        // SetEnvironment(), consumed by DrawFrame(), reset at the end of Update().
        EnvironmentSettings                         m_defaultEnvironment;
        EnvironmentSettings                         m_environment;

        std::vector<VulkanBuffer>                   m_uniformBuffers;
        std::vector<void*>                          m_uniformBuffersMapped;

        SceneLights                                 m_lights;   // this frame's lights, their buffer, and each view's cluster grid

        bool                                        m_debugInWindow = false;

        // Cascaded sun shadows (the map itself is ShadowPass's).
        ShadowSettings                              m_shadowSettings;

        // Selection outline (SetOutlinedObject / SetViewportOutline).
        OutlineCompositePush                        m_outlineStyle;

        // Post-processing (HDR scene image -> bloom -> tonemap). Pool BEFORE m_windowPost and
        // m_viewportSet, whose PostTargets hold sets allocated from it.
        PostSettings                                m_postSettings;
        // Timings: what the overlay shows, and the GPU half's measuring.
        RenderFrameStats                            m_frameStats;
        GpuProfiler                                 m_profiler;
        double                                      m_lastFrameStart = 0.0;  // seconds, steady clock

        // Load test (see StressSettings).
        StressSettings                              m_stressSettings;
        std::vector<RenderProxyHandle>              m_stressProxies;
        std::vector<MaterialHandle>                 m_stressMaterials;
        float                                       m_stressBuiltSpacing = 0.0f;
        bool                                        m_stressWasMoving = false;

        uint32_t                                    m_msaaSamples = 1;      // in use now (PostSettings::msaaSamples, clamped to the GPU)
        float                                       m_time = 0.0f;          // seconds since Init (FrameUniforms::time.x)
        float                                       m_deltaTime = 0.0f;     // last Update()'s dt (FrameUniforms::time.y)
        ViewTargetAllocator                         m_viewTargets{ m_context };   // samplers + the pool PostTargets sets come from (so: before every PostTargets)
        PostTargets                                 m_windowPost;           // the window's own targets (standalone build)
        FogVolumeTargets                            m_windowFog;            // the window's fog volumes (standalone build)

        vk::raii::DescriptorPool                    m_descriptorPool = nullptr;
        std::vector<vk::raii::DescriptorSet>        m_descriptorSets;

        // Viewports (see ViewportHandle). Empty in a standalone Engine/Game build. Declared after
        // m_viewTargets, whose pool their PostTargets sets come from.
        ViewportSet                                m_viewportSet{ m_context, m_viewTargets };


        // Vulkan helpers
        void RecordCommandBuffer(FrameContext& fc);
        void CreateUniformBuffers();
        void CreateDescriptorPool();
        void CreateDescriptorSets();

        // ---- Particles (ParticlePass holds and sorts them) ----
        // Uploads this frame's particles; re-points set 0, binding 5 if the buffer was replaced.
        void UploadParticles(uint32_t frameIndex);

        // ---- Post-processing ----
        // The scene pass (ScenePass: meshes, sky and particles into the view's HDR image, clearing
        // colour and depth), then the bloom chain. Leaves the HDR image and bloom ready to be sampled,
        // and depth holding the scene's depth for the debug lines that follow.
        void DrawSceneHdr(FrameContext& fc, PostTargets& targets, vk::Image depthImage, vk::ImageView depthView,
                          vk::Extent2D extent, vk::DescriptorSet frameSet, const ViewDrawList& draws);
        /// Points a view's set 0 at the current environment map (binding 8), at that view's
        /// ambient occlusion image (binding 9) and at its fog volume (binding 10; nullptr = the
        /// "no fog" volume). Called every frame for the frame's own set.
        void WriteViewTextures(vk::DescriptorSet frameSet, const PostTargets& targets, const FogVolumeTargets* fog);
        /// Whether views draw lit (froxel) fog this frame: the scene has fog, its Skybox asks for
        /// the volumetric kind, and PostSettings::volumetricFog allows it.
        bool VolumetricFogActive() const;
        /// Whether views get toon outlines this frame: the Skybox asks for them and the Rendering
        /// menu has toon shading on.
        bool ToonLinesActive() const;

        // ---- Frame preparation (before recording) ----
        // Culls and batches everything one view draws this frame (main pass, shadow cascades,
        // selection mask) into `out`, appending instances to m_instanceScratch, and writes the
        // view's FrameUniforms to `uniformDst`.
        void PrepareView(Camera& camera, vk::Extent2D extent, RenderCullStats& stats, ViewDrawList& out,
                         bool outline, void* uniformDst);
        // Uploads this frame's instances; re-points set 0, binding 2 if the buffer was replaced.
        void UploadInstances(uint32_t frameIndex);
        // Points set 0's shared bindings (1 lights, 2 instances, 3/4 shadow map) of each per-frame
        // set at this frame's resources.
        void WriteSharedFrameDescriptors(std::vector<vk::raii::DescriptorSet>& sets);

        // ---- Views ----
        // Whether a viewport draws the scene this frame: Scene always, Game while there is a main
        // camera, UI never. One that doesn't shows its clear colour (plus UI).
        bool ViewportDrawsScene(const Viewport& vp) const;

        // ---- Shadows ----
        // Has ShadowPass create the shadow map, then moves it to its resting (shader-read) layout.
        void CreateShadowResources();

        // ---- Debug lines (DebugDraw.h) ----


        // ---- Lights/environment/skybox helpers ----
        // Everything FrameUniforms holds besides view/proj/cameraPos: ambient, exposure, skybox
        // intensity, light count. Shared by the window and viewport paths so they can't drift.
        void FillEnvironmentUniforms(FrameUniforms& ubo) const;
        /// Builds, moves or removes the load-test objects to match m_stressSettings.
        void UpdateStressTest();
        uint32_t SupportedMsaaSamples(uint32_t wanted) const;   ///< Highest of 4 / 2 / 1 that is <= wanted and the GPU can render to.
        void ApplyMsaaSetting();                                ///< Rebuilds every view's targets when PostSettings::msaaSamples changed.

        vk::raii::CommandBuffer BeginSingleTimeCommand();
        void EndSingleTimeCommand(vk::raii::CommandBuffer&& cmdBuff);

        // Generalized from a swapchain-color-only helper so it can also transition the depth
        // image: takes the target image and aspect mask explicitly instead of assuming color.
        void TransitionImageLayout(FrameContext fc, vk::Image image, vk::ImageAspectFlags aspectMask,
                                   vk::ImageLayout old_layout, vk::ImageLayout new_layout,
                                   vk::AccessFlags2 src_access_mask, vk::AccessFlags2 dst_access_mask,
                                   vk::PipelineStageFlags2 src_stage_mask, vk::PipelineStageFlags2 dst_stage_mask,
                                   uint32_t layerCount = 1);

    };  // RENDERER CLASS

}   // NAMESPACE CRUCIBLE
