# Minimum version of CMake for this project.
cmake_minimum_required(VERSION 3.23.1)
project(Crucible LANGUAGES CXX C)

# ------------------------------------------------------------
# The game's name and version: change them HERE, and only here
# ------------------------------------------------------------
# Everything else reads these: the .exe's name (Game/CMakeLists.txt), the zip's name (CPack
# below), the installer (the generated Tools/Package/Installer/GameInfo.iss), and the C++ code
# through the generated GameInfo.h (Engine/src/GameInfo.h.in): the window title, the Local
# AppData folder, error boxes, the Editor and the name Vulkan reports to the GPU driver.
# Re-run run.bat after changing them.
set(CRUCIBLE_GAME_NAME    "Lithomachia")
set(CRUCIBLE_GAME_VERSION "0.1.0")   # major.minor.patch

# The installer's AppId: how Windows knows a new version is the same game, so it upgrades it in
# place instead of installing a second copy. NEVER change it - not for a new version, and not if
# the game is renamed. Only a brand-new game gets a new one (Visual Studio: Tools > Create GUID,
# or PowerShell: [guid]::NewGuid().ToString().ToUpper()). Written here without the { }.
set(CRUCIBLE_GAME_APP_ID  "CB7DEFE7-76D9-419C-AE99-8187D3FBF97A")

string(LENGTH "${CRUCIBLE_GAME_APP_ID}" CRUCIBLE_GAME_APP_ID_LENGTH)
if(NOT CRUCIBLE_GAME_APP_ID_LENGTH EQUAL 36
   OR NOT CRUCIBLE_GAME_APP_ID MATCHES "^[0-9A-F]+-[0-9A-F]+-[0-9A-F]+-[0-9A-F]+-[0-9A-F]+$")
    message(FATAL_ERROR "CRUCIBLE_GAME_APP_ID must be a GUID in capitals without braces, "
                        "like CB7DEFE7-76D9-419C-AE99-8187D3FBF97A (it is '${CRUCIBLE_GAME_APP_ID}')")
endif()

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
# Build/<game name>-<version>-win64.zip, holding one <game name>-<version>-win64 folder.
# Players unzip it and run <game name>.exe. Build the Cook target first:
#
#   cmake --build Build --config Release --target Game Cook
#   cmake --build Build --config Release --target PACKAGE
#
# FreeType, glm and rttr call include(CPack) in their own folders too. This one runs
# last, so the Build/CPackConfig.cmake it writes is the one PACKAGE uses.
set(CPACK_PACKAGE_NAME        "${CRUCIBLE_GAME_NAME}")
set(CPACK_PACKAGE_VENDOR      "Team Medium-Well")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${CRUCIBLE_GAME_NAME}, made with the Crucible Engine")
set(CPACK_PACKAGE_VERSION     "${CRUCIBLE_GAME_VERSION}")   # default; File > Package Game (or cpack -R) can give another
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

# ------------------------------------------------------------
# Installer (Inno Setup): the name, version and AppId for InstallScript.iss
# ------------------------------------------------------------
# Written next to InstallScript.iss (which #includes it), so the script also works when opened in
# the Inno Setup app. Generated, so it is in .gitignore. File > Package Game passes the version
# typed in its window with /DAppVersion, which wins over the one here.
configure_file("${CMAKE_SOURCE_DIR}/Tools/Package/Installer/GameInfo.iss.in"
               "${CMAKE_SOURCE_DIR}/Tools/Package/Installer/GameInfo.iss" @ONLY NEWLINE_STYLE CRLF)

# Set startup project for Visual Studio
if(MSVC)
    set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Editor)
endif()
