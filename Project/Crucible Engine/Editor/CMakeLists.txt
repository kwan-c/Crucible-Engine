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

# Create the executable
add_executable(Editor
    ${EDITOR_SOURCES}
    ${EDITOR_HEADERS}
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
)

# Link against the engine (brings in EngineThirdParty transitively) plus the
# Editor-only ImGui libraries. Engine and Game must NOT link EditorThirdParty.
target_link_libraries(Editor PRIVATE Engine EditorThirdParty)
crucible_deploy_mono(Editor)

# Copy Engine.dll to Editor output directory (ImGui is static, so no imgui.dll)
add_custom_command(TARGET Editor POST_BUILD

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:glfw>" "$<TARGET_FILE_DIR:Editor>"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:rttr_core>" "$<TARGET_FILE_DIR:Editor>"

    # Engine.dll's audio calls FMOD, so Windows will not start Editor.exe without this beside it.
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/Extern/fmod/bin/fmod.dll" "$<TARGET_FILE_DIR:Editor>"

    COMMENT "Copying Engine.dll, glfw3.dll, rttr_core.dll and fmod.dll to Editor output directory..."
)

# Debugger working directory (Visual Studio)
set_target_properties(Editor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:Editor>"
)

# Organize files in Visual Studio Solution Explorer
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    FILES ${EDITOR_SOURCES} ${EDITOR_HEADERS}
)
