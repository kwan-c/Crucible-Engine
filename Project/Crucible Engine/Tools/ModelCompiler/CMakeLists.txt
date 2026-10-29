# Offline tool: intentionally does not link Engine.dll or a Vulkan renderer.
add_executable(CrucibleModelCompiler
    main.cpp
    ../../Engine/src/AssetPipe/GLTFLoader.cpp
    ../../Engine/src/AssetPipe/GLTFDocument.cpp
    ../../Engine/src/AssetPipe/Assetmanager.cpp
    ../../Engine/src/AssetPipe/AssetRegistry.cpp
    ../../Engine/src/Resources/CompiledModel.cpp
)
target_include_directories(CrucibleModelCompiler PRIVATE ../../Engine/src)
target_include_directories(CrucibleModelCompiler SYSTEM PRIVATE
    ${Vulkan_INCLUDE_DIRS} ../../Extern/glfw/include)
target_link_libraries(CrucibleModelCompiler PRIVATE glm rapidjson xresource vma)
target_compile_definitions(CrucibleModelCompiler PRIVATE ENGINE_EXPORTS NOMINMAX
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS)
if(MSVC)
    target_compile_options(CrucibleModelCompiler PRIVATE /W4 /permissive-)
    target_link_options(CrucibleModelCompiler PRIVATE /SUBSYSTEM:CONSOLE /ENTRY:mainCRTStartup)
endif()
set_target_properties(CrucibleModelCompiler PROPERTIES FOLDER "Tools")

# A lightweight hash/format check on each build picks up exports without needing
# to regenerate the solution. Actual source parsing happens only offline here.
add_custom_target(CompiledModels
    COMMAND $<TARGET_FILE:CrucibleModelCompiler> --compile-tree
        "${CMAKE_SOURCE_DIR}/AssetsSource" "${CMAKE_SOURCE_DIR}/Assets"
    DEPENDS CrucibleModelCompiler
    COMMENT "Compiling source models into Crucible resources..."
    VERBATIM
)
set_target_properties(CompiledModels PROPERTIES FOLDER "Tools")
