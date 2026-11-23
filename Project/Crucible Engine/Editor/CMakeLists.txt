# ------------------------------------------------------------ #
# Crucible Editor
# ------------------------------------------------------------ #
project(Editor)

# Gather editor source and header files
file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
file(GLOB_RECURSE EDITOR_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.tpp"
)

# Create the executable. Cook.cpp is shared with the CrucibleCook tool (File -> Cook Resources).
add_executable(Editor
    ${EDITOR_SOURCES}
    ${EDITOR_HEADERS}
    "${CMAKE_SOURCE_DIR}/Tools/Cook/Cook.cpp"
    "${CMAKE_SOURCE_DIR}/Tools/Cook/Cook.h"
    # The Texture Importer compiles one image the same way the build does (stb + Compressonator: editor only).
    "${CMAKE_SOURCE_DIR}/Tools/Common/TextureBuild.cpp"
    "${CMAKE_SOURCE_DIR}/Tools/Common/TextureBuild.h"
    "${CMAKE_SOURCE_DIR}/Tools/Common/TextureImport.cpp"
    "${CMAKE_SOURCE_DIR}/Tools/Common/TextureImport.h"
    # The Asset Browser compiles a .glb as soon as it appears in Assets, the same way the build
    # does. These are left out of Engine.dll on purpose: only tools and the editor read source models.
    "${CMAKE_SOURCE_DIR}/Tools/Common/ModelBuild.cpp"
    "${CMAKE_SOURCE_DIR}/Tools/Common/ModelBuild.h"
    "${CMAKE_SOURCE_DIR}/Engine/src/AssetPipe/GLTFLoader.cpp"
    "${CMAKE_SOURCE_DIR}/Engine/src/AssetPipe/GLTFDocument.cpp"
    "${CMAKE_SOURCE_DIR}/Engine/src/Resources/CompiledModel.cpp"
)

target_precompile_headers(Editor PRIVATE src/pch.h)
set_target_properties(Editor PROPERTIES LINKER_LANGUAGE CXX)

# Set warning level
if(MSVC)
    target_compile_options(Editor PRIVATE /W4 /permissive- /MP)
else()
    target_compile_options(Editor PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Include directories
target_include_directories(Editor
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/Tools/Cook
        ${CMAKE_SOURCE_DIR}/Tools/Common
)

# Link against the engine (brings in EngineThirdParty transitively) plus the
# Editor-only ImGui libraries. Engine and Game must NOT link EditorThirdParty.
target_link_libraries(Editor PRIVATE Engine EditorThirdParty)
crucible_deploy_mono(Editor)
# The Editor compiles C# scripts itself on save (Editor/src/Scripting/ScriptCompiler.cpp)
# with this SDK's mono.exe + mcs.exe.
if(CRUCIBLE_ENABLE_MONO)
    target_compile_definitions(Editor PRIVATE CRUCIBLE_MONO_SDK="${CRUCIBLE_MONO_ROOT}")
endif()

# File > Package Game builds the Release game with this machine's CMake, from this build folder,
# then zips it with CPack (Editor/src/Packaging/PackageJob.h).
target_compile_definitions(Editor PRIVATE
    CRUCIBLE_CMAKE_COMMAND="${CMAKE_COMMAND}"
    CRUCIBLE_CPACK_COMMAND="${CMAKE_CPACK_COMMAND}"
    CRUCIBLE_BUILD_DIR="${CMAKE_BINARY_DIR}"
)

# Network > Connection Server starts CrucibleConnectionServer.exe (ConnectionServer/): building the
# Editor builds it too, so the exe is there to start.
add_dependencies(Editor CrucibleConnectionServer)

# Copy Engine.dll to Editor output directory (ImGui is static, so no imgui.dll)
add_custom_command(TARGET Editor POST_BUILD

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:glfw>" "$<TARGET_FILE_DIR:Editor>"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:rttr_core>" "$<TARGET_FILE_DIR:Editor>"

    # Engine.dll's audio calls FMOD, so Windows will not start Editor.exe without this beside it.
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/Extern/fmod/bin/fmod.dll" "$<TARGET_FILE_DIR:Editor>"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/Extern/GameNetworkingSockets/bin/GameNetworkingSockets.dll" 
        "$<TARGET_FILE_DIR:Editor>"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/Extern/GameNetworkingSockets/bin/libprotobuf.dll" 
        "$<TARGET_FILE_DIR:Editor>"

    COMMENT "Copying Engine.dll, glfw3.dll, rttr_core.dll, GameNetworkingSockets.dll, libprotobuf.dll and fmod.dll to Editor output directory..."
)

# Debugger working directory (Visual Studio)
set_target_properties(Editor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:Editor>"
)

# Organize files in Visual Studio Solution Explorer
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    FILES ${EDITOR_SOURCES} ${EDITOR_HEADERS}
)
