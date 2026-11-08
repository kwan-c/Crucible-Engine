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
    target_compile_options(Game PRIVATE /W4 /permissive- /MP)
else()
    target_compile_options(Game PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Link against Crucible engine
target_link_libraries(Game PRIVATE Engine)
crucible_deploy_mono(Game)

# Set output name and Visual Studio debugging directory
set_target_properties(Game PROPERTIES
    OUTPUT_NAME "Mekanomachia"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:Game>"
)

# Define output directory variable for cleaner commands
set(GAME_OUTPUT_DIR "$<TARGET_FILE_DIR:Game>")

add_custom_command(TARGET Game POST_BUILD

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

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/Extern/GameNetworkingSockets/bin/GameNetworkingSockets.dll"
        "${GAME_OUTPUT_DIR}"
    
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/Extern/GameNetworkingSockets/bin/libprotobuf.dll"
        "${GAME_OUTPUT_DIR}"
)

# Ensure proper build order
add_dependencies(Game 
    #CopyAssets 
    Engine 
    #ManagedScripts
)

# ------------------------------------------------------------ #
# Install: the folder a player gets
# ------------------------------------------------------------ #
# Puts everything the game needs into one folder, with Resources beside the .exe (where
# AssetPaths looks for it when there is no Assets folder):
#
#   Mekanomachia.exe, Engine.dll, the third-party DLLs, the Mono runtime,
#   Managed/ (C# scripts) and Resources/ (cooked assets)
#
# All of it is in the "Game" component. Third-party libraries (Jolt, glfw, ...) add install
# rules of their own for headers and .lib files, which a game folder does not want, so
# install this component only. Run the Cook target first so Resources is complete:
#
#   cmake --build Build --config Release --target Game Cook
#   cmake --install Build --config Release --component Game --prefix <folder>
#
# Install somewhere outside this project: within 4 folders of the source Assets folder,
# the game finds Assets and reads it instead of Resources.

install(TARGETS Game RUNTIME DESTINATION . COMPONENT Game)

install(FILES
    "$<TARGET_FILE:Engine>"
    "$<TARGET_FILE:glfw>"
    "$<TARGET_FILE:rttr_core>"
    "${CMAKE_SOURCE_DIR}/Extern/fmod/bin/fmod.dll"
    "${CMAKE_SOURCE_DIR}/Extern/fmod/bin/fmodstudio.dll"
    "${CMAKE_SOURCE_DIR}/Extern/GameNetworkingSockets/bin/GameNetworkingSockets.dll"
    "${CMAKE_SOURCE_DIR}/Extern/GameNetworkingSockets/bin/libprotobuf.dll"
    DESTINATION . COMPONENT Game
)

# The Visual C++ runtime (vcruntime140.dll, msvcp140.dll, ...). Everything is built with the
# DLL runtime (/MD), so without these beside the .exe a PC that never installed the
# "Visual C++ Redistributable" fails to start the game with "VCRUNTIME140.dll was not found".
# Microsoft allows shipping them in the app's own folder like this.
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT Game)
include(InstallRequiredSystemLibraries)

if(CRUCIBLE_ENABLE_MONO)
    # The same runtime DLLs DeployMono.cmake puts beside the .exe.
    file(GLOB GAME_MONO_DLLS "${CRUCIBLE_MONO_ROOT}/bin/*.dll")
    install(FILES ${GAME_MONO_DLLS} DESTINATION . COMPONENT Game)

    # Mono/ (framework) and Managed/ (C# scripts) as deployed by the build.
    # Left out: Mono debug symbols and DeployManaged.ps1's bookkeeping file.
    install(DIRECTORY
        "$<TARGET_FILE_DIR:Game>/Mono"
        "$<TARGET_FILE_DIR:Game>/Managed"
        DESTINATION . COMPONENT Game
        PATTERN "*.mdb" EXCLUDE
        PATTERN "Crucible.ManagedFiles.txt" EXCLUDE
    )
endif()

# Stop early with a clear message if Cook has never been run (no Resources at all).
install(CODE "
    if(NOT EXISTS \"${CMAKE_SOURCE_DIR}/Resources/Config/EngineConfig.json\")
        message(FATAL_ERROR \"Resources is not cooked (no Config/EngineConfig.json). Build the Cook target first.\")
    endif()
" COMPONENT Game)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/Resources" DESTINATION . COMPONENT Game)

# Organize files in Visual Studio Solution Explorer
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" 
    FILES ${GAME_SOURCES} ${GAME_HEADERS}
)
