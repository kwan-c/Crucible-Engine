# Standalone C# projects, built offline with the matching Mono SDK.
if(NOT CRUCIBLE_ENABLE_MONO)
    return()
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/ManagedLibrary.cmake")
# Every .cs under Runtime/ (including subfolders) is part of the engine C# API.
file(GLOB_RECURSE script_core_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Runtime/*.cs")
crucible_add_managed_library(CrucibleScriptCore
    ASSEMBLY Crucible.ScriptCore SOURCES ${script_core_sources})
crucible_add_managed_library(CrucibleManagedExamples
    ASSEMBLY Crucible.Examples
    SOURCES Examples/Counter.cs Examples/LifecycleCounter.cs)

# Gameplay scripts live with the game's assets: every .cs under Assets/Scripts
# (subfolders allowed) goes into Crucible.Scripts.dll. Adding/removing .cs files
# regenerates the build; no runtime file watcher or source-asset loader is added.
# AssemblyInfo.cs stays here so the assembly still builds when there are no scripts.
file(GLOB_RECURSE project_scripts CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Assets/Scripts/*.cs")
crucible_add_managed_library(CrucibleProjectScripts
    ASSEMBLY Crucible.Scripts SOURCES AssemblyInfo.cs ${project_scripts} REFERENCES CrucibleScriptCore
    FALLBACK_SOURCE AssemblyInfo.cs)   # a broken gameplay script warns; it never stops the engine build
add_custom_target(CrucibleManagedScripts)
get_property(managed_targets GLOBAL PROPERTY CRUCIBLE_MANAGED_TARGETS)
add_dependencies(CrucibleManagedScripts ${managed_targets})
set_target_properties(CrucibleManagedScripts PROPERTIES FOLDER "Managed")
