# Standalone programs that use only the public headers and Composia::UI.
function(composia_example target source)
    add_executable(${target} WIN32 ${source} ${COMPOSIA_MANIFEST})
    target_link_libraries(${target} PRIVATE Composia::UI ${ARGN})
    target_compile_options(${target} PRIVATE /W4 $<$<BOOL:${COMPOSIA_WARNINGS_AS_ERRORS}>:/WX>)
endfunction()

# Text in a composition canvas, hosted EDIT controls and a check box, a Button, and an accessible slider.
composia_example(composia-inputs-example inputs/main.cpp)
# Direct3D 11 content in a swap chain surface, presented only when it changes.
composia_example(composia-gpu-example gpu/main.cpp d3dcompiler)
