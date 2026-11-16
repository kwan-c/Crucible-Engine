# Minimum version of CMake for this project.
cmake_minimum_required(VERSION 3.23.1)
project(Crucible LANGUAGES CXX C)

# Force consistent CRT selection across every target (including third-party ones)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# Find system-installed Vulkan SDK (LunarG)
find_package(Vulkan REQUIRED)

# Global C++ settings.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    # Hide the console window in visual studio projects
    #set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
endif()

# Enable folder organization in Visual Studio
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------
# Third-Party Dependencies (shared across projects)
# ------------------------------------------------------------
add_subdirectory(Extern)
add_subdirectory(Tools/ModelCompiler)
add_subdirectory(Tools/TextureCompiler)

# ------------------------------------------------------------
# Engine Projects
# ------------------------------------------------------------
add_subdirectory(Engine)
add_subdirectory(ManagedScripts)
add_subdirectory(Editor)
add_subdirectory(Game)
add_subdirectory(Tools/Cook)   # after Engine: the Cook target waits for its Shaders target
add_subdirectory(Tools/ShaderGraphTests)   # tests for Editor/src/ShaderGraph; needs only rapidjson

# ------------------------------------------------------------
# Packaging (CPack): the PACKAGE target zips the game folder
# ------------------------------------------------------------
# Packs only the "Game" install component (see Game/CMakeLists.txt) into
# Build/Mekanomachia-<version>-win64.zip, holding one Mekanomachia-<version>-win64 folder.
# Players unzip it and run Mekanomachia.exe. Build the Cook target first:
#
#   cmake --build Build --config Release --target Game Cook
#   cmake --build Build --config Release --target PACKAGE
#
# FreeType, glm and rttr call include(CPack) in their own folders too. This one runs
# last, so the Build/CPackConfig.cmake it writes is the one PACKAGE uses.
set(CPACK_PACKAGE_NAME        "Mekanomachia")
set(CPACK_PACKAGE_VENDOR      "Team Medium-Well")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Mekanomachia, made with the Crucible Engine")
set(CPACK_PACKAGE_VERSION     "0.1.0")   # default; File > Package Game (or cpack -R) can give another
# Names the zip <name>-<version>-win64 when cpack runs, so a version given then is used too.
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_SOURCE_DIR}/Tools/Package/CPackOptions.cmake")
set(CPACK_GENERATOR           "ZIP")
# Only the Game component: leaves out third-party headers and .lib files (Jolt, glfw, ...).
# CPack only honours this list in component mode, so turn that on and put every listed
# component into one zip.
set(CPACK_COMPONENTS_ALL      Game)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
set(CPACK_COMPONENT_INCLUDE_TOPLEVEL_DIRECTORY ON)   # component mode leaves it out by default
include(CPack)

# Set startup project for Visual Studio
if(MSVC)
    set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Editor)
endif()
