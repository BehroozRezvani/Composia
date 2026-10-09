cmake_minimum_required(VERSION 3.25)
project(Composia VERSION 0.3.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR NOT MSVC)
    message(FATAL_ERROR "Composia requires Windows and clang-cl in an x64 Visual Studio developer shell.")
endif()

# Static CRT: executables depend only on Windows system DLLs. Consumers must match this setting.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
option(COMPOSIA_BUILD_DEMO "Build the interactive demo" ${PROJECT_IS_TOP_LEVEL})
option(COMPOSIA_BUILD_TESTS "Build Composia's tests" ${PROJECT_IS_TOP_LEVEL})
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(wil CONFIG REQUIRED)
find_package(cppwinrt CONFIG REQUIRED)

add_library(composia STATIC
    src/Application.cpp
    src/Log.cpp
    src/Window.cpp
    src/GraphicsDevice.cpp
    src/CompositionWindowTarget.cpp
    src/ScopedSurfaceDraw.cpp
    src/VirtualSurface.cpp
    src/AnimationHelpers.cpp
    src/TextLayout.cpp
    src/Layout.cpp
    src/Button.cpp
    src/Accessible.cpp
    src/NativeControl.cpp
    src/TextureSurface.cpp
    src/SwapChainSurface.cpp
    src/ScreenCapture.cpp
)
set_source_files_properties(src/TextureSurface.cpp PROPERTIES COMPILE_DEFINITIONS NTDDI_VERSION=NTDDI_WIN10_NI)
add_library(Composia::UI ALIAS composia)
target_include_directories(composia PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(composia PUBLIC cxx_std_23)
set_target_properties(composia PROPERTIES CXX_EXTENSIONS OFF EXPORT_NAME UI)
target_compile_definitions(composia PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_compile_options(composia PUBLIC /EHsc /utf-8 PRIVATE /W4 /WX)
target_link_libraries(composia
    PUBLIC WIL::WIL Microsoft::CppWinRT
    PRIVATE d3d11 dxgi d2d1 dwrite windowsapp CoreMessaging user32 gdi32 ole32 oleaut32 uiautomationcore comctl32 uxtheme
)

if(COMPOSIA_BUILD_DEMO OR COMPOSIA_BUILD_TESTS)
    add_library(composia-virtual-sample STATIC demo/virtual/VirtualDemoWindow.cpp)
    target_link_libraries(composia-virtual-sample PUBLIC Composia::UI)
    target_include_directories(composia-virtual-sample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/demo/virtual)
    target_compile_options(composia-virtual-sample PRIVATE /W4 /WX)
    add_library(composia-media-sample STATIC
        demo/media/GpuScene.cpp demo/media/TextureFrames.cpp demo/media/VideoSource.cpp demo/media/MediaDemoWindow.cpp)
    target_link_libraries(composia-media-sample PUBLIC Composia::UI PRIVATE d3dcompiler shlwapi shell32 comdlg32)
    target_include_directories(composia-media-sample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/demo/media)
    target_compile_options(composia-media-sample PRIVATE /W4 /WX)
    add_library(composia-mail-sample STATIC
        demo/mail/MailModel.cpp demo/mail/MailSample.cpp demo/mail/TextField.cpp demo/mail/MailDemoWindow.cpp)
    target_link_libraries(composia-mail-sample PUBLIC Composia::UI)
    target_include_directories(composia-mail-sample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/demo/mail)
    target_compile_options(composia-mail-sample PRIVATE /W4 /WX)
endif()
if(COMPOSIA_BUILD_DEMO)
    add_executable(composia-virtual-demo WIN32 demo/virtual/main.cpp demo/app.manifest)
    target_link_libraries(composia-virtual-demo PRIVATE composia-virtual-sample)
    target_compile_options(composia-virtual-demo PRIVATE /W4 /WX)
    add_executable(composia-demo WIN32 demo/main.cpp demo/DemoWindow.cpp demo/app.manifest)
    target_link_libraries(composia-demo PRIVATE Composia::UI)
    target_compile_options(composia-demo PRIVATE /W4 /WX)
    add_executable(composia-media-demo WIN32 demo/media/main.cpp demo/app.manifest)
    target_link_libraries(composia-media-demo PRIVATE composia-media-sample shell32)
    target_compile_options(composia-media-demo PRIVATE /W4 /WX)
    add_executable(composia-mail-demo WIN32 demo/mail/main.cpp demo/app.manifest)
    target_link_libraries(composia-mail-demo PRIVATE composia-mail-sample)
    target_compile_options(composia-mail-demo PRIVATE /W4 /WX)
    # A standalone example that uses only the public headers and Composia::UI.
    add_executable(composia-inputs-example WIN32 examples/inputs/main.cpp demo/app.manifest)
    target_link_libraries(composia-inputs-example PRIVATE Composia::UI)
    target_compile_options(composia-inputs-example PRIVATE /W4 /WX)
    # Direct3D 11 content in a swap chain surface, presented only when it changes.
    add_executable(composia-gpu-example WIN32 examples/gpu/main.cpp demo/app.manifest)
    target_link_libraries(composia-gpu-example PRIVATE Composia::UI d3dcompiler)
    target_compile_options(composia-gpu-example PRIVATE /W4 /WX)
endif()

install(TARGETS composia EXPORT ComposiaTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/composia DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/Composia)
install(EXPORT ComposiaTargets NAMESPACE Composia:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Composia)
configure_package_config_file(cmake/ComposiaConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/ComposiaConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Composia)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/ComposiaConfigVersion.cmake
    COMPATIBILITY SameMinorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ComposiaConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/ComposiaConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Composia)

if(COMPOSIA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
