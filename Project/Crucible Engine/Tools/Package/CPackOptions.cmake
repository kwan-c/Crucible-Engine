# Read by cpack when it packages (CPACK_PROJECT_CONFIG_FILE in the top-level CMakeLists.txt),
# after any version given on its command line:
#
#   cpack -C Release --config Build/CPackConfig.cmake -R 1.2.0
#
# so the zip is named after that version, not the one fixed when CMake configured.
# The editor's File > Package Game passes the version this way.
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-win64")
