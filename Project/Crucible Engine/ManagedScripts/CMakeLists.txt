# S2 example, built offline with the matching Mono SDK. No engine bindings.
if(NOT CRUCIBLE_ENABLE_MONO)
    return()
endif()
foreach(tool IN ITEMS bin/mono.exe lib/mono/4.5/mcs.exe)
    if(NOT EXISTS "${CRUCIBLE_MONO_ROOT}/${tool}")
        message(FATAL_ERROR "Managed C# compilation requires ${CRUCIBLE_MONO_ROOT}/${tool}")
    endif()
endforeach()
set(example_dll "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/Crucible.Examples.dll")
add_custom_command(OUTPUT "${example_dll}" "${example_dll}.mdb"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
    COMMAND "${CRUCIBLE_MONO_ROOT}/bin/mono.exe" "${CRUCIBLE_MONO_ROOT}/lib/mono/4.5/mcs.exe"
        -noconfig -sdk:4.5 -langversion:7.2 -platform:anycpu -target:library -debug
        "-out:${example_dll}" "${CMAKE_CURRENT_SOURCE_DIR}/Examples/Counter.cs"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Examples/Counter.cs"
    COMMENT "Compiling standalone C# example (Mono framework 4.5)"
    VERBATIM)
add_custom_target(CrucibleManagedExamples DEPENDS "${example_dll}" "${example_dll}.mdb")
set_target_properties(CrucibleManagedExamples PROPERTIES FOLDER "Managed")
