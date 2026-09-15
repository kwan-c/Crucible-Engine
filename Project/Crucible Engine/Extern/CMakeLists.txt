# ------------------------------------------------------------ #
# Third-Party Dependencies
# ------------------------------------------------------------ #

# GLFW
add_subdirectory(glfw EXCLUDE_FROM_ALL)
set_target_properties(glfw PROPERTIES FOLDER "ThirdParty")

# Glad
add_library(glad glad/src/glad.c)
target_include_directories(glad PUBLIC glad/include)
set_target_properties(glad PROPERTIES FOLDER "ThirdParty")

# Add more here...

# ----------------------------------- #
# IMGUI
# ----------------------------------- #

set(IMGUI_SRC
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp

    # GLFW backend
    imgui/backends/imgui_impl_glfw.cpp

    # OpenGL3 backend
    imgui/backends/imgui_impl_opengl3.cpp
)

add_library(imgui STATIC ${IMGUI_SRC})

target_include_directories(imgui PUBLIC
    imgui
    imgui/backends
)

target_link_libraries(imgui PUBLIC glfw)

set_target_properties(imgui PROPERTIES FOLDER "ThirdParty")

# ----------------------------------- #
# XMATH (header-only)
# ----------------------------------- #

add_library(xmath INTERFACE)
target_include_directories(xmath SYSTEM INTERFACE xmath/source)

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

# ----------------------------------- #
# RTTR
# ----------------------------------- #

set(BUILD_STATIC ON CACHE BOOL "" FORCE)                 
set(BUILD_RTTR_DYNAMIC OFF CACHE BOOL "" FORCE)          
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

set_target_properties(rttr_core_lib PROPERTIES FOLDER "ThirdParty")

# ----------------------------------- #
# STB (header-only)
# ----------------------------------- #

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE stb)


# ----------------------------------- #


# Create an interface library that bundles all common dependencies
add_library(EngineThirdParty INTERFACE)

target_link_libraries(EngineThirdParty INTERFACE
    glfw
    glad

    # Add more here...
    imgui
    xmath
    xresource
    fmod
    Jolt
    rttr_core_lib
    stb
)