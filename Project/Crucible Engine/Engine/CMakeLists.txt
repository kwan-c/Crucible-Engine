# ------------------------------------------------------------ #
# Crucible Engine DLL
# ------------------------------------------------------------ #
project(Engine)

# Gather engine source and header files
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
file(GLOB_RECURSE ENGINE_HEADERS CONFIGURE_DEPENDS 
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.tpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)

# Create the DLL
add_library(Engine SHARED
    ${ENGINE_SOURCES}
    ${ENGINE_HEADERS}
)

target_precompile_headers(Engine PRIVATE src/pch.h)
set_target_properties(Engine PROPERTIES LINKER_LANGUAGE CXX)

# Set warning level
if(MSVC)
    target_compile_options(Engine PRIVATE
        /W4 /permissive-

        # Do not run code analysis (the C26xxx and C6xxx checks) inside third-party headers.
        # CMake already passes every SYSTEM include as /external:I with /external:W0, but that
        # only lowers the COMPILER warning level - analysis warnings have no level to lower, so
        # they need this switch too. Every check stays on over our own code.
        /analyze:external-
	/MP
    )
else()
    target_compile_options(Engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Define export macro for DLL with relative paths from executable location
target_compile_definitions(Engine 
    PRIVATE
        ENGINE_EXPORTS
    PUBLIC
        VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
        VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS   # To handle swap chain recreation.
)

# Include directories
target_include_directories(Engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link against third-party dependencies
target_link_libraries(Engine PUBLIC EngineThirdParty)

# NOTE: Engine.dll is copied next to each executable by that executable's own
# POST_BUILD step (see Game/CMakeLists.txt and Editor/CMakeLists.txt).
add_custom_command(TARGET Engine POST_BUILD

    # Ensure Game's output directory exists
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:Game>"

    # Copy Engine.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:Engine>"
        "$<TARGET_FILE_DIR:Game>"

    COMMENT "Copying Engine.dll to Game output directory..."
)

# Organize files in Visual Studio Solution Explorer
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" 
    FILES ${ENGINE_SOURCES} ${ENGINE_HEADERS}
)