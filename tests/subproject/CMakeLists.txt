cmake_minimum_required(VERSION 3.25)
project(ComposiaSubproject LANGUAGES CXX)
# Composia inside another build, as add_subdirectory or FetchContent adds it: it uses this build's
# runtime library (CMake's default, the DLL CRT) and defines no tests, examples, or install rules.
add_subdirectory(${COMPOSIA_SOURCE_DIR} composia)
foreach(target composia-testing composia-smoke-tests composia-inputs-example)
    if(TARGET ${target})
        message(FATAL_ERROR "Composia defined ${target} inside another build")
    endif()
endforeach()
add_executable(subproject ${COMPOSIA_SOURCE_DIR}/tests/consumer/main.cpp ${COMPOSIA_MANIFEST})
target_link_libraries(subproject PRIVATE Composia::UI)
target_compile_definitions(subproject PRIVATE COMPOSIA_PACKAGE_VERSION=COMPOSIA_VERSION_STRING)
