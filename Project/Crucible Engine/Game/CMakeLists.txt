# ------------------------------------------------------------ #
# Game Executable
# ------------------------------------------------------------ #
project(Game)

# Gather game-specific source files
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
file(GLOB_RECURSE GAME_HEADERS CONFIGURE_DEPENDS 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)

# Create executable (removed WIN32 to use console entry point)
add_executable(Game 
    ${GAME_SOURCES}
    ${GAME_HEADERS}    
)

# Set warning level and fix runtime library conflicts
if(MSVC)
    target_compile_options(Game PRIVATE /W4 /permissive-)
else()
    target_compile_options(Game PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Link against Crucible engine
target_link_libraries(Game PRIVATE Engine)

# Set output name and Visual Studio debugging directory
set_target_properties(Game PROPERTIES
    OUTPUT_NAME "Mekanomachia"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:Game>"
)

# Define output directory variable for cleaner commands
set(GAME_OUTPUT_DIR "$<TARGET_FILE_DIR:Game>")

add_custom_command(TARGET Game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Engine>"
        "${GAME_OUTPUT_DIR}"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:glfw>"
        "${GAME_OUTPUT_DIR}"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:rttr_core>"
        "${GAME_OUTPUT_DIR}"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/Extern/fmod/bin/fmod.dll"
        "${GAME_OUTPUT_DIR}"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/Extern/fmod/bin/fmodstudio.dll"
        "${GAME_OUTPUT_DIR}"
)

# Ensure proper build order
add_dependencies(Game 
    #CopyAssets 
    Engine 
    #ManagedScripts
)

install(FILES 
    "$<TARGET_FILE:Engine>"
    DESTINATION .
)

# Organize files in Visual Studio Solution Explorer
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" 
    FILES ${GAME_SOURCES} ${GAME_HEADERS}
)