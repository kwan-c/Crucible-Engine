# Offline tool like CrucibleModelCompiler: images (Assets) -> GPU-compressed .bill (Resources).
# Uses stb_image and Compressonator, which the course allows in tools only (Y*): this exe
# links them, Engine.dll and the game never do - they only read the finished .bill.
add_executable(CrucibleTextureCompiler
    main.cpp
    ../Common/TextureBuild.cpp
    ../Common/TextureImport.cpp
    ../../Engine/src/Resources/CompiledTexture.cpp
    ../../Engine/src/Resources/CompiledSky.cpp
    ../../Engine/src/Graphics/EnvironmentBake.cpp
    ../../Engine/src/Compression/Billet.cpp
    ../../Engine/src/AssetPipe/Assetmanager.cpp
    ../../Engine/src/AssetPipe/AssetRegistry.cpp
    ../../Engine/src/AssetPipe/AssetPaths.cpp
    ../../Engine/src/Systems/ExecutablePath.cpp
    ../../Engine/src/Systems/UserDataPath.cpp
    ../../Engine/src/Diagnostics/FatalError.cpp
    ../../Engine/src/Diagnostics/Logger.cpp
    ../../Engine/src/Diagnostics/LogSink.cpp
    ../../Engine/src/Diagnostics/LogTime.cpp
)
target_include_directories(CrucibleTextureCompiler PRIVATE ../../Engine/src ../Common)
target_link_libraries(CrucibleTextureCompiler PRIVATE stb rapidjson xresource compressonator_core glm)
target_compile_definitions(CrucibleTextureCompiler PRIVATE ENGINE_EXPORTS NOMINMAX)
if(MSVC)
    target_compile_options(CrucibleTextureCompiler PRIVATE /W4 /permissive-)
    target_link_options(CrucibleTextureCompiler PRIVATE /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup)
endif()
set_target_properties(CrucibleTextureCompiler PROPERTIES FOLDER "Tools")

# Runs on every build (Engine depends on it); images whose .bill is up to date are skipped.
# --report: an image that fails is listed in the report instead of failing the build (see
# Tools/Common/AssetBuildReport.h). Cook runs it strict.
add_custom_target(CompiledTextures
    COMMAND $<TARGET_FILE:CrucibleTextureCompiler> --compile-tree
        "${CMAKE_SOURCE_DIR}/Assets" "${CMAKE_SOURCE_DIR}/Resources"
        --report "${CMAKE_BINARY_DIR}/AssetBuildReports/Textures.txt"
    DEPENDS CrucibleTextureCompiler
    COMMENT "Compiling images (Assets) into GPU textures (Resources)..."
    VERBATIM
)
set_target_properties(CompiledTextures PROPERTIES FOLDER "Tools")
