# Builds and runs tests/subproject, which adds Composia's source tree to another build.
get_filename_component(build_root "${BINARY_DIR}" REALPATH)
set(stage "${build_root}/subproject-test")
file(REMOVE_RECURSE "${stage}")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}/tests/subproject" -B "${stage}" -G Ninja
    "-DCMAKE_BUILD_TYPE=${CONFIG}" "-DCMAKE_CXX_COMPILER=${COMPILER}"
    "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN}" "-DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}"
    "-DVCPKG_TARGET_TRIPLET=${TRIPLET}" "-DCOMPOSIA_SOURCE_DIR=${SOURCE_DIR}"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${stage}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${stage}/subproject.exe" COMMAND_ERROR_IS_FATAL ANY)
# A failure above stops the script and leaves the build directory for inspection.
file(REMOVE_RECURSE "${stage}")
