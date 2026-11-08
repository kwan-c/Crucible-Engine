# Cook (see Cook.h): fills Resources with everything the shipped game reads.
# Offline tool like CrucibleModelCompiler: builds the few AssetPipe sources it needs
# instead of linking Engine.dll. The editor compiles Cook.cpp too (File -> Cook Resources).
add_executable(CrucibleCook
    main.cpp
    Cook.cpp
    ../../Engine/src/AssetPipe/Assetmanager.cpp
    ../../Engine/src/AssetPipe/AssetRegistry.cpp
    ../../Engine/src/AssetPipe/AssetPaths.cpp
    ../../Engine/src/Systems/ExecutablePath.cpp
    ../../Engine/src/Diagnostics/FatalError.cpp
    ../../Engine/src/Diagnostics/Logger.cpp
    ../../Engine/src/Diagnostics/LogSink.cpp
    ../../Engine/src/Diagnostics/LogTime.cpp
)
target_include_directories(CrucibleCook PRIVATE ../../Engine/src)
target_link_libraries(CrucibleCook PRIVATE xresource)
target_compile_definitions(CrucibleCook PRIVATE ENGINE_EXPORTS NOMINMAX)
if(MSVC)
    target_compile_options(CrucibleCook PRIVATE /W4 /permissive-)
    target_link_options(CrucibleCook PRIVATE /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup)
endif()
set_target_properties(CrucibleCook PROPERTIES FOLDER "Tools")

# For the installer build: cmake --build Build --config Release --target Cook
# Compiles models, textures and shaders first (their targets), then copies the rest into Resources.
# The models and images are compiled again here WITHOUT --report: the everyday build lets a bad
# asset through (so the Editor opens), but a shipped game must not, so Cook stops on one.
# Up-to-date files are skipped, so this costs nothing when the build already compiled them.
add_custom_target(Cook
    COMMAND $<TARGET_FILE:CrucibleModelCompiler> --compile-tree "${CMAKE_SOURCE_DIR}/Assets" "${CMAKE_SOURCE_DIR}/Resources"
    COMMAND $<TARGET_FILE:CrucibleTextureCompiler> --compile-tree "${CMAKE_SOURCE_DIR}/Assets" "${CMAKE_SOURCE_DIR}/Resources"
    COMMAND $<TARGET_FILE:CrucibleCook> "${CMAKE_SOURCE_DIR}/Assets" "${CMAKE_SOURCE_DIR}/Resources"
    COMMENT "Cooking Assets into Resources..."
    VERBATIM
)
add_dependencies(Cook CrucibleCook CompiledModels CompiledTextures)
if(TARGET Shaders)   # missing when slangc is not installed (CompileShaders.cmake)
    add_dependencies(Cook Shaders)
endif()
set_target_properties(Cook PROPERTIES FOLDER "Tools")
