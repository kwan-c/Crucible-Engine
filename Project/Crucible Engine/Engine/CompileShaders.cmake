# ------------------------------------------------------------ #
# Shader compilation (Slang -> SPIR-V)
# ------------------------------------------------------------ #
# Compiles every shader in Assets/Shaders with slangc as part of the normal build.
# Engine depends on the "Shaders" target, so building Editor or Game first rebuilds
# any shader whose source changed, and a shader error shows up as a build error.
# Each .spv is written next to its .slang, where the passes load it from
# (../../../Assets/Shaders/<Name>.spv), so nothing at runtime changes.
#
# Adding a shader: add one crucible_shader(<Name> <entry points...>) line below.
# Every other .slang in the folder (Common.slang, ...) counts as a shared include:
# editing one rebuilds every shader. A new .slang file needs a CMake re-run.

set(CRUCIBLE_SHADER_DIR "${CMAKE_SOURCE_DIR}/Assets/Shaders")

# <Name>.slang -> <Name>.spv, with these entry points.
set(CRUCIBLE_SHADERS "")
macro(crucible_shader name)
    list(APPEND CRUCIBLE_SHADERS ${name})
    set(CRUCIBLE_SHADER_ENTRIES_${name} ${ARGN})
endmacro()

crucible_shader(DefaultShader    vertMain fragMain)
crucible_shader(Skybox           skyVert skyFrag)
crucible_shader(DebugLine        lineVert lineFrag)
crucible_shader(Shadow           shadowVert)
crucible_shader(OutlineMask      maskVert maskFrag)
crucible_shader(OutlineComposite compositeVert compositeFrag)
crucible_shader(Particle         particleVert particleFrag)
crucible_shader(Post             postVert bloomDownFrag bloomUpFrag tonemapFrag aoFrag aoBlurFrag)
crucible_shader(DepthNormal      depthNormalVert depthNormalFrag)
crucible_shader(UI               uiVert uiFrag)

# Flags shared by every shader.
set(CRUCIBLE_SLANGC_FLAGS
    -target spirv
    -profile spirv_1_4
    -emit-spirv-directly
    -fvk-use-entrypoint-name
    -matrix-layout-column-major
)

# slangc ships with the Vulkan SDK, in the same Bin folder as glslc.
get_filename_component(_crucible_vulkan_bin "${Vulkan_GLSLC_EXECUTABLE}" DIRECTORY)
find_program(SLANGC_EXECUTABLE slangc
    HINTS "${_crucible_vulkan_bin}" "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
    DOC "Slang shader compiler (part of the Vulkan SDK)"
)
if(NOT SLANGC_EXECUTABLE)
    message(WARNING "slangc was not found in the Vulkan SDK, so shaders will not be compiled. "
                    "The .spv files already in Assets/Shaders are used as they are.")
    return()
endif()
message(STATUS "Shader compiler: ${SLANGC_EXECUTABLE}")

# Shared includes: every .slang in the folder that is not one of the shaders above.
file(GLOB CRUCIBLE_SHADER_SOURCES CONFIGURE_DEPENDS "${CRUCIBLE_SHADER_DIR}/*.slang")
set(CRUCIBLE_SHADER_INCLUDES ${CRUCIBLE_SHADER_SOURCES})
foreach(_shader IN LISTS CRUCIBLE_SHADERS)
    if(NOT EXISTS "${CRUCIBLE_SHADER_DIR}/${_shader}.slang")
        message(FATAL_ERROR "CompileShaders.cmake lists ${_shader}, but Assets/Shaders/${_shader}.slang does not exist.")
    endif()
    list(REMOVE_ITEM CRUCIBLE_SHADER_INCLUDES "${CRUCIBLE_SHADER_DIR}/${_shader}.slang")
endforeach()

set(CRUCIBLE_SHADER_OUTPUTS "")
foreach(_shader IN LISTS CRUCIBLE_SHADERS)
    set(_shader_src "${CRUCIBLE_SHADER_DIR}/${_shader}.slang")
    set(_shader_out "${CRUCIBLE_SHADER_DIR}/${_shader}.spv")

    set(_shader_entries "")
    foreach(_entry IN LISTS CRUCIBLE_SHADER_ENTRIES_${_shader})
        list(APPEND _shader_entries -entry ${_entry})
    endforeach()

    add_custom_command(
        OUTPUT  "${_shader_out}"
        COMMAND "${SLANGC_EXECUTABLE}" "${_shader_src}" ${CRUCIBLE_SLANGC_FLAGS} ${_shader_entries} -o "${_shader_out}"
        MAIN_DEPENDENCY "${_shader_src}"
        DEPENDS ${CRUCIBLE_SHADER_INCLUDES}
        WORKING_DIRECTORY "${CRUCIBLE_SHADER_DIR}"
        COMMENT "Compiling shader ${_shader}.slang"
        VERBATIM
    )
    list(APPEND CRUCIBLE_SHADER_OUTPUTS "${_shader_out}")
endforeach()

# The .slang files show up under this project in Visual Studio.
add_custom_target(Shaders ALL
    DEPENDS ${CRUCIBLE_SHADER_OUTPUTS}
    SOURCES ${CRUCIBLE_SHADER_SOURCES}
)
source_group("Shaders" FILES ${CRUCIBLE_SHADER_SOURCES})

add_dependencies(Engine Shaders)
