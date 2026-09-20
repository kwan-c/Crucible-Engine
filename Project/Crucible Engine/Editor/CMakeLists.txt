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
set_target_properties(Editor PROPERTIES LINKER_LANGUAGE CXX)

# Set warning level
if(MSVC)
    target_compile_options(Editor PRIVATE /W4 /permissive-)
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

# Copy Engine.dll to Editor output directory (ImGui is static, so no imgui.dll)
add_custom_command(TARGET Editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Engine>" "$<TARGET_FILE_DIR:Editor>"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:glfw>" "$<TARGET_FILE_DIR:Editor>"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:rttr_core>" "$<TARGET_FILE_DIR:Editor>"

    COMMENT "Copying Engine.dll, glfw3.dll and rttr_core.dll to Editor output directory..."
)

# Debugger working directory (Visual Studio)
set_target_properties(Editor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:Editor>"
)

# Organize files in Visual Studio Solution Explorer
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src"
    FILES ${EDITOR_SOURCES} ${EDITOR_HEADERS}
)