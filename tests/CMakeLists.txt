function(composia_test_target target source)
    add_executable(${target} ${source} ${PROJECT_SOURCE_DIR}/demo/app.manifest)
    target_link_libraries(${target} PRIVATE Composia::UI)
    target_compile_options(${target} PRIVATE /W4 /WX)
endfunction()

target_compile_definitions(composia PRIVATE COMPOSIA_TESTING)
composia_test_target(composia-dpi-tests DpiTests.cpp)
add_test(NAME monitor-dpi COMMAND composia-dpi-tests)
set_tests_properties(monitor-dpi PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE LABELS desktop)
composia_test_target(composia-interaction-tests InteractionTests.cpp)
foreach(scenario input accessibility)
    add_test(NAME ${scenario} COMMAND composia-interaction-tests ${scenario})
    set_tests_properties(${scenario} PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE LABELS desktop)
endforeach()
composia_test_target(composia-recovery-tests RecoveryTests.cpp)
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

composia_test_target(composia-desktop-tests DesktopTests.cpp)
target_sources(composia-desktop-tests PRIVATE SmokeTest.cpp ${PROJECT_SOURCE_DIR}/demo/DemoWindow.cpp)
target_include_directories(composia-desktop-tests PRIVATE ${PROJECT_SOURCE_DIR}/demo)
target_link_libraries(composia-desktop-tests PRIVATE spdlog::spdlog Microsoft::DirectXTK)
add_test(NAME desktop-smoke COMMAND composia-desktop-tests)
add_test(NAME warp-smoke COMMAND composia-desktop-tests --warp)
set_tests_properties(desktop-smoke warp-smoke PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE LABELS desktop)

composia_test_target(composia-core-tests CoreTests.cpp)
add_test(NAME core COMMAND composia-core-tests)
set_tests_properties(core PROPERTIES TIMEOUT 10 LABELS core)

add_test(NAME package-consumer COMMAND ${CMAKE_COMMAND}
    -DBINARY_DIR=${PROJECT_BINARY_DIR} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DCONFIG=$<CONFIG> -DCOMPILER=${CMAKE_CXX_COMPILER}
    -DTOOLCHAIN=${CMAKE_TOOLCHAIN_FILE} -DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}
    -DTRIPLET=${VCPKG_TARGET_TRIPLET} -P ${CMAKE_CURRENT_SOURCE_DIR}/PackageTest.cmake)
set_tests_properties(package-consumer PROPERTIES TIMEOUT 120 LABELS core RUN_SERIAL TRUE)
