# Optional Windows x64 Mono SDK integration. No downloads or global PATH changes.
option(CRUCIBLE_ENABLE_MONO "Embed the Mono C# runtime in Engine" OFF)
set(CRUCIBLE_MONO_ROOT "" CACHE PATH "Mono 6.12 x64 SDK root (bin, include, lib, etc)")

if(CRUCIBLE_ENABLE_MONO)
    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
       OR CMAKE_GENERATOR_PLATFORM MATCHES "[Aa][Rr][Mm]"
       OR CMAKE_SYSTEM_PROCESSOR MATCHES "[Aa][Rr][Mm]|aarch")
        message(FATAL_ERROR "Crucible's Mono deployment currently supports Windows x64 only.")
    endif()
    if(NOT CRUCIBLE_MONO_ROOT AND DEFINED ENV{MONO_ROOT})
        set(CRUCIBLE_MONO_ROOT "$ENV{MONO_ROOT}" CACHE PATH "Mono SDK root" FORCE)
    endif()
    if(NOT CRUCIBLE_MONO_ROOT)
        message(FATAL_ERROR "Set CRUCIBLE_MONO_ROOT to a Mono 6.12 x64 SDK, or disable CRUCIBLE_ENABLE_MONO. See Docs/ScriptingRuntime.md.")
    endif()
    # Do not mix headers, import libraries or framework files from different SDKs.
    foreach(required IN ITEMS include/mono-2.0/mono/jit/jit.h lib/mono-2.0-sgen.lib
                              bin/mono-2.0-sgen.dll lib/mono/4.5/mscorlib.dll etc/mono/config)
        if(NOT EXISTS "${CRUCIBLE_MONO_ROOT}/${required}")
            message(FATAL_ERROR "Mono SDK is incomplete: missing ${CRUCIBLE_MONO_ROOT}/${required}")
        endif()
    endforeach()
    add_library(CrucibleMono SHARED IMPORTED)
    set_target_properties(CrucibleMono PROPERTIES
        IMPORTED_IMPLIB "${CRUCIBLE_MONO_ROOT}/lib/mono-2.0-sgen.lib"
        IMPORTED_LOCATION "${CRUCIBLE_MONO_ROOT}/bin/mono-2.0-sgen.dll"
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${CRUCIBLE_MONO_ROOT}/include/mono-2.0"
        INTERFACE_INCLUDE_DIRECTORIES "${CRUCIBLE_MONO_ROOT}/include/mono-2.0")
    target_link_libraries(Engine PRIVATE CrucibleMono)
    target_compile_definitions(Engine PRIVATE CRUCIBLE_WITH_MONO=1)
else()
    target_compile_definitions(Engine PRIVATE CRUCIBLE_WITH_MONO=0)
endif()

function(crucible_deploy_mono target)
    if(CRUCIBLE_ENABLE_MONO)
        # This target runs on incremental builds too, so a removed deployment is repaired.
        add_custom_target(${target}MonoRuntime
            COMMAND "${CMAKE_COMMAND}"
                "-DMONO_SDK=${CRUCIBLE_MONO_ROOT}"
                "-DMONO_DEST=$<TARGET_FILE_DIR:${target}>"
                -P "${CMAKE_SOURCE_DIR}/Engine/DeployMono.cmake"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>/Managed"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_BINARY_DIR}/ManagedScripts/$<CONFIG>/Crucible.Examples.dll"
                "$<TARGET_FILE_DIR:${target}>/Managed/Crucible.Examples.dll"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_BINARY_DIR}/ManagedScripts/$<CONFIG>/Crucible.Examples.dll.mdb"
                "$<TARGET_FILE_DIR:${target}>/Managed/Crucible.Examples.dll.mdb"
            COMMENT "Deploying Mono runtime for ${target}"
            VERBATIM)
        set_target_properties(${target}MonoRuntime PROPERTIES FOLDER "CMakePredefinedTargets")
        add_dependencies(${target}MonoRuntime CrucibleManagedExamples)
        add_dependencies(${target} ${target}MonoRuntime)
    endif()
endfunction()
