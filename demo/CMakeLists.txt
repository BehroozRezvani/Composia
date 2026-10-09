# The demos: larger programs built on Composia's public API. They live on the demo branch, outside
# the library's release.
function(composia_demo_options target)
    target_compile_options(${target} PRIVATE /W4 $<$<BOOL:${COMPOSIA_WARNINGS_AS_ERRORS}>:/WX>)
endfunction()

# The demos' code, as libraries the programs and the tests share.
add_library(composia-virtual-sample STATIC virtual/VirtualDemoWindow.cpp)
target_link_libraries(composia-virtual-sample PUBLIC Composia::UI)
target_include_directories(composia-virtual-sample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/virtual)
composia_demo_options(composia-virtual-sample)
add_library(composia-media-sample STATIC media/GpuScene.cpp media/TextureFrames.cpp media/VideoSource.cpp media/MediaDemoWindow.cpp)
target_link_libraries(composia-media-sample PUBLIC Composia::UI PRIVATE d3dcompiler shlwapi shell32 comdlg32)
target_include_directories(composia-media-sample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/media)
composia_demo_options(composia-media-sample)
add_library(composia-mail-sample STATIC mail/MailModel.cpp mail/MailSample.cpp mail/TextField.cpp mail/MailDemoWindow.cpp)
target_link_libraries(composia-mail-sample PUBLIC Composia::UI)
target_include_directories(composia-mail-sample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/mail)
composia_demo_options(composia-mail-sample)
# The UI builder reuses the mail demo's editable TextField.
add_library(composia-builder-sample STATIC
    builder/BuilderModel.cpp builder/FormPainter.cpp builder/FormView.cpp builder/FormPlayerWindow.cpp
    builder/AppPackager.cpp builder/BuilderDemoWindow.cpp mail/TextField.cpp)
target_link_libraries(composia-builder-sample PUBLIC Composia::UI PRIVATE comdlg32 shell32)
target_include_directories(composia-builder-sample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/builder ${CMAKE_CURRENT_SOURCE_DIR}/mail)
composia_demo_options(composia-builder-sample)

# The programs.
add_executable(composia-demo WIN32 main.cpp DemoWindow.cpp ${COMPOSIA_MANIFEST})
target_link_libraries(composia-demo PRIVATE Composia::UI)
composia_demo_options(composia-demo)
add_executable(composia-virtual-demo WIN32 virtual/main.cpp ${COMPOSIA_MANIFEST})
target_link_libraries(composia-virtual-demo PRIVATE composia-virtual-sample)
composia_demo_options(composia-virtual-demo)
add_executable(composia-media-demo WIN32 media/main.cpp ${COMPOSIA_MANIFEST})
target_link_libraries(composia-media-demo PRIVATE composia-media-sample shell32)
composia_demo_options(composia-media-demo)
add_executable(composia-mail-demo WIN32 mail/main.cpp ${COMPOSIA_MANIFEST})
target_link_libraries(composia-mail-demo PRIVATE composia-mail-sample)
composia_demo_options(composia-mail-demo)
# The form player is the program the UI builder stamps a design into; the tests run it too.
add_executable(composia-form-player WIN32 builder/player_main.cpp ${COMPOSIA_MANIFEST})
target_link_libraries(composia-form-player PRIVATE composia-builder-sample shell32)
composia_demo_options(composia-form-player)
# The builder embeds the player as a resource, so "Build app" needs nothing beside the executable.
enable_language(RC)
set(embeddedPlayer ${CMAKE_CURRENT_BINARY_DIR}/embedded/composia-form-player.exe)
add_custom_command(OUTPUT ${embeddedPlayer}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:composia-form-player> ${embeddedPlayer}
    DEPENDS composia-form-player COMMENT "Embedding the form player" VERBATIM)
file(TO_NATIVE_PATH "${embeddedPlayer}" embeddedPlayerNative)
string(REPLACE "\\" "\\\\" embeddedPlayerEscaped "${embeddedPlayerNative}")
file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/embedded/player.rc CONTENT "COMPOSIA_PLAYER RCDATA \"${embeddedPlayerEscaped}\"\n")
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/embedded/player.rc PROPERTIES OBJECT_DEPENDS ${embeddedPlayer} GENERATED TRUE)
add_executable(composia-builder-demo WIN32 builder/main.cpp ${COMPOSIA_MANIFEST} ${CMAKE_CURRENT_BINARY_DIR}/embedded/player.rc)
target_link_libraries(composia-builder-demo PRIVATE composia-builder-sample)
composia_demo_options(composia-builder-demo)

if(COMPOSIA_BUILD_TESTS)
    # The tests take the same scenario and --warp arguments as the library's tests.
    function(composia_demo_test target source library)
        composia_test_target(${target} tests/${source})
        target_link_libraries(${target} PRIVATE ${library})
        target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    endfunction()

    composia_demo_test(composia-demo-smoke-tests DemoSmokeTests.cpp Composia::UI)
    target_sources(composia-demo-smoke-tests PRIVATE tests/SmokeTest.cpp DemoWindow.cpp)
    target_include_directories(composia-demo-smoke-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME demo-smoke COMMAND composia-demo-smoke-tests)
    add_test(NAME demo-smoke-warp COMMAND composia-demo-smoke-tests --warp)
    set_tests_properties(demo-smoke demo-smoke-warp PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE LABELS desktop)

    composia_demo_test(composia-virtual-demo-tests VirtualDemoTests.cpp composia-virtual-sample)
    composia_add_scenarios(composia-virtual-demo-tests virtual-demo WARP TIMEOUT 40 SCENARIOS navigation)

    composia_demo_test(composia-mail-tests MailTests.cpp composia-mail-sample)
    add_test(NAME mail-model COMMAND composia-mail-tests model)
    set_tests_properties(mail-model PROPERTIES TIMEOUT 10 LABELS core)
    add_test(NAME mail-client COMMAND composia-mail-tests client)
    add_test(NAME mail-client-warp COMMAND composia-mail-tests client --warp)
    set_tests_properties(mail-client mail-client-warp PROPERTIES TIMEOUT 40 RUN_SERIAL TRUE LABELS desktop)

    composia_demo_test(composia-media-tests MediaTests.cpp composia-media-sample)
    set(video ${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/color-bars.mp4)
    add_test(NAME media-gpu COMMAND composia-media-tests --hardware)
    add_test(NAME media-gpu-warp COMMAND composia-media-tests --warp)
    add_test(NAME media-video COMMAND composia-media-tests --hardware ${video})
    add_test(NAME media-video-warp COMMAND composia-media-tests --warp ${video})
    set_tests_properties(media-gpu media-gpu-warp media-video media-video-warp PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE LABELS desktop SKIP_RETURN_CODE 77)

    composia_demo_test(composia-media-capture-tests MediaCaptureTests.cpp composia-media-sample)
    add_test(NAME media-capture-window COMMAND composia-media-capture-tests --hardware)
    add_test(NAME media-capture-window-warp COMMAND composia-media-capture-tests --warp)
    add_test(NAME media-capture-monitor COMMAND composia-media-capture-tests --hardware --monitor)
    add_test(NAME media-capture-monitor-warp COMMAND composia-media-capture-tests --warp --monitor)
    set_tests_properties(media-capture-window media-capture-window-warp media-capture-monitor media-capture-monitor-warp
        PROPERTIES TIMEOUT 40 RUN_SERIAL TRUE LABELS desktop SKIP_RETURN_CODE 77)

    composia_demo_test(composia-builder-tests BuilderTests.cpp composia-builder-sample)
    add_test(NAME builder-model COMMAND composia-builder-tests model)
    set_tests_properties(builder-model PROPERTIES TIMEOUT 10 LABELS core)
    add_test(NAME builder-editor COMMAND composia-builder-tests editor)
    add_test(NAME builder-editor-warp COMMAND composia-builder-tests editor --warp)
    add_test(NAME builder-app COMMAND composia-builder-tests app $<TARGET_FILE:composia-form-player>)
    add_test(NAME builder-app-warp COMMAND composia-builder-tests app $<TARGET_FILE:composia-form-player> --warp)
    set_tests_properties(builder-editor builder-editor-warp builder-app builder-app-warp PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE LABELS desktop)
endif()
