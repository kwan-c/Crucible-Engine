if(NOT DEFINED MONO_SDK OR NOT DEFINED MONO_DEST)
    message(FATAL_ERROR "DeployMono requires MONO_SDK and MONO_DEST")
endif()
file(MAKE_DIRECTORY "${MONO_DEST}/Mono/lib/mono" "${MONO_DEST}/Mono/etc")
# Use the SDK's runtime DLLs as one matching set; exclude executables/toolchains.
file(GLOB native_dlls "${MONO_SDK}/bin/*.dll")
foreach(dll IN LISTS native_dlls)
    get_filename_component(name "${dll}" NAME)
    file(COPY_FILE "${dll}" "${MONO_DEST}/${name}" ONLY_IF_DIFFERENT)
endforeach()
# Preserve the complete runtime profile (including Facades), not just mscorlib.
# file(COPY) preserves timestamps and only replaces changed files.
file(COPY "${MONO_SDK}/lib/mono/4.5" DESTINATION "${MONO_DEST}/Mono/lib/mono")
file(COPY "${MONO_SDK}/etc/mono" DESTINATION "${MONO_DEST}/Mono/etc")
foreach(notice IN ITEMS LICENSE PATENTS.TXT COPYING THIRD-PARTY-NOTICES.TXT)
    if(EXISTS "${MONO_SDK}/${notice}")
        file(COPY "${MONO_SDK}/${notice}" DESTINATION "${MONO_DEST}/Mono")
    endif()
endforeach()
