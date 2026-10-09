# Each test executable takes a scenario name and, optionally, --warp for the WARP software device.
# Tests labeled core need no desktop; tests labeled desktop open windows, so they need an
# interactive session and run one at a time. A scenario this machine cannot run exits with 77,
# which CTest reports as skipped.

# A test executable linking Composia::UI, or the library named by a third argument. A coverage
# build instruments the tests too, for the inline functions in the public headers, and links them
# all to one library variant, so each library function has one set of counters.
function(composia_test_target target source)
    set(library Composia::UI)
    if(ARGC GREATER 2 OR COMPOSIA_COVERAGE)
        set(library composia-testing)
    endif()
    add_executable(${target} ${source} ${COMPOSIA_MANIFEST})
    target_link_libraries(${target} PRIVATE ${library})
    target_compile_options(${target} PRIVATE /W4 $<$<BOOL:${COMPOSIA_WARNINGS_AS_ERRORS}>:/WX>)
    if(COMPOSIA_COVERAGE)
        target_compile_options(${target} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
    endif()
endfunction()

# Registers a test named <area>-<scenario> for each scenario of a test executable, and with WARP
# an <area>-<scenario>-warp variant too. CORE labels them core instead of desktop.
function(composia_add_scenarios target area)
    cmake_parse_arguments(PARSE_ARGV 2 arg "WARP;CORE" "TIMEOUT" "SCENARIOS")
    if(NOT arg_TIMEOUT)
        set(arg_TIMEOUT 30)
    endif()
    foreach(scenario IN LISTS arg_SCENARIOS)
        set(names ${area}-${scenario})
        add_test(NAME ${area}-${scenario} COMMAND ${target} ${scenario})
        if(arg_WARP)
            add_test(NAME ${area}-${scenario}-warp COMMAND ${target} ${scenario} --warp)
            list(APPEND names ${area}-${scenario}-warp)
        endif()
        if(arg_CORE)
            set_tests_properties(${names} PROPERTIES TIMEOUT ${arg_TIMEOUT} LABELS core)
        else()
            set_tests_properties(${names} PROPERTIES TIMEOUT ${arg_TIMEOUT} LABELS desktop RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
        endif()
    endforeach()
endfunction()

# Every public header compiles on its own and tolerates a second inclusion; the build fails if not.
file(GLOB publicHeaders CONFIGURE_DEPENDS RELATIVE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/include/composia/*.hpp)
list(APPEND publicHeaders composia/Version.hpp)
set(headerSources)
foreach(header IN LISTS publicHeaders)
    get_filename_component(name ${header} NAME_WE)
    set(source ${CMAKE_CURRENT_BINARY_DIR}/headers/${name}.cpp)
    file(CONFIGURE OUTPUT ${source} CONTENT "#include <${header}>\n#include <${header}>\n")
    list(APPEND headerSources ${source})
endforeach()
add_library(composia-header-check OBJECT ${headerSources})
target_link_libraries(composia-header-check PRIVATE Composia::UI)
target_compile_options(composia-header-check PRIVATE /W4 $<$<BOOL:${COMPOSIA_WARNINGS_AS_ERRORS}>:/WX>)

# The library with failure injection compiled in, for the application checks only.
composia_add_library(composia-testing)
target_compile_definitions(composia-testing PRIVATE COMPOSIA_TESTING)

# Signals, layout, and text measurement: no window or graphics device.
composia_test_target(composia-core-tests CoreTests.cpp)
composia_add_scenarios(composia-core-tests core CORE TIMEOUT 10 SCENARIOS signal layout text)

# The Application and its graphics device: replacement, WARP fallback, shutdown, and threading.
composia_test_target(composia-application-tests ApplicationTests.cpp composia-testing)
target_compile_definitions(composia-application-tests PRIVATE COMPOSIA_TESTING)
target_include_directories(composia-application-tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
composia_add_scenarios(composia-application-tests application TIMEOUT 20
    SCENARIOS transaction repeated failed sequential hardware-fallback shutdown callback-error wrong-thread)

# Window and message loop lifetime.
composia_test_target(composia-window-tests WindowTests.cpp)
composia_add_scenarios(composia-window-tests window TIMEOUT 15
    SCENARIOS closed closed-parent secondary-close secondary-error quit-code callback-rethrow)

# An application window through its whole life in the message loop.
composia_test_target(composia-smoke-tests SmokeTests.cpp)
composia_add_scenarios(composia-smoke-tests smoke WARP SCENARIOS lifecycle)

# The mechanisms Window provides: pointer, focus, enabled state, rendering, accessibility, controls.
composia_test_target(composia-foundation-tests FoundationTests.cpp)
composia_add_scenarios(composia-foundation-tests foundation WARP
    SCENARIOS pointer focus render partial swapchain accessible native)

# Button input as Windows delivers it, and a UI Automation client in another apartment.
composia_test_target(composia-interaction-tests InteractionTests.cpp)
composia_add_scenarios(composia-interaction-tests interaction SCENARIOS input accessibility)

# Per-monitor DPI.
composia_test_target(composia-dpi-tests DpiTests.cpp)
composia_add_scenarios(composia-dpi-tests dpi SCENARIOS monitors messages)

# Drawing into a window's composition surface.
composia_test_target(composia-surface-tests SurfaceTests.cpp)
composia_add_scenarios(composia-surface-tests surface WARP SCENARIOS pixels)

# The animation helpers, through composited pixels.
composia_test_target(composia-animation-tests AnimationTests.cpp)
composia_add_scenarios(composia-animation-tests animation WARP SCENARIOS helpers)

# Sparse virtual surfaces.
composia_test_target(composia-virtual-tests VirtualSurfaceTests.cpp)
composia_add_scenarios(composia-virtual-tests virtual WARP TIMEOUT 40 SCENARIOS sparse-cache partial-updates)
composia_add_scenarios(composia-virtual-tests virtual TIMEOUT 20 SCENARIOS arguments)

# Direct3D textures in the visual tree, where the system supports composition textures.
composia_test_target(composia-texture-tests TextureTests.cpp)
composia_add_scenarios(composia-texture-tests texture WARP SCENARIOS surface)

# Windows Graphics Capture of a window and of a monitor.
composia_test_target(composia-capture-tests CaptureTests.cpp)
composia_add_scenarios(composia-capture-tests capture WARP TIMEOUT 40 SCENARIOS window monitor)

# The installed package, relocated, consumed through find_package.
add_test(NAME package-consumer COMMAND ${CMAKE_COMMAND}
    -DBINARY_DIR=${PROJECT_BINARY_DIR} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DCONFIG=$<CONFIG> -DCOMPILER=${CMAKE_CXX_COMPILER}
    -DTOOLCHAIN=${CMAKE_TOOLCHAIN_FILE} -DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}
    -DTRIPLET=${VCPKG_TARGET_TRIPLET} -DRUNTIME=${CMAKE_MSVC_RUNTIME_LIBRARY}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/PackageTest.cmake)
set_tests_properties(package-consumer PROPERTIES TIMEOUT 120 LABELS core RUN_SERIAL TRUE)

# The source tree, added to another build with add_subdirectory.
add_test(NAME subproject-consumer COMMAND ${CMAKE_COMMAND}
    -DBINARY_DIR=${PROJECT_BINARY_DIR} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DCONFIG=$<CONFIG> -DCOMPILER=${CMAKE_CXX_COMPILER}
    -DTOOLCHAIN=${CMAKE_TOOLCHAIN_FILE} -DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}
    -DTRIPLET=${VCPKG_TARGET_TRIPLET} -P ${CMAKE_CURRENT_SOURCE_DIR}/SubprojectTest.cmake)
set_tests_properties(subproject-consumer PROPERTIES TIMEOUT 300 LABELS core RUN_SERIAL TRUE)
