# Copies Engine.dll beside the Editor (Engine's POST_BUILD, see Engine/CMakeLists.txt), without
# failing the build when that Editor is running: Windows locks a DLL a program has loaded.
#
# That happens when File > Package Game in a Release Editor builds the engine in Release while
# the Editor runs from the same Build/Editor/Release folder. The packaged game takes its
# Engine.dll from the engine's own output folder, so it is not affected; the running Editor just
# keeps its old Engine.dll until it is closed and built again.
#
#   cmake -DSOURCE=<Engine.dll> -DDESTINATION=<Editor folder> -P CopyToRunningEditor.cmake
if(NOT DEFINED SOURCE OR NOT DEFINED DESTINATION)
    message(FATAL_ERROR "CopyToRunningEditor requires SOURCE and DESTINATION")
endif()
get_filename_component(name "${SOURCE}" NAME)
file(COPY_FILE "${SOURCE}" "${DESTINATION}/${name}" ONLY_IF_DIFFERENT RESULT result)
if(NOT result EQUAL 0)
    message(WARNING "Could not update ${DESTINATION}/${name} (${result}). If the Editor is running, "
                    "it keeps its old ${name} until it is closed and built again.")
endif()
