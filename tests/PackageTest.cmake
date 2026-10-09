get_filename_component(build_root "${BINARY_DIR}" REALPATH)
string(RANDOM LENGTH 8 ALPHABET 0123456789abcdef suffix)
set(stage "${build_root}/package-test-${suffix}")
file(MAKE_DIRECTORY "${stage}")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BINARY_DIR}" --config "${CONFIG}" --prefix "${stage}/install"
    COMMAND_ERROR_IS_FATAL ANY)
cmake_path(IS_PREFIX build_root "${stage}/install" NORMALIZE inside_build)
if(NOT inside_build)
    message(FATAL_ERROR "Package staging directory is outside the build directory")
endif()
file(RENAME "${stage}/install" "${stage}/relocated")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}/tests/consumer" -B "${stage}/consumer" -G Ninja
    "-DCMAKE_BUILD_TYPE=${CONFIG}" "-DCMAKE_CXX_COMPILER=${COMPILER}"
    "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN}" "-DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}"
    "-DVCPKG_TARGET_TRIPLET=${TRIPLET}" "-DCMAKE_PREFIX_PATH=${stage}/relocated"
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=${RUNTIME}"
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${stage}/consumer" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${stage}/consumer/consumer.exe" COMMAND_ERROR_IS_FATAL ANY)
# A failure above stops the script and leaves the staging directory for inspection.
file(REMOVE_RECURSE "${stage}")
