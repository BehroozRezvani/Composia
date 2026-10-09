# A test executable linking Composia::UI, or the library named by a third argument.
function(composia_test_target target source)
    set(library Composia::UI)
    if(ARGC GREATER 2)
        set(library ${ARGV2})
    endif()
    add_executable(${target} ${source} ${COMPOSIA_MANIFEST})
    target_link_libraries(${target} PRIVATE ${library})
    target_compile_options(${target} PRIVATE /W4 $<$<BOOL:${COMPOSIA_WARNINGS_AS_ERRORS}>:/WX>)
endfunction()

# The library with failure injection compiled in, for the recovery checks only.
composia_add_library(composia-testing)
target_compile_definitions(composia-testing PRIVATE COMPOSIA_TESTING)

composia_test_target(composia-virtual-tests VirtualSurfaceTests.cpp)
foreach(scenario sparse-cache partial-updates)
    add_test(NAME virtual-${scenario} COMMAND composia-virtual-tests ${scenario})
    add_test(NAME virtual-${scenario}-warp COMMAND composia-virtual-tests ${scenario} --warp)
    set_tests_properties(virtual-${scenario} virtual-${scenario}-warp PROPERTIES TIMEOUT 40 RUN_SERIAL TRUE LABELS desktop)
endforeach()
add_test(NAME virtual-arguments COMMAND composia-virtual-tests arguments --warp)
set_tests_properties(virtual-arguments PROPERTIES TIMEOUT 20 LABELS desktop)
composia_test_target(composia-capture-tests CaptureTests.cpp)
foreach(scenario window monitor)
    add_test(NAME capture-${scenario} COMMAND composia-capture-tests ${scenario})
    add_test(NAME capture-${scenario}-warp COMMAND composia-capture-tests ${scenario} --warp)
    set_tests_properties(capture-${scenario} capture-${scenario}-warp PROPERTIES TIMEOUT 40 RUN_SERIAL TRUE LABELS desktop SKIP_RETURN_CODE 77)
endforeach()
composia_test_target(composia-texture-tests TextureTests.cpp)
add_test(NAME texture-surface COMMAND composia-texture-tests)
add_test(NAME texture-surface-warp COMMAND composia-texture-tests --warp)
set_tests_properties(texture-surface texture-surface-warp PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE LABELS desktop SKIP_RETURN_CODE 77)
composia_test_target(composia-foundation-tests FoundationTests.cpp)
foreach(scenario pointer focus render partial swapchain accessible native)
    add_test(NAME foundation-${scenario} COMMAND composia-foundation-tests ${scenario})
    add_test(NAME foundation-${scenario}-warp COMMAND composia-foundation-tests ${scenario} --warp)
    set_tests_properties(foundation-${scenario} foundation-${scenario}-warp PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE LABELS desktop SKIP_RETURN_CODE 77)
endforeach()
composia_test_target(composia-dpi-tests DpiTests.cpp)
add_test(NAME monitor-dpi COMMAND composia-dpi-tests)
set_tests_properties(monitor-dpi PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE LABELS desktop)
composia_test_target(composia-interaction-tests InteractionTests.cpp)
foreach(scenario input accessibility)
    add_test(NAME ${scenario} COMMAND composia-interaction-tests ${scenario})
    set_tests_properties(${scenario} PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE LABELS desktop)
endforeach()
composia_test_target(composia-recovery-tests RecoveryTests.cpp composia-testing)
target_compile_definitions(composia-recovery-tests PRIVATE COMPOSIA_TESTING)
target_include_directories(composia-recovery-tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
foreach(scenario transaction repeated-recovery failed-recovery shutdown callback-error)
    add_test(NAME ${scenario} COMMAND composia-recovery-tests ${scenario})
    set_tests_properties(${scenario} PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE LABELS desktop)
endforeach()

composia_test_target(composia-window-tests WindowTests.cpp)
foreach(scenario secondary-close secondary-error closed-window closed-parent)
    add_test(NAME ${scenario} COMMAND composia-window-tests ${scenario})
    set_tests_properties(${scenario} PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE LABELS desktop)
endforeach()

composia_test_target(composia-surface-tests SurfaceTests.cpp)
add_test(NAME surface-pixels COMMAND composia-surface-tests)
set_tests_properties(surface-pixels PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE LABELS desktop)

composia_test_target(composia-smoke-tests SmokeTests.cpp)
add_test(NAME smoke COMMAND composia-smoke-tests)
add_test(NAME smoke-warp COMMAND composia-smoke-tests --warp)
set_tests_properties(smoke smoke-warp PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE LABELS desktop)

composia_test_target(composia-core-tests CoreTests.cpp)
add_test(NAME core COMMAND composia-core-tests)
set_tests_properties(core PROPERTIES TIMEOUT 10 LABELS core)

add_test(NAME package-consumer COMMAND ${CMAKE_COMMAND}
    -DBINARY_DIR=${PROJECT_BINARY_DIR} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DCONFIG=$<CONFIG> -DCOMPILER=${CMAKE_CXX_COMPILER}
    -DTOOLCHAIN=${CMAKE_TOOLCHAIN_FILE} -DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}
    -DTRIPLET=${VCPKG_TARGET_TRIPLET} -DRUNTIME=${CMAKE_MSVC_RUNTIME_LIBRARY}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/PackageTest.cmake)
set_tests_properties(package-consumer PROPERTIES TIMEOUT 120 LABELS core RUN_SERIAL TRUE)

add_test(NAME subproject-consumer COMMAND ${CMAKE_COMMAND}
    -DBINARY_DIR=${PROJECT_BINARY_DIR} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DCONFIG=$<CONFIG> -DCOMPILER=${CMAKE_CXX_COMPILER}
    -DTOOLCHAIN=${CMAKE_TOOLCHAIN_FILE} -DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}
    -DTRIPLET=${VCPKG_TARGET_TRIPLET} -P ${CMAKE_CURRENT_SOURCE_DIR}/SubprojectTest.cmake)
set_tests_properties(subproject-consumer PROPERTIES TIMEOUT 300 LABELS core RUN_SERIAL TRUE)
