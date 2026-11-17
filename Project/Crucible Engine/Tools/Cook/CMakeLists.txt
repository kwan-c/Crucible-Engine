# Cook (see Cook.h): fills Resources with everything the shipped game reads.
# Offline tool like CrucibleModelCompiler: builds the few AssetPipe sources it needs
# instead of linking Engine.dll. The editor compiles Cook.cpp too (File -> Cook Resources).
add_executable(CrucibleCook
    main.cpp
    Cook.cpp
    CookShaderGraphs.cpp
    # Shader graphs: the generator and the compiler the Editor uses (no engine, only rapidjson).
    ../../Editor/src/ShaderGraph/ShaderGraph.cpp
    ../../Editor/src/ShaderGraph/ShaderGraphNodes.cpp
    ../../Editor/src/ShaderGraph/ShaderGraphGenerator.cpp
    ../../Editor/src/Shaders/ShaderCompiler.cpp
    ../../Engine/src/AssetPipe/Assetmanager.cpp
    ../../Engine/src/AssetPipe/AssetRegistry.cpp
    ../../Engine/src/AssetPipe/AssetPaths.cpp
    ../../Engine/src/Systems/ExecutablePath.cpp
    ../../Engine/src/Diagnostics/FatalError.cpp
    ../../Engine/src/Diagnostics/Logger.cpp
    ../../Engine/src/Diagnostics/LogSink.cpp
    ../../Engine/src/Diagnostics/LogTime.cpp
)
target_include_directories(CrucibleCook PRIVATE ../../Engine/src ../../Editor/src)
target_link_libraries(CrucibleCook PRIVATE xresource rapidjson)
target_compile_definitions(CrucibleCook PRIVATE ENGINE_EXPORTS NOMINMAX)
# The slangc the build compiles shaders with (Engine/CompileShaders.cmake), so shader graphs are
# compiled by the same one. Without it ShaderCompiler::FindCompiler looks in the Vulkan SDK.
if(SLANGC_EXECUTABLE)
    target_compile_definitions(CrucibleCook PRIVATE CRUCIBLE_SLANGC="${SLANGC_EXECUTABLE}")
endif()
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

# Every shader graph (.shadergraph) compiled into Resources/Shaders/<GUID>.spv and .params.json
# (CookShaderGraphs.h), on every build and before Game: so the game run from the build folder,
# on a clean checkout too, has every graph's shader without the Editor having compiled it.
# Unchanged graphs come from the compiler's cache (Build/Tools/Cook/<config>/Cache/Shaders), so
# this costs little. A broken graph is a warning here, not a failed build; Cook stops on it.
add_custom_target(ShaderGraphs ALL
    COMMAND $<TARGET_FILE:CrucibleCook> --shader-graphs "${CMAKE_SOURCE_DIR}/Assets" "${CMAKE_SOURCE_DIR}/Resources"
    COMMENT "Compiling shader graphs into Resources/Shaders..."
    VERBATIM
)
add_dependencies(ShaderGraphs CrucibleCook)
if(TARGET Shaders)   # the includes a graph's shader uses are compiled and checked first
    add_dependencies(ShaderGraphs Shaders)
endif()
add_dependencies(Game ShaderGraphs)
set_target_properties(ShaderGraphs PROPERTIES FOLDER "Tools")
