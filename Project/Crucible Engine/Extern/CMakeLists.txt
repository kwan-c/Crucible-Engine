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
)
