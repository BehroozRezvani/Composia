# Composia

Composia is a small C++23 foundation for native Windows applications. It provides
windowing, Windows Composition, Direct2D and DirectWrite drawing, Direct3D 11 interop,
and reusable platform integration (pointer and keyboard state, focus, accessibility,
native controls) without prescribing an application architecture.

- **Composition first.** Each window hosts a Composition visual tree. Drawing goes into
  Composition surfaces, and animations run in the system compositor without a render
  loop or a UI-thread timer.
- **No HWND per element.** Content drawn inside one window shares its input, capture,
  and invalidation. Child windows are used where Windows needs one: keyboard focus,
  UI Automation elements, and hosted native controls.
- **Native access everywhere.** Every helper exposes its HWND, its `wil::com_ptr`
  interfaces, or its C++/WinRT objects, so nothing stops you from using the platform
  directly.
- **Small and self-contained.** A static library with two header-only dependencies, WIL
  and C++/WinRT. Executables built with it import only Windows system DLLs; the
  release examples are 330 to 380 KB each.
- **Recovers from device loss.** Graphics device removal is detected even while idle;
  the device is replaced transactionally, and every window repaints.

Composia is not a UI toolkit: it has one `Button`, a stack layout helper, and no data
binding, styling, or designer. It is meant for applications whose interface is mostly
their own drawing, and for building such toolkits.

## Requirements

- Windows 10 version 1903 or later, x64. Composition textures (`TextureSurface`) need a
  recent Windows 11 runtime.
- Visual Studio 2022 C++ build tools and Windows SDK 10.0.26100.0 or later
- clang-cl with C++23 support (tested with clang 21), CMake 3.25 or later, Ninja, Git

## Quick start

`examples/hello/main.cpp` is a complete program:

```cpp
#include <composia/Application.hpp>
#include <composia/Button.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <composia/TextLayout.hpp>
#include <algorithm>
#include <exception>

class HelloWindow final : public composia::Window {
public:
    explicit HelloWindow(composia::Application& app)
        : Window(app, L"Hello, Composia", 480, 240),
          target_(app.compositor(), app.graphics(), hwnd()),
          greeting_(app.graphics().text_factory().get(), L"Hello from Windows Composition", 24),
          close_(*this, L"Close") {
        clicked_ = close_.on_click([this] { PostMessageW(hwnd(), WM_CLOSE, 0, 0); });
        arrange();
    }

private:
    // Child windows are placed in DIPs; Windows does not move them when the window resizes.
    void arrange() {
        const auto bounds = client_bounds();
        close_.set_bounds({std::max(24.0f, bounds.width - 184), std::max(80.0f, bounds.height - 68), 160, 44});
    }
    void on_resize() override {
        arrange();
        invalidate();
    }
    // Paints the client area in DIPs; render sizes the surface and retries after device loss.
    void on_paint() override {
        target_.render(*this, [&](composia::ScopedSurfaceDraw& draw, composia::numerics::float2 size) {
            draw.context()->Clear(D2D1::ColorF(0x101923));
            greeting_.resize(std::max(1.0f, size.x - 48), 40);
            draw.context()->DrawTextLayout({24, 24}, greeting_.layout().get(), draw.solid_brush(0xEAF2F4));
        });
    }

    composia::CompositionWindowTarget target_;
    composia::TextLayout greeting_;
    composia::Button close_;
    composia::Connection clicked_;
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int showCommand) {
    try {
        composia::Application app;
        int result{};
        {
            HelloWindow window{app};
            window.show(showCommand);
            result = app.run();
        }
        app.close();
        return result;
    } catch (const std::exception&) {
    } catch (const winrt::hresult_error&) {
    }
    MessageBoxW(nullptr, L"Composia could not continue.", L"Hello, Composia", MB_OK | MB_ICONERROR);
    return 1;
}
```

## Building

From PowerShell, in a clone with its submodules:

```powershell
git clone --recurse-submodules https://github.com/BehroozRezvani/Composia.git
cd Composia
.\scripts\build.ps1 -Preset release -Test
.\out\build\release\examples\composia-hello-example.exe
```

The script loads the Visual Studio developer environment, bootstraps the pinned vcpkg
submodule, then configures, builds, and tests with the `release` preset (or `debug`, or
`coverage`). From an x64 Visual Studio developer shell, the presets also work directly:
`cmake --preset debug`, `cmake --build --preset debug`, `ctest --preset debug`.

vcpkg supplies WIL and the C++/WinRT projections, both header-only. `builtin-baseline` in
`vcpkg.json` pins their versions; the `external/vcpkg` submodule provides the tool and
must contain that commit.

## Using Composia in a project

### As an installed package

```powershell
cmake --install out/build/release --prefix C:/libs/composia
```

```cmake
find_package(Composia 0.3 CONFIG REQUIRED)
add_executable(my_app WIN32 main.cpp ${COMPOSIA_MANIFEST})
target_link_libraries(my_app PRIVATE Composia::UI)
```

Point `CMAKE_PREFIX_PATH` at the installation and use a toolchain that provides WIL
and C++/WinRT, such as vcpkg. A top-level build of Composia uses the static CRT, so the
application must too: set `CMAKE_MSVC_RUNTIME_LIBRARY` to
`MultiThreaded$<$<CONFIG:Debug>:Debug>`, or the linker reports a mismatch. Debug and
Release libraries can share one installation; the Debug one is `composiad.lib` and
carries its own debug information.

### From source

```cmake
include(FetchContent)
FetchContent_Declare(composia
    GIT_REPOSITORY https://github.com/BehroozRezvani/Composia.git
    GIT_TAG main          # or a release tag
    GIT_SUBMODULES "")    # the vcpkg submodule is only for Composia's own builds
FetchContent_MakeAvailable(composia)
add_executable(my_app WIN32 main.cpp ${COMPOSIA_MANIFEST})
target_link_libraries(my_app PRIVATE Composia::UI)
```

Added with `add_subdirectory` or FetchContent, Composia uses the parent project's runtime
library and builds no tests or examples. WIL and C++/WinRT must still be findable with
`find_package`.

### The application manifest

`COMPOSIA_MANIFEST` names `resources/composia.manifest`, which declares per-monitor DPI
awareness (PerMonitorV2) and common controls version 6. Composia's DIP layout assumes the
first; hosted native controls need the second for their themed look. Embed it, or put
the same declarations in your own manifest.

### Options

| Option | Default | Effect |
| --- | --- | --- |
| `COMPOSIA_BUILD_EXAMPLES` | On at top level | Builds `examples/` |
| `COMPOSIA_BUILD_TESTS` | On at top level | Builds `tests/` |
| `COMPOSIA_INSTALL` | On at top level | Generates the install rules and CMake package |
| `COMPOSIA_WARNINGS_AS_ERRORS` | On at top level | Builds Composia's own code with `/WX` |
| `COMPOSIA_COVERAGE` | Off | Instruments the library for clang source-based coverage |

`composia/Version.hpp` defines `COMPOSIA_VERSION_MAJOR`, `_MINOR`, `_PATCH`, `_STRING`,
and `COMPOSIA_VERSION`.

## What is in the library

| Header | Provides |
| --- | --- |
| `Application.hpp` | The UI thread's apartment, dispatcher queue, compositor, message loop, and graphics recovery |
| `Window.hpp` | An owning HWND with exception-safe dispatch: resize and DPI events, hover, capture, focus and its restoration, enabled state including ancestors, invalidation, signals, notification routing, and a UI Automation provider slot |
| `GraphicsDevice.hpp` | The Direct3D 11, Direct2D, and DirectWrite devices and the Composition graphics device, replaced on device loss |
| `CompositionWindowTarget.hpp` | A window's visual tree and canvas surface, sized in DIPs, painted in one call that updates only the invalidated area |
| `ScopedSurfaceDraw.hpp` | One balanced BeginDraw/EndDraw on a Composition surface, with DPI, an update clip, and scope-owned brushes |
| `VirtualSurface.hpp` | A sparse surface up to 16,777,216 pixels square, drawn in tiles for the current viewport |
| `SwapChainSurface.hpp` | Direct3D 11 frames in the visual tree on Windows 10 and later, presented only when they change |
| `TextureSurface.hpp` | A Direct3D 11 texture in the visual tree without a copy (Windows 11) |
| `ScreenCapture.hpp` | Windows Graphics Capture of a window, a monitor, or a visual |
| `TextLayout.hpp` | A reusable DirectWrite layout with measurement |
| `Accessible.hpp` | A UI Automation element for any window: name, role, focus and enabled state, Invoke and Value patterns |
| `NativeControl.hpp` | Standard Win32 controls (EDIT, BUTTON, COMBOBOX, ...) hosted with DIP bounds, DPI-aware fonts, colors, and notifications |
| `Button.hpp` | A composition-drawn, accessible push button, built only on the public API |
| `AnimationHelpers.hpp` | Container and sprite visuals, implicit offset animation, and centering |
| `Layout.hpp` | DIP points and rectangles, and stack placement |
| `Signal.hpp` | Signals and connections |
| `Log.hpp` | A handler for Composia's diagnostic events |

## Examples

| Example | Shows |
| --- | --- |
| `examples/hello` | The program above |
| `examples/inputs` | Text in a composition canvas, hosted EDIT controls and a check box with matching colors, a `Button`, and a drawn slider that is a tab stop with its own UI Automation element |
| `examples/gpu` | A Direct3D 11 triangle in a `SwapChainSurface` behind a rounded composition clip, presented only on input, while a compositor animation keeps running |

The `demo` branch holds larger demos: a texture and screen capture studio, a virtual
map, a mail client, and a UI builder.

## Documentation

- [Guide](docs/guide.md): how the library works and how to use each part
- [Testing](docs/testing.md): how Composia is tested, coverage, and what is not verified
- [Changelog](CHANGELOG.md)

## Versioning

Composia follows semantic versioning. Before 1.0, a minor version may change the API;
the changelog lists every such change. The CMake package accepts requests for the same
major and minor version.

## License

[MIT](LICENSE). WIL and C++/WinRT keep their own licenses.
