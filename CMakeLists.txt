cmake_minimum_required(VERSION 3.25)
project(Composia VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR NOT MSVC)
    message(FATAL_ERROR "Composia requires Windows and clang-cl in an x64 Visual Studio developer shell.")
endif()

find_package(wil CONFIG REQUIRED)
find_package(cppwinrt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(directxtk CONFIG REQUIRED)

add_library(composia STATIC
    src/Application.cpp
    src/Window.cpp
    src/GraphicsDevice.cpp
    src/CompositionWindowTarget.cpp
    src/ScopedSurfaceDraw.cpp
    src/AnimationHelpers.cpp
)
add_library(Composia::UI ALIAS composia)
target_include_directories(composia PUBLIC include)
target_compile_features(composia PUBLIC cxx_std_23)
set_target_properties(composia PROPERTIES CXX_EXTENSIONS OFF)
target_compile_definitions(composia PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_compile_options(composia PUBLIC /EHsc /utf-8 PRIVATE /W4 /WX)
target_link_libraries(composia PUBLIC
    WIL::WIL Microsoft::CppWinRT spdlog::spdlog Microsoft::DirectXTK
    d3d11 dxgi d2d1 dwrite windowsapp CoreMessaging user32 gdi32 ole32
)

add_executable(composia-demo WIN32 demo/main.cpp demo/DemoWindow.cpp demo/SmokeTest.cpp demo/app.manifest)
target_link_libraries(composia-demo PRIVATE Composia::UI)
target_compile_options(composia-demo PRIVATE /W4 /WX)

include(CTest)
if(BUILD_TESTING)
    target_compile_definitions(composia PRIVATE COMPOSIA_TESTING)
    add_executable(composia-recovery-tests tests/RecoveryTests.cpp demo/app.manifest)
    target_compile_definitions(composia-recovery-tests PRIVATE COMPOSIA_TESTING)
    target_include_directories(composia-recovery-tests PRIVATE src)
    target_link_libraries(composia-recovery-tests PRIVATE Composia::UI)
    target_compile_options(composia-recovery-tests PRIVATE /W4 /WX)
    foreach(scenario transaction repeated-recovery failed-recovery shutdown callback-error)
        add_test(NAME ${scenario} COMMAND composia-recovery-tests ${scenario})
        set_tests_properties(${scenario} PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE)
    endforeach()
    add_executable(composia-window-tests tests/WindowTests.cpp demo/app.manifest)
    target_link_libraries(composia-window-tests PRIVATE Composia::UI)
    target_compile_options(composia-window-tests PRIVATE /W4 /WX)
    add_test(NAME secondary-close COMMAND composia-window-tests secondary-close)
    add_test(NAME secondary-error COMMAND composia-window-tests secondary-error)
    set_tests_properties(secondary-close secondary-error PROPERTIES TIMEOUT 15 RUN_SERIAL TRUE)
    add_test(NAME desktop-smoke COMMAND composia-demo --smoke-test)
    add_test(NAME warp-smoke COMMAND composia-demo --smoke-test --warp)
    set_tests_properties(desktop-smoke warp-smoke PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
endif()
