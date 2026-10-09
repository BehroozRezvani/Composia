cmake_minimum_required(VERSION 3.25)
project(Composia VERSION 0.3.0
    DESCRIPTION "A small C++23 foundation for native Windows applications on Windows Composition"
    HOMEPAGE_URL "https://github.com/BehroozRezvani/Composia"
    LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR NOT MSVC)
    message(FATAL_ERROR "Composia requires Windows and clang-cl in an x64 Visual Studio developer shell.")
endif()

option(COMPOSIA_BUILD_EXAMPLES "Build the examples" ${PROJECT_IS_TOP_LEVEL})
option(COMPOSIA_BUILD_TESTS "Build Composia's tests" ${PROJECT_IS_TOP_LEVEL})
option(COMPOSIA_INSTALL "Generate Composia's install rules" ${PROJECT_IS_TOP_LEVEL})
option(COMPOSIA_WARNINGS_AS_ERRORS "Treat warnings in Composia's own code as errors" ${PROJECT_IS_TOP_LEVEL})
option(COMPOSIA_COVERAGE "Instrument the library for clang source-based code coverage" OFF)

# A top-level build uses the static CRT, so its executables depend only on Windows system DLLs.
# Inside another project, Composia uses that project's runtime library.
if(PROJECT_IS_TOP_LEVEL AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# vcpkg.json carries the same version for the package manager; the two must not drift apart.
file(READ ${PROJECT_SOURCE_DIR}/vcpkg.json manifest)
string(JSON manifestVersion GET "${manifest}" version)
if(NOT manifestVersion STREQUAL PROJECT_VERSION)
    message(FATAL_ERROR "vcpkg.json declares version ${manifestVersion}, but CMakeLists.txt declares ${PROJECT_VERSION}.")
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(wil CONFIG REQUIRED)
find_package(cppwinrt CONFIG REQUIRED)

configure_file(cmake/Version.hpp.in ${PROJECT_BINARY_DIR}/include/composia/Version.hpp @ONLY)
# The manifest applications embed for per-monitor DPI awareness and themed common controls.
set(COMPOSIA_MANIFEST ${PROJECT_SOURCE_DIR}/resources/composia.manifest CACHE INTERNAL "Composia's application manifest")

set(COMPOSIA_SOURCES
    Accessible.cpp
    AnimationHelpers.cpp
    Application.cpp
    Button.cpp
    CompositionWindowTarget.cpp
    GraphicsDevice.cpp
    Layout.cpp
    Log.cpp
    NativeControl.cpp
    ScopedSurfaceDraw.cpp
    ScreenCapture.cpp
    SwapChainSurface.cpp
    TextLayout.cpp
    TextureSurface.cpp
    VirtualSurface.cpp
    Window.cpp
)
list(TRANSFORM COMPOSIA_SOURCES PREPEND ${PROJECT_SOURCE_DIR}/src/)

if(COMPOSIA_COVERAGE)
    # Programs that link an instrumented library need clang's profile runtime.
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} /clang:-print-resource-dir
        OUTPUT_VARIABLE resourceDir OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    set(COMPOSIA_PROFILE_RUNTIME ${resourceDir}/lib/windows/clang_rt.profile-x86_64.lib)
endif()

# Defines a Composia library target. The tests define a second one with failure injection
# compiled in, so the library that is installed never carries test hooks.
function(composia_add_library target)
    add_library(${target} STATIC ${COMPOSIA_SOURCES})
    set_source_files_properties(${PROJECT_SOURCE_DIR}/src/TextureSurface.cpp TARGET_DIRECTORY ${target}
        PROPERTIES COMPILE_DEFINITIONS NTDDI_VERSION=NTDDI_WIN10_NI)
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_compile_features(${target} PUBLIC cxx_std_23)
    # Debug information goes into the library itself: a static library has no PDB of its own to install.
    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
        DEBUG_POSTFIX d
        MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
    target_compile_definitions(${target} PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
    target_compile_options(${target} PUBLIC /EHsc /utf-8 PRIVATE /W4 $<$<BOOL:${COMPOSIA_WARNINGS_AS_ERRORS}>:/WX>)
    # C++/WinRT clears its factory cache with a 16-byte compare-exchange (CMPXCHG16B), which every
    # x64 processor running Windows 10 supports; clang emits it only with this feature enabled.
    target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:Clang>:/clang:-mcx16>)
    target_link_libraries(${target}
        PUBLIC WIL::WIL Microsoft::CppWinRT
        PRIVATE d3d11 dxgi d2d1 dwrite windowsapp CoreMessaging user32 gdi32 ole32 oleaut32 uiautomationcore comctl32 uxtheme
    )
    if(COMPOSIA_COVERAGE)
        target_compile_options(${target} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
        target_link_libraries(${target} PUBLIC ${COMPOSIA_PROFILE_RUNTIME})
    endif()
endfunction()

composia_add_library(composia)
set_target_properties(composia PROPERTIES EXPORT_NAME UI)
add_library(Composia::UI ALIAS composia)

if(COMPOSIA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(COMPOSIA_INSTALL)
    set(configDestination ${CMAKE_INSTALL_LIBDIR}/cmake/Composia)
    set(COMPOSIA_MANIFEST_INSTALL ${CMAKE_INSTALL_DATADIR}/Composia/composia.manifest)
    install(TARGETS composia EXPORT ComposiaTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(DIRECTORY include/composia ${PROJECT_BINARY_DIR}/include/composia DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES LICENSE resources/composia.manifest DESTINATION ${CMAKE_INSTALL_DATADIR}/Composia)
    install(EXPORT ComposiaTargets NAMESPACE Composia:: DESTINATION ${configDestination})
    configure_package_config_file(cmake/ComposiaConfig.cmake.in ${PROJECT_BINARY_DIR}/ComposiaConfig.cmake
        INSTALL_DESTINATION ${configDestination} PATH_VARS COMPOSIA_MANIFEST_INSTALL)
    write_basic_package_version_file(${PROJECT_BINARY_DIR}/ComposiaConfigVersion.cmake COMPATIBILITY SameMinorVersion)
    install(FILES ${PROJECT_BINARY_DIR}/ComposiaConfig.cmake ${PROJECT_BINARY_DIR}/ComposiaConfigVersion.cmake
        DESTINATION ${configDestination})
endif()

if(COMPOSIA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
