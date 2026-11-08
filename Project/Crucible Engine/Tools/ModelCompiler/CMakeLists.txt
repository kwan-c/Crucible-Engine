# Offline tool: intentionally does not link Engine.dll or a Vulkan renderer.
add_executable(CrucibleModelCompiler
    main.cpp
    ../../Engine/src/AssetPipe/GLTFLoader.cpp
    ../../Engine/src/AssetPipe/GLTFDocument.cpp
    ../../Engine/src/AssetPipe/Assetmanager.cpp
    ../../Engine/src/AssetPipe/AssetRegistry.cpp
    ../../Engine/src/Resources/CompiledModel.cpp
    ../../Engine/src/Resources/CompiledTexture.cpp
    ../../Engine/src/Compression/Billet.cpp
    ../Common/TextureBuild.cpp
    ../Common/ModelBuild.cpp
    ../../Engine/src/Systems/ExecutablePath.cpp
    ../../Engine/src/AssetPipe/AssetPaths.cpp
    ../../Engine/src/Diagnostics/FatalError.cpp
    ../../Engine/src/Diagnostics/Logger.cpp
    ../../Engine/src/Diagnostics/LogSink.cpp
    ../../Engine/src/Diagnostics/LogTime.cpp
)
target_include_directories(CrucibleModelCompiler PRIVATE ../../Engine/src ../Common)
target_include_directories(CrucibleModelCompiler SYSTEM PRIVATE
    ${Vulkan_INCLUDE_DIRS} ../../Extern/glfw/include)
# stb + Compressonator (tools only): embedded images are compressed here, never in the game.
target_link_libraries(CrucibleModelCompiler PRIVATE glm rapidjson xresource vma stb compressonator_core)
target_compile_definitions(CrucibleModelCompiler PRIVATE ENGINE_EXPORTS NOMINMAX
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS)
if(MSVC)
    target_compile_options(CrucibleModelCompiler PRIVATE /W4 /permissive-)
    target_link_options(CrucibleModelCompiler PRIVATE /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup)
endif()
set_target_properties(CrucibleModelCompiler PROPERTIES FOLDER "Tools")

# A lightweight hash/format check on each build picks up exports without needing
# to regenerate the solution. Actual source parsing happens only offline here.
# --report: a model that fails is listed in the report instead of failing the build, so the
# Editor still opens (it shows the problems and blocks Play). Cook runs it strict.
add_custom_target(CompiledModels
    COMMAND $<TARGET_FILE:CrucibleModelCompiler> --compile-tree
        "${CMAKE_SOURCE_DIR}/Assets" "${CMAKE_SOURCE_DIR}/Resources"
        --report "${CMAKE_BINARY_DIR}/AssetBuildReports/Models.txt"
    DEPENDS CrucibleModelCompiler
    COMMENT "Compiling source models (Assets) into Crucible resources (Resources)..."
    VERBATIM
)
set_target_properties(CompiledModels PROPERTIES FOLDER "Tools")
