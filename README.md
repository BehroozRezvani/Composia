# Composia

A small C++23 library for native Windows applications built on Windows Composition:
windowing, Direct2D and DirectWrite drawing into Composition surfaces, Direct3D 11 interop,
and platform integration (input state, focus, accessibility, native controls), without
prescribing an application architecture.

- Each window hosts a Composition visual tree; animations run in the system compositor,
  with no render loop.
- A retained element tree lays out, draws, and routes input for custom content on one surface.
- Every helper exposes its HWND, COM interfaces, or C++/WinRT objects.
- A static library whose only dependencies are the header-only WIL and C++/WinRT.
- Recovers from graphics device loss, including while idle.

Use stack, flex, grid, and scroll containers for custom-drawn elements. A retained
`TextBox` paints editable Unicode text on the same surface and exposes native text ranges
to screen readers. Standard Win32
controls and Composia's accessible `Button` can share the same application.

Applications that draw their own controls can link `Composia::Controls` for button,
toggle, slider, textbox, scroll view, dropdown, menu and virtual list behaviours.
The playback example supplies its own painters for both the dropdown and its popup.
[Controls guide](docs/guide.md#lookless-controls) · [Playback example](examples/controls/Playback.hpp)

For resizable panes, use [shared composition targets and coordinated updates](docs/resizing.md).
They keep pane placement, clipping and painting together; native EDIT controls can opt into
buffered painting while panes move around them.

## Requirements

- Windows 10 version 1903 or later, x64 (tested on Windows 11). `TextureSurface` needs a
  recent Windows 11.
- Visual Studio C++ build tools (tested with Visual Studio 2026) and Windows SDK
  10.0.26100.0 or later
- clang-cl with C++23 support (tested with clang 21), CMake 3.25 or later, Ninja, Git

## Example

The [Composia Editor](examples/editor/README.md) opens, edits and saves UTF-8 files,
preserves line endings, and prompts before discarding edits. The
[drawing comparison](benchmarks/README.md) reports measured startup, memory,
executable size and render timings for Composia, Direct2D and GDI.

```cpp
#include <composia/Application.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <composia/ScopedSurfaceDraw.hpp>

// A window whose client area is drawn with Direct2D into a Composition surface.
class HelloWindow final : public composia::Window {
public:
    explicit HelloWindow(composia::Application& app)
        : Window(app, L"Hello, Composia", 480, 240, Content::composition),
          target_(*this, composia::CompositionWindowTarget::Hosting::shared) {}

private:
    void on_resize() override { application().update(*this, [] {}); }
    void on_paint() override {
        target_.render(*this, [](composia::ScopedSurfaceDraw& draw, composia::numerics::float2 size) {
            draw.context()->Clear(D2D1::ColorF(0x101923));
            draw.context()->FillRectangle({24, 24, size.x - 24, 72}, draw.solid_brush(0x6FE6C8));
        });
    }

    composia::CompositionWindowTarget target_;
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int showCommand) {
    composia::Application app;
    {
        HelloWindow window{app};
        window.show(showCommand);
        app.run();
    }
    app.close();
    return 0;
}
```

[`examples/hello`](examples/hello/main.cpp) adds text, a `Button`, error handling, and the
system's light, dark, or high contrast appearance;
[`examples/inputs`](examples/inputs/main.cpp) shows native controls and accessibility, and
[`examples/gpu`](examples/gpu/main.cpp) Direct3D content. Larger demos are on the `demo`
branch.

## Building

```powershell
git clone --recurse-submodules https://github.com/BehroozRezvani/Composia.git
cd Composia
.\scripts\build.ps1 -Preset release -Test
```

The script enters the Visual Studio developer environment, bootstraps the pinned vcpkg
submodule, which supplies WIL and C++/WinRT, and configures, builds, and tests a preset:
`debug`, `release`, or `coverage`. Output goes to `out\build\<preset>`, with the examples
in its `examples` folder.

## Using Composia

### With vcpkg

In an x64 Visual Studio developer shell, clone Composia into your application's
`vendor/Composia` directory. Pin the checkout to your chosen release. Add
`"composia"` to the dependencies in the application's vcpkg manifest, or request
`{ "name": "composia", "features": ["controls"] }` for the lookless controls.
Use the overlay from that checkout with your usual vcpkg toolchain:

```powershell
cmake -S . -B build -G "Ninja Multi-Config" -DCMAKE_CXX_COMPILER=clang-cl `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  "-DVCPKG_OVERLAY_PORTS=$PWD/vendor/Composia/ports" -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release
```

Set `CMAKE_MSVC_RUNTIME_LIBRARY` before defining application targets, as shown
below. The overlay installs both configurations and resolves WIL and C++/WinRT.
[Consumer CMake project](tests/consumer/CMakeLists.txt) · [Example manifest](tests/port/vcpkg.json)

### With CMake

From an installed package:

```powershell
cmake --install out/build/release --prefix C:/libs/composia
```

```cmake
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
find_package(Composia 0.3 CONFIG REQUIRED)
add_executable(my_app WIN32 main.cpp ${COMPOSIA_MANIFEST})
target_link_libraries(my_app PRIVATE Composia::UI)
```

The package is built with the static CRT, so the application must set
`CMAKE_MSVC_RUNTIME_LIBRARY` to `MultiThreaded$<$<CONFIG:Debug>:Debug>`. WIL and C++/WinRT
must be available to `find_package`, for example through vcpkg.

From source, with FetchContent or `add_subdirectory`, Composia uses the application's
runtime library and builds no tests or examples:

```cmake
include(FetchContent)
FetchContent_Declare(composia
    GIT_REPOSITORY https://github.com/BehroozRezvani/Composia.git
    GIT_TAG v0.3.0
    GIT_SUBMODULES "")  # Composia's vcpkg submodule is not needed here
FetchContent_MakeAvailable(composia)
```

`COMPOSIA_MANIFEST` is the application manifest Composia expects: per-monitor DPI
awareness (PerMonitorV2) and common controls version 6. Embed it, or put the same
declarations in your own manifest.

`COMPOSIA_BUILD_EXAMPLES`, `COMPOSIA_BUILD_TESTS`, `COMPOSIA_INSTALL`, and
`COMPOSIA_WARNINGS_AS_ERRORS` are on only when Composia is the top-level project.

## Headers

| Header | Provides |
| --- | --- |
| `Application.hpp` | The UI thread's message loop, compositor, graphics device recovery, and system appearance |
| `Appearance.hpp` | Dark mode, high contrast, accent color, text scale, and system colors |
| `Window.hpp` | An HWND with input, focus, enabled state, invalidation, and DPI tracking |
| `CompositionWindowTarget.hpp` | Standalone or shared pane visuals, clipping, backgrounds, and drawing surfaces |
| `ScopedSurfaceDraw.hpp` | Direct2D drawing into a Composition surface |
| `GraphicsDevice.hpp` | The Direct3D 11, Direct2D, DirectWrite, and Composition devices |
| `SwapChainSurface.hpp` | Direct3D 11 frames in the visual tree |
| `TextureSurface.hpp` | A Direct3D 11 texture in the visual tree, without a copy (Windows 11) |
| `VirtualSurface.hpp` | A sparse surface up to 2²⁴ pixels square, drawn in tiles |
| `ScreenCapture.hpp` | Windows Graphics Capture of a window, monitor, or visual |
| `TextLayout.hpp` | DirectWrite text layout and measurement |
| `TextBox.hpp` | Retained plain-text editing, IME, clipboard, undo, selection geometry and custom painting |
| `NativeControl.hpp` | Standard Win32 controls hosted in a window |
| `Accessible.hpp` | A UI Automation element for a window |
| `ElementAccessibility.hpp` | Nested UI Automation fragments, names, relationships and interaction patterns on a retained surface |
| `Button.hpp` | An accessible push button whose look a painter supplies |
| `AnimationHelpers.hpp` | Visual and animation shortcuts |
| `Layout.hpp` | DIP geometry and stack placement |
| `Element.hpp` | Retained elements, layout, clipped drawing, routed input, focus, and dirty regions |
| `ElementWindow.hpp` | A window or child pane that hosts and repaints an element tree |
| `HostedWindow.hpp` | A child window placed, clipped and tab-ordered as a retained element |
| `Signal.hpp` | Signals and connections |
| `Log.hpp` | A handler for Composia's diagnostic events |
| `Version.hpp` | The library version |

## Documentation

- [Releases and compatibility](docs/releases.md) covers version selection, SDK downloads and the public API policy.

- [Guide](docs/guide.md): how the library works and how to use each part
- [Testing](docs/testing.md): the tests, coverage, and what is not verified
- [Changelog](CHANGELOG.md): changes and upgrade steps. Before 1.0, a minor version may
  change the API.

## License

[MIT](LICENSE)
