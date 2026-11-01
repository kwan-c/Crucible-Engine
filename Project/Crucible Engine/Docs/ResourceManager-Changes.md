# Resource pipeline changes (Nelson, 30–31 Oct 2026)

What changed in the asset/resource pipeline, what you need to do after pulling, and which of
**your** files I touched. Short version first.

## After you pull: do this once

1. **Run `run.bat`** (re-runs CMake). There are new tool projects, and shaders now compile into a different folder.
2. **Build the solution.** The first build compiles every model and texture (about 20 s), then only what changed.
3. **Close the editor before building Debug.** A running editor locks `Engine.dll`, and the link fails with `LNK1201` / `LNK1168`.
4. To try the **installed game**, build the **Cook** target first (see below).

## The big picture

| Before | Now |
|---|---|
| Originals and compiled files mixed in `Assets/`, models in `AssetsSource/` | **`Assets/`** = originals you edit. **`Resources/`** = compiled files the game reads. `AssetsSource/` is gone. |
| The game decoded PNG/JPG with stb at load | The game reads only **compiled** files. stb is no longer in `Engine.dll` or the game (course rule: stb is tools/editor only). |
| Textures were RGBA8 on the GPU, mipmaps made at load | Textures are **GPU-compressed** (BC7 etc.) with mipmaps made offline: about **4× less GPU memory**, faster loading |
| Skyboxes were baked on the CPU at every load (~13 MB GPU each) | Skyboxes are **baked offline** to `.sky` files: an upload only, about **1.6 MB** GPU each |
| Installer would need to pick files by hand | **Cook** fills `Resources/` with everything the game needs; the installer packages `Resources/` + the exe folder |

### Folders

- **`Assets/`**: `.glb`, `.png`/`.jpg`, `.hdr`, `.ttf`, `.slang`, scenes, prefabs, `.mat`, audio, animator controllers, config. **Edit here.**
- **`Resources/`**: compiled files, **committed to git**: `.cmodel`, `.cfont`, `.spv`, `.bill`, `.sky` (+ their `.meta`, same GUID as the source).
  Cook also copies scenes, prefabs, `.mat`, audio, controllers and config here, but those **copies are git-ignored**.
- The game finds its folders by itself: on your PC it reads `Assets/` plus the compiled files in `Resources/`; installed (no `Assets/`), it reads `Resources/` only.

### New compiled formats

| File | What it is | Made by |
|---|---|---|
| `.bill` | A texture, GPU-compressed (BC7 / BC1 / BC3 / BC4 / BC5) with all mipmaps, packed with **Billet** | Texture compiler (every build) or the Texture Importer |
| `.sky` | A skybox: 6 cube faces + its lighting, baked offline, BC6H (HDR) | Same, for images named `*_skybox` or `.hdr` |
| `.cmodel` v2 | Models now store their textures GPU-compressed (normal maps as BC5) | Model compiler (every build) |
| `.cfont` | Font atlas now packed with Billet instead of PNG (lossless, looks identical) | Font Importer |

**Billet** is our own lossless packer (`Engine/src/Compression/Billet.h`, LZ77 + Huffman, like zip). It makes files smaller on disk and is unpacked at load. No external library.

## Rules for everyone

- **Skybox images need `_skybox` in the file name** (e.g. `aurora_green_skybox.png`). Cook reports any scene sky that doesn't.
- **Add an image to `Assets/` and build**, or open it in the **Texture Importer** and press Compile. Either makes its `.bill`.
- **The renderer no longer loads image files.** `Renderer::CreateTexture(path)`, `CreateTextureFromMemory` and `CreateCubemap(6 paths)` are removed (nothing used them). Get textures, skies, models and fonts from the **ResourceManager by GUID**.
- **Never `#include <stb_image.h>` in Engine code.** The engine keeps stb's folder only for `stb_easy_font` (UI test overlay).
- **`.bill` means "compiled texture".** Anything else packed with Billet later needs its own extension.
- **Fonts:** compile them in the Font Importer. Old PNG fonts open in the editor but not in the game (Cook reports them).

## New in the editor

- **View → Texture Importer** (or double-click an image in the Asset Browser): pick usage (Color / Linear / Mask / Normal), format (BC1/BC3/BC4/BC5/BC7), max size, mipmaps and quality, with **live previews** of the result and of every BC format side by side (GPU memory + an error score). **Compile** writes the `.bill` and swaps it in straight away. Settings are saved next to the image as `<image>.texsettings` (hidden in the Asset Browser).
- **File → Cook Resources**: fills `Resources/` for the game (same as the `Cook` build target).
- **Hot reload**: when the editor window gets the focus back (or on F5 / Refresh in the Asset Browser), changed textures, `.mat` materials, fonts, prefabs, animator controllers and skies are loaded again, and failed loads are retried. Changed **models** need the scene opened again (the Console says so).
- ImGui's texture pool went from 8 to 64 slots (we had run out: `ImGui Vulkan Error: -1000069000`).

## Building the installer

```
cmake --build Build --config Release
cmake --build Build --config Release --target Cook
```

Package **`Build/Game/Release/*`** plus the whole **`Resources/`** folder next to the exe. Cook fails the build if anything the game needs is missing (an uncompiled model/texture/font, a sky without `_skybox`, a PNG font).

## Which of your files changed

### Kwan (renderer)

| File | Change |
|---|---|
| `Graphics/RenderSystem.h/.cpp` | **Removed** `CreateTexture(path)`, `CreateTextureFromMemory`, `CreateCubemap(6 paths)`. **Added** `CreateTextureCompressed` (uploads a `.bill`) and `CreateCubemapCompiled` (uploads a `.sky`). `CreateCubemapFromEquirect` now takes decoded pixels, not a path. No more `STB_IMAGE_IMPLEMENTATION`. |
| `Graphics/RenderResources.h/.cpp` | Same removals; new `CreateTextureCompressed` (BC1/3/4/5/6H/7 + RGBA8, every mip as stored). |
| `Graphics/EnvironmentMaps.h/.cpp` | The baking maths moved to the new `Graphics/EnvironmentBake.h/.cpp` (shared with the texture compiler, unchanged). New `CreateCubemapCompiled`; the 6-file loader removed; descriptor-set code shared via `RegisterCubemap`. No stb. |
| `Graphics/Vulkan/VulkanContext.cpp` | Turns on `textureCompressionBC` when the GPU has it. |
| `Graphics/Vulkan/VulkanShader.h` | Shaders load from `Resources/Shaders`. |
| `Engine/CompileShaders.cmake` | `.spv` files are written to `Resources/Shaders` (the `.slang` stay in `Assets/Shaders`). |
| `Assets/Shaders/DefaultShader.slang` | The normal map's Z is rebuilt from X and Y (needed for BC5; same result for every other normal map). |
| `Resources/ResourceManager.h/.cpp` (RM is Nelson's) | Loads `.bill`/`.sky`, compressed model textures, hot reload (`ReloadChanged`, `ReloadTexture`), decoder fallbacks for the editor. |
| `TextureHandle.h`, `CubemapHandle.h`, `GLTFLoader.h` | Comments only. |

### Xavier (editor, fonts, asset paths)

| File | Change |
|---|---|
| `AssetPipe/AssetPaths.h/.cpp` | New `ResourceRoot()` / `ResourcePath()` / `IsCookedRun()`; with no `Assets/` folder, the game runs from `Resources/`. |
| `Text/FontData.h/.cpp` | New atlas encoding **Billet** (the default); PNG atlases no longer written; old PNG fonts read only in the editor. No stb. |
| `Fonts/FontCompileJob.cpp` | `.cfont` is written to `Resources/Fonts`; a PNG-atlas font shows "Out of date". |
| `ImGuiPanel_AssetBrowser.h/.cpp` | Double-click an image opens the Texture Importer; `.texsettings` hidden; search skips `Resources`; refresh hot-reloads and also runs when the editor gets the focus back. |
| `ImGuiPanel_Menu.cpp` | **File → Cook Resources**, **View → Texture Importer**. |
| `ImGui/ImGuiManager.h/.cpp` | Registers the Texture Importer; ImGui pool 8 → 64. |
| `Editor/src/main.cpp` | Installs the editor's image decoder at startup. |

### chuuniby (model compiler)

| File | Change |
|---|---|
| `Tools/ModelCompiler/main.cpp`, `CMakeLists.txt` | Compiles `Assets` → `Resources`; images inside models are GPU-compressed (normal maps BC5); its duplicate-GUID check now looks at compiled files only. Links stb + Compressonator (tools only). |
| `Resources/CompiledModel.h/.cpp`, `ModelResource.h` | `.cmodel` version 2: accepts compressed images. |

### Abhishek

| File | Change |
|---|---|
| `ECS/Components/Components.h` | One comment (the cubemap function's name). |

### Shared build files

| File | Change |
|---|---|
| root `CMakeLists.txt` | New `Tools/TextureCompiler` and `Tools/Cook`. |
| `Engine/CMakeLists.txt` | The engine waits for `CompiledTextures` as well as `CompiledModels`. |
| `Editor/CMakeLists.txt` | Builds the Cook code and the shared texture code into the editor. |
| `Extern/CMakeLists.txt` | Compressonator (CMP_Core, tools/editor only); stb moved to the editor's libraries. |
| repo `.gitignore` | Cook's copies in `Resources/` ignored; compiled files kept. |

### Nelson (mine)

- **New:** `Compression/Billet`, `Resources/CompiledTexture`, `Resources/CompiledSky`, `Resources/ImageDecoders`, `Graphics/EnvironmentBake`, `Tools/Common/TextureBuild` + `TextureImport`, `Tools/TextureCompiler`, `Tools/Cook`, `Editor/src/Assets/EditorImageDecoders`, `ImGuiPanel_TextureImporter`, `Extern/compressonator/`.
- **Changed:** `AssetPipe/Assetmanager.h/.cpp` (indexes `Resources/`, `WriteCompiled`, change tracking for hot reload), `AssetRegistry.h/.cpp` (a compiled file owns its GUID).

### Assets

- `.glb` models moved from `AssetsSource/Models` to `Assets/Models`; compiled `.cmodel`/`.cfont`/`.spv` moved to `Resources/` (`.meta` files moved with them: **all GUIDs kept**).
- `Textures/aurora_green.png` renamed `aurora_green_skybox.png` (GUID kept).
- New `Textures/Orange_Milk_skybox.jpg` (a copy, own GUID): the sky in `Scenes/SkyboxFog.scene`, which now points at it. `Orange_Milk.jpg` is still the material texture.
- `Models/modeltest.obj` (an empty file) deleted.

## Open questions

- **For the prof:** does the "stb = tools/editor only" rule also cover `stb_easy_font` (used by the renderer's UI test overlay in `Engine.dll`)? If so, that overlay should move into the editor.
- **Editor bug, not fixed:** `Editor --scene <path>` is ignored. `Editor/src/main.cpp` builds `Core` from a fresh config instead of the `config` it just edited.
- **Not seen on screen yet:** a BC5 normal map. None of our current models have a normal map.
