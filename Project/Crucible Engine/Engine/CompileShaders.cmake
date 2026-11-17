# ------------------------------------------------------------ #
# Shader compilation (Slang -> SPIR-V)
# ------------------------------------------------------------ #
# Compiles every shader in Assets/Shaders with slangc as part of the normal build.
# Engine depends on the "Shaders" target, so building Editor or Game first rebuilds
# any shader whose source changed, and a shader error shows up as a build error.
# Each .spv is written to Resources/Shaders/<Name>.spv (the compiled files the game
# reads), where the passes load it from (VulkanShader.h). The .slang sources stay in Assets.
#
# Adding a shader: add one crucible_shader(<Name> <entry points...>) line below.
# Every other .slang in the folder (Common.slang, ...) counts as a shared include:
# editing one rebuilds every shader. A new .slang file needs a CMake re-run.

set(CRUCIBLE_SHADER_DIR "${CMAKE_SOURCE_DIR}/Assets/Shaders")
set(CRUCIBLE_SHADER_OUT_DIR "${CMAKE_SOURCE_DIR}/Resources/Shaders")
file(MAKE_DIRECTORY "${CRUCIBLE_SHADER_OUT_DIR}")

# <Name>.slang -> <Name>.spv, with these entry points.
set(CRUCIBLE_SHADERS "")
macro(crucible_shader name)
    list(APPEND CRUCIBLE_SHADERS ${name})
    set(CRUCIBLE_SHADER_ENTRIES_${name} ${ARGN})
endmacro()

crucible_shader(DefaultShader    vertMain fragMain)
# More mesh shaders: a surface function followed by SurfaceTemplate.slang (see Surface.slang), with
# the same two entry points. A material picks one by name ("shader" in its .mat).
# One that moves vertices (SURFACE_MOVES_VERTICES in Surface.slang) also needs, on its line here,
# the eleven entry points at the end of SurfaceTemplate.slang: shadowVert shadowCutoutVert
# shadowCutoutFrag depthNormalVert depthNormalFrag depthNormalCutoutVert depthNormalCutoutFrag
# maskVert maskFrag maskCutoutVert maskCutoutFrag. (A shader graph's shader is compiled by the
# Editor, which adds them itself.)
crucible_shader(MagicOrb         vertMain fragMain)
# What a material is drawn with when its shader cannot be (missing .spv, a broken or deleted graph).
crucible_shader(ErrorShader      vertMain fragMain)
crucible_shader(Skybox           skyVert skyFrag)
crucible_shader(DebugLine        lineVert sphereOutlineVert lineFrag)
crucible_shader(Shadow           shadowVert shadowCutoutVert shadowCutoutFrag)
crucible_shader(OutlineMask      maskVert maskFrag maskCutoutVert maskCutoutFrag)
crucible_shader(OutlineComposite compositeVert compositeFrag)
crucible_shader(Particle         particleVert particleFrag)
crucible_shader(Post             postVert bloomDownFrag bloomUpFrag tonemapFrag aoFrag aoBlurFrag toonOutlineFrag)
crucible_shader(DepthNormal      depthNormalVert depthNormalFrag depthNormalCutoutVert depthNormalCutoutFrag)
crucible_shader(UI               uiVert uiFrag)
crucible_shader(VolumetricFog    fogInject fogIntegrate)
crucible_shader(Water            waterVert waterFrag)
crucible_shader(WorldUI          worldUIVert worldUIFrag)

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
                    "The .spv files already in Resources/Shaders are used as they are.")
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
    set(_shader_out "${CRUCIBLE_SHADER_OUT_DIR}/${_shader}.spv")

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

# A mesh shader's parameter list (<Name>.params.json beside its .slang: the numbers, colours and
# textures a material using the shader can set - Engine/src/Graphics/MaterialShaderParams.h) is
# copied beside its .spv as it is, so the game finds both in Resources/Shaders.
file(GLOB CRUCIBLE_SHADER_PARAM_LISTS CONFIGURE_DEPENDS "${CRUCIBLE_SHADER_DIR}/*.params.json")
foreach(_list IN LISTS CRUCIBLE_SHADER_PARAM_LISTS)
    get_filename_component(_list_name "${_list}" NAME)
    set(_list_out "${CRUCIBLE_SHADER_OUT_DIR}/${_list_name}")
    add_custom_command(
        OUTPUT  "${_list_out}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_list}" "${_list_out}"
        DEPENDS "${_list}"
        COMMENT "Copying shader parameter list ${_list_name}"
        VERBATIM
    )
    list(APPEND CRUCIBLE_SHADER_OUTPUTS "${_list_out}")
endforeach()

# The .slang files (and the parameter lists) show up under this project in Visual Studio.
add_custom_target(Shaders ALL
    DEPENDS ${CRUCIBLE_SHADER_OUTPUTS}
    SOURCES ${CRUCIBLE_SHADER_SOURCES} ${CRUCIBLE_SHADER_PARAM_LISTS}
)
source_group("Shaders" FILES ${CRUCIBLE_SHADER_SOURCES} ${CRUCIBLE_SHADER_PARAM_LISTS})

add_dependencies(Engine Shaders)
