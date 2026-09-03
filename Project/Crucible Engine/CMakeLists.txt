# Minimum version of CMake for this project.
cmake_minimum_required(VERSION 3.23.1)

project(Crucible LANGUAGES CXX)

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

# ------------------------------------------------------------
# Engine Projects
# ------------------------------------------------------------
add_subdirectory(Engine)
add_subdirectory(Game)
#add_subdirectory(ManagedScripts)

# Set startup project for Visual Studio
if(MSVC)
    set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Game)
endif()