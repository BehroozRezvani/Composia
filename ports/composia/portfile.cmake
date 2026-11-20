vcpkg_check_linkage(ONLY_STATIC_LIBRARY)
# Pin the library tree independently of subsequent packaging and documentation commits.
set(revision 52e927b8450890bcb09f99ed83f7d2c5d9d9a9f9)
get_filename_component(repository "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
vcpkg_find_acquire_program(GIT)
execute_process(COMMAND "${GIT}" -C "${repository}" cat-file -e "${revision}^{commit}"
    RESULT_VARIABLE missing OUTPUT_QUIET ERROR_QUIET)
if(missing)
    set(repository "https://github.com/BehroozRezvani/Composia.git")
endif()
vcpkg_from_git(OUT_SOURCE_PATH source URL "${repository}" REF "${revision}")
vcpkg_check_features(OUT_FEATURE_OPTIONS features FEATURES controls COMPOSIA_BUILD_CONTROLS)
find_program(clang_cl clang-cl HINTS "$ENV{VSINSTALLDIR}/VC/Tools/Llvm/x64/bin" REQUIRED)
vcpkg_cmake_configure(SOURCE_PATH "${source}"
    OPTIONS ${features} "-DCMAKE_CXX_COMPILER=${clang_cl}"
        -DCOMPOSIA_INSTALL=ON -DCOMPOSIA_BUILD_EXAMPLES=OFF -DCOMPOSIA_BUILD_TESTS=OFF
        -DCOMPOSIA_WARNINGS_AS_ERRORS=OFF)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME Composia CONFIG_PATH lib/cmake/Composia)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${source}/LICENSE")
