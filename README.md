# Composia

A small C++23 library for native Windows applications built on Windows Composition:
windowing, Direct2D and DirectWrite drawing into Composition surfaces, Direct3D 11 interop,
and platform integration (input state, focus, accessibility, native controls), without
prescribing an application architecture.

- Each window hosts a Composition visual tree; animations run in the system compositor,
  with no render loop.
- Content drawn inside one window needs no HWND per element.
- Every helper exposes its HWND, COM interfaces, or C++/WinRT objects.
- A static library whose only dependencies are the header-only WIL and C++/WinRT.
- Recovers from graphics device loss, including while idle.

Composia is not a UI toolkit: it has one `Button`, a stack layout helper, and no data
binding, styling, or designer.

## Requirements

- Windows 10 version 1903 or later, x64 (tested on Windows 11). `TextureSurface` needs a
  recent Windows 11.
- Visual Studio C++ build tools (tested with Visual Studio 2026) and Windows SDK
  10.0.26100.0 or later
- clang-cl with C++23 support (tested with clang 21), CMake 3.25 or later, Ninja, Git

## Example

```cpp
#include <composia/Application.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <composia/ScopedSurfaceDraw.hpp>

// A window whose client area is drawn with Direct2D into a Composition surface.
class HelloWindow final : public composia::Window {
public:
    explicit HelloWindow(composia::Application& app)
        : Window(app, L"Hello, Composia", 480, 240), target_(app.compositor(), app.graphics(), hwnd()) {}

private:
    void on_resize() override { invalidate(); }
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

From an installed package:

```powershell
cmake --install out/build/release --prefix C:/libs/composia
```

```cmake
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
    GIT_TAG main
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
| `CompositionWindowTarget.hpp` | A window's visual tree and its drawing surface |
| `ScopedSurfaceDraw.hpp` | Direct2D drawing into a Composition surface |
| `GraphicsDevice.hpp` | The Direct3D 11, Direct2D, DirectWrite, and Composition devices |
| `SwapChainSurface.hpp` | Direct3D 11 frames in the visual tree |
| `TextureSurface.hpp` | A Direct3D 11 texture in the visual tree, without a copy (Windows 11) |
| `VirtualSurface.hpp` | A sparse surface up to 2²⁴ pixels square, drawn in tiles |
| `ScreenCapture.hpp` | Windows Graphics Capture of a window, monitor, or visual |
| `TextLayout.hpp` | DirectWrite text layout and measurement |
| `NativeControl.hpp` | Standard Win32 controls hosted in a window |
| `Accessible.hpp` | A UI Automation element for a window |
| `Button.hpp` | An accessible push button whose look a painter supplies |
| `AnimationHelpers.hpp` | Visual and animation shortcuts |
| `Layout.hpp` | DIP geometry and stack placement |
| `Signal.hpp` | Signals and connections |
| `Log.hpp` | A handler for Composia's diagnostic events |
| `Version.hpp` | The library version |

## Documentation

- [Guide](docs/guide.md): how the library works and how to use each part
- [Testing](docs/testing.md): the tests, coverage, and what is not verified
- [Changelog](CHANGELOG.md): changes and upgrade steps. Before 1.0, a minor version may
  change the API.

## License

[MIT](LICENSE)
