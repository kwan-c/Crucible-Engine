# ------------------------------------------------------------ #
# Third-Party Dependencies
# ------------------------------------------------------------ #

# GLFW (shared DLL)
# Must be shared: Engine.dll creates the window, while Editor.exe's ImGui GLFW backend also calls
# GLFW. A static GLFW would give each module its own copy of GLFW's global state, and the copy in
# Editor.exe would report "not initialized" (glfwGetWin32Window returns NULL).
set(GLFW_LIBRARY_TYPE SHARED CACHE STRING "" FORCE)
add_subdirectory(glfw EXCLUDE_FROM_ALL)
set_target_properties(glfw PROPERTIES FOLDER "ThirdParty")

# GLM (header-only)
add_library(glm INTERFACE)
target_include_directories(glm SYSTEM INTERFACE glm)

# Add more here...

# ----------------------------------- #
# IMGUI core (static, linked by Editor only)
# ----------------------------------- #

add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_demo.cpp
    imgui/misc/cpp/imgui_stdlib.cpp   # ImGui::InputText(std::string*): resizable text fields
)

target_include_directories(imgui PUBLIC imgui)

set_target_properties(imgui PROPERTIES FOLDER "ThirdParty")

# ----------------------------------- #
# IMGUI backends (static, linked by Editor only)
# ----------------------------------- #

add_library(imgui_backends STATIC
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui_backends PUBLIC imgui/backends)
target_link_libraries(imgui_backends PUBLIC imgui glfw Vulkan::Vulkan)

set_target_properties(imgui_backends PROPERTIES FOLDER "ThirdParty")

# ----------------------------------- #
# ImGuizmo (static, linked by Editor only)
# ----------------------------------- #
# Built here instead of via ImGuizmo's own CMakeLists, which would fetch its own ImGui for the
# example. Linking our imgui target makes ImGuizmo compile against the same imgui.h/imconfig.h
# as the Editor, so ImGui's context and data layout match.

add_library(imguizmo STATIC
    ImGuizmo/src/ImGuizmo.cpp
    ImGuizmo/src/GraphEditor.cpp
    ImGuizmo/src/ImCurveEdit.cpp
    ImGuizmo/src/ImGradient.cpp
    ImGuizmo/src/ImSequencer.cpp
    ImGuizmo/src/ImVectorEditor.cpp
)

target_include_directories(imguizmo PUBLIC ImGuizmo/src)
target_link_libraries(imguizmo PUBLIC imgui)

set_target_properties(imguizmo PROPERTIES FOLDER "ThirdParty")


# ----------------------------------- #
# VMA (Vulkan Memory Allocator)
# ----------------------------------- #

# vma (header-only)
add_library(vma INTERFACE)
target_include_directories(vma SYSTEM INTERFACE vma/include)

# ------------------------------------------------------------ #
# XRESOURCE (guid + mgr, header-only)
# ------------------------------------------------------------ #

add_library(xresource INTERFACE)
target_include_directories(xresource SYSTEM INTERFACE
    xresource_mgr/source
    xresource_mgr/source/dependencies/xresource_guid/source
)

# ----------------------------------- #
# FMOD
# ----------------------------------- #

set(FMOD_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/fmod"
)

add_library(fmod INTERFACE)

target_include_directories(fmod SYSTEM INTERFACE
    "${FMOD_ROOT}/include"
)

target_link_libraries(fmod INTERFACE
    "${FMOD_ROOT}/lib/x64/fmodstudio_vc.lib"
    "${FMOD_ROOT}/lib/x64/fmod_vc.lib"
)

# ----------------------------------- #
# JOLT
# ----------------------------------- #

set(JPH_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_TESTS OFF CACHE BOOL "" FORCE)

set(JPH_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/jolt/Build"
    "${CMAKE_CURRENT_BINARY_DIR}/Jolt"
)

# Match the engine's MSVC runtime:
# Debug   -> /MDd
# Release -> /MD
set_property(TARGET Jolt PROPERTY
    MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)

set_target_properties(Jolt PROPERTIES
    FOLDER "ThirdParty"
)

# Jolt exposes its headers with a plain target_include_directories(Jolt PUBLIC ...), because it
# arrives through add_subdirectory rather than a target we write ourselves. That makes MSVC treat
# them as our code and run /W4 and the code-analysis checks over them. Re-tag them as SYSTEM, the
# same as glm, vma, xmath, xresource, stb and rapidjson above, so they come through as /external:I.
get_target_property(JOLT_INCLUDE_DIRS Jolt INTERFACE_INCLUDE_DIRECTORIES)
if(JOLT_INCLUDE_DIRS)
    set_target_properties(Jolt PROPERTIES
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${JOLT_INCLUDE_DIRS}"
    )
endif()

# ----------------------------------- #
# RTTR (shared DLL)
# ----------------------------------- #
# Must be shared: RTTR keeps a global type registry. If it were static, Engine.dll and
# Editor.exe would each get a private registry, and the Editor could not see any type that
# Engine.dll registered (so the Inspector would show no properties for Engine components).

set(BUILD_STATIC OFF CACHE BOOL "" FORCE)
set(BUILD_RTTR_DYNAMIC ON CACHE BOOL "" FORCE)
set(BUILD_WITH_STATIC_RUNTIME_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_WITH_RTTI ON CACHE BOOL "" FORCE)              
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(BUILD_INSTALLER OFF CACHE BOOL "" FORCE)
set(BUILD_PACKAGE OFF CACHE BOOL "" FORCE)
set(USE_PCH OFF CACHE BOOL "" FORCE)                     # simpler to reason about while debugging; turn back on later if you want faster rebuilds

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/rttr"
    "${CMAKE_CURRENT_BINARY_DIR}/RTTR"
)

set_target_properties(rttr_core PROPERTIES FOLDER "ThirdParty")

# RTTR still uses std::aligned_storage, which is deprecated in C++23 (warning STL4034).
# PUBLIC so every target that includes RTTR headers (Engine, Editor, Game) gets the define.
target_compile_definitions(rttr_core PUBLIC _SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING)

# ----------------------------------- #
# STB (header-only)
# ----------------------------------- #

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE stb)

# ----------------------------------- #
# RAPID JSON (header-only)
# ----------------------------------- #

add_library(rapidjson INTERFACE)
target_include_directories(rapidjson SYSTEM INTERFACE rapidjson/include)

# ----------------------------------- #


# ----------------------------------- #
# FREETYPE (static)
# ----------------------------------- #
# Loads TTF/OTF outlines, kerning and metrics. Used by msdfgen-ext to read fonts.
# Every optional codec is disabled, otherwise FreeType's CMake picks up whatever
# zlib/libpng/harfbuzz/brotli it happens to find on the machine and the build
# differs between team members' PCs.

set(FT_DISABLE_ZLIB     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2    ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG      ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI   ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL    ON CACHE BOOL "" FORCE)

# FreeType reads BUILD_SHARED_LIBS. Force static for this subdirectory only.
set(_CRU_SAVED_SHARED ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/freetype"
    "${CMAKE_CURRENT_BINARY_DIR}/freetype"
    EXCLUDE_FROM_ALL
)

set(BUILD_SHARED_LIBS ${_CRU_SAVED_SHARED})

set_property(TARGET freetype PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
set_target_properties(freetype PROPERTIES FOLDER "ThirdParty/Fonts")

# FreeType only exports the name Freetype::Freetype at install time. msdfgen looks
# for that exact target, and falls back to find_package (system FreeType) if it is
# missing, so give it an alias to our in-tree build.
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

# Same treatment as Jolt: treat FreeType headers as external (/external:I).
get_target_property(FREETYPE_INCLUDE_DIRS freetype INTERFACE_INCLUDE_DIRECTORIES)
if(FREETYPE_INCLUDE_DIRS)
    set_target_properties(freetype PROPERTIES
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${FREETYPE_INCLUDE_DIRS}"
    )
endif()

# ----------------------------------- #
# MSDFGEN (static: msdfgen-core + msdfgen-ext)
# ----------------------------------- #
# Generates the (multi-channel) signed distance field for each glyph outline.
# - No vcpkg / Skia: everything comes from Extern.
# - No SVG (tinyxml2) / PNG (libpng): we don't load SVG shapes, and image saving
#   goes through stb_image_write instead.
# - Dynamic CRT (/MD, /MDd) to match the rest of the engine. msdfgen defaults to /MT.

set(MSDFGEN_CORE_ONLY        OFF CACHE BOOL "" FORCE)
set(MSDFGEN_BUILD_STANDALONE OFF CACHE BOOL "" FORCE)
set(MSDFGEN_USE_VCPKG        OFF CACHE BOOL "" FORCE)
set(MSDFGEN_USE_SKIA         OFF CACHE BOOL "" FORCE)
set(MSDFGEN_USE_OPENMP       OFF CACHE BOOL "" FORCE)
set(MSDFGEN_USE_CPP11        ON  CACHE BOOL "" FORCE)
set(MSDFGEN_DISABLE_SVG      ON  CACHE BOOL "" FORCE)
set(MSDFGEN_DISABLE_PNG      ON  CACHE BOOL "" FORCE)
set(MSDFGEN_INSTALL          OFF CACHE BOOL "" FORCE)
set(MSDFGEN_DYNAMIC_RUNTIME  ON  CACHE BOOL "" FORCE)

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/msdfgen"
    "${CMAKE_CURRENT_BINARY_DIR}/msdfgen"
    EXCLUDE_FROM_ALL
)

set_target_properties(msdfgen-core msdfgen-ext PROPERTIES FOLDER "ThirdParty/Fonts")

# ----------------------------------- #
# MSDF-ATLAS-GEN (static)
# ----------------------------------- #
# Packs the glyphs into one atlas texture and runs msdfgen on each of them.
# MSDF_ATLAS_MSDFGEN_EXTERNAL: use the Extern/msdfgen target above instead of the
# msdfgen git submodule inside msdf-atlas-gen.
# MSDF_ATLAS_NO_ARTERY_FONT: we write our own font file format, so the
# artery-font-format submodule isn't needed.

set(MSDF_ATLAS_BUILD_STANDALONE OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_USE_VCPKG        OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_USE_SKIA         OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_NO_ARTERY_FONT   ON  CACHE BOOL "" FORCE)
set(MSDF_ATLAS_MSDFGEN_EXTERNAL ON  CACHE BOOL "" FORCE)
set(MSDF_ATLAS_INSTALL          OFF CACHE BOOL "" FORCE)
set(MSDF_ATLAS_DYNAMIC_RUNTIME  ON  CACHE BOOL "" FORCE)

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/msdf-atlas-gen"
    "${CMAKE_CURRENT_BINARY_DIR}/msdf-atlas-gen"
    EXCLUDE_FROM_ALL
)

set_target_properties(msdf-atlas-gen PROPERTIES FOLDER "ThirdParty/Fonts")

# Engine compiles with /W4 + code analysis; mark the msdfgen / msdf-atlas-gen headers
# as external so their warnings don't show up as ours (same as FreeType and Jolt).
foreach(_font_target msdfgen-core msdfgen-ext msdf-atlas-gen)
    get_target_property(_font_includes ${_font_target} INTERFACE_INCLUDE_DIRECTORIES)
    if(_font_includes)
        set_target_properties(${_font_target} PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_font_includes}"
        )
    endif()
endforeach()

# One target for the engine to link: msdf-atlas-gen pulls in msdfgen-core,
# msdfgen-ext and FreeType through its own PUBLIC links.
add_library(CrucibleFonts INTERFACE)
target_link_libraries(CrucibleFonts INTERFACE
    msdf-atlas-gen::msdf-atlas-gen
    msdfgen::msdfgen
    Freetype::Freetype
)


# ----------------------------------- #

# Create an interface library that bundles all common dependencies
add_library(EngineThirdParty INTERFACE)

target_link_libraries(EngineThirdParty INTERFACE
    glfw
    glm

    # Add more here...
    xresource
    fmod
    Jolt
    vma
    Vulkan::Vulkan
    rttr_core
    stb
    rapidjson
)

# ----------------------------------- #
# Editor-only dependencies (ImGui, ImGuizmo).
# Link this from Editor ONLY - never from Engine or Game.
# ----------------------------------- #

add_library(EditorThirdParty INTERFACE)

target_link_libraries(EditorThirdParty INTERFACE
    imgui
    imgui_backends
    imguizmo
    CrucibleFonts   # FreeType + msdfgen + msdf-atlas-gen: the font compiler (Editor/src/Fonts). The engine only reads .cfont files.
)
