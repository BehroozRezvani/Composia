# Composia

A small C++23 UI framework for desktop HWNDs, using Windows Composition,
Direct2D, DirectWrite, and Direct3D 11. The demo renders a text/vector canvas with
an animated visual tree. No custom IDL, XAML, UWP application, or Windows App SDK
is needed.

## Build and run

Use Windows 10 version 1903 or newer, an x64 Visual Studio C++ toolchain with a
recent Windows SDK, clang-cl with C++23 support, CMake 3.25+, Ninja, and Git.
Put clang-cl, CMake, and Ninja on `PATH`. From PowerShell:

```powershell
.\scripts\build.ps1 -Preset release -Test
.\out\build\release\composia-demo.exe
```

The script loads the Visual Studio developer environment, initializes the pinned
vcpkg submodule, bootstraps vcpkg, and runs the presets. Alternatively, from an
x64 Visual Studio developer shell:

```powershell
git submodule update --init --recursive
.\external\vcpkg\bootstrap-vcpkg.bat -disableMetrics
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
.\out\build\debug\composia-demo.exe
```

vcpkg supplies WIL, generated C++/WinRT SDK projections, spdlog, and DirectX
Toolkit 11 (whose vcpkg package is `directxtk`). The application/framework compile
with clang-cl; vcpkg builds ABI-compatible static dependencies using the MSVC
toolchain and dynamic CRT (`x64-windows-static-md`). Release requires the x64
Visual C++ runtime. The baseline matches the submodule commit.

## Framework

Link `Composia::UI` from CMake; public headers are in `include/composia`.

The demo and tests are optional (`COMPOSIA_BUILD_DEMO` and
`COMPOSIA_BUILD_TESTS`). Both default off when included with `add_subdirectory`.
To install and consume the library:

```powershell
cmake --install out/build/release --prefix out/install
```

```cmake
find_package(Composia 0.2 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE Composia::UI)
```

Set `CMAKE_PREFIX_PATH` to the installation and configure the consumer with the
same vcpkg toolchain/triplet. Installed targets carry their dependencies.

| Module | Responsibility |
| --- | --- |
| `Application` | STA, dispatcher queue, compositor, message loop, and graphics recovery |
| `Window` | Owning HWND, exception-safe message dispatch, resize and per-monitor DPI events |
| `GraphicsDevice` | D3D11 device5, D2D device6/context6, DirectWrite factory7, and Composition graphics device |
| `CompositionWindowTarget` | Desktop HWND bridge, visual root, canvas surface/brush, and pixel/DIP sizing |
| `ScopedSurfaceDraw` | Balanced surface BeginDraw/EndDraw with DPI and atlas-offset translation |
| `AnimationHelpers` | Containers/sprites, implicit offset, vector/scalar keyframes, and expression layout |

`demo/main.cpp` only initializes logging and starts the application/window.
`demo/DemoWindow.cpp` demonstrates drawing and scene assembly. The moving tile
uses vector keyframes; the status dot uses scalar opacity and implicit offset
animations; an expression centers the tile's container. Composition runs these
animations independently of the UI message loop. The ordinary demo has no
rendering timer.

Create one `Application` on the UI thread and pass it to each `Window` constructor.
`Application::run()` serves all registered windows and exits when the last HWND
closes. Destroy window objects and Composition objects, then call `app.close()`
to observe shutdown errors. All framework
operations and surface updates belong on that thread. The demo embeds a
PerMonitorV2 manifest; consumers should embed the same DPI declaration. Layout
uses DIPs, surfaces use physical pixels, and the root applies the DPI scale.

Every owning helper exposes its native HWND, `wil::com_ptr` interfaces, or
C++/WinRT objects. `GraphicsDevice` also exposes an offscreen D2D context; a
Composition surface must be drawn using the context returned by its drawing
scope. Do not nest drawing scopes on the same graphics device or call the D2D
context's own BeginDraw/EndDraw. Call `finish()` to observe EndDraw failures;
the destructor balances an unfinished scope during exception unwinding. The
draw context is valid only until `finish()`/scope destruction.

`Application::post()` accepts work from other threads and propagates callback
errors through `run()`. Shutdown cancels pending application callbacks and drains
the raw dispatcher while graphics remain alive. Raw dispatcher callbacks are the
consumer's responsibility. The application must outlive its window objects and
any threads calling `post()`.

Device removal is registered with D3D11 and watched alongside the Win32 message
queue, including while idle. Recovery replaces the D3D/D2D devices through
`SetRenderingDevice`, preserves Composition visuals/surfaces, and invokes the
window invalidation. `Application::render` retries a drawing operation once after
device loss; persistent or unrelated errors propagate. Recreate consumer-owned
device-dependent caches in `Window::on_graphics_recreated()` or subscribe with
`GraphicsDevice::on_recreated()` and retain the returned `Connection`. Device
replacement prepares the device and removal subscription before changing
Composition; a preparation failure preserves the previous published state. Raw device
access does not transfer ownership or extend a drawing scope's validity.

## Validation

`ctest --preset debug` and `ctest --preset release` run the test suite, including
hardware-preferred and forced-WARP desktop checks. Desktop checks briefly show
windows and require a Windows desktop. CTest retains output in
`out/build/<preset>/Testing/Temporary/LastTest.log`; `build.ps1 -Test` also writes
JUnit results there. Normal runs write `composia.log` in the working directory.
Smoke-test code lives in separate test executables.

The Windows CI matrix builds Debug/Release and runs `-L core` (signal behavior
and a relocated installed-package consumer). Desktop checks are a separate
`-L desktop` group, also available through the workflow's manual `desktop` input.

The checks exercise real HWND attachment, vector/text drawing, exception-unwind
cleanup, resize, current-monitor DPI sizing, minimize/restore, native animation
completion, and redraw after graphics replacement. Device loss is injected as
an HRESULT and a removal-event signal; these checks do not reset the GPU or
qualify driver/TDR recovery. Pixel readback checks cover text, color, atlas offsets,
and 96/120/144/168/192 DPI before and after recovery. Multiple-window lifetime,
callback exceptions, failed/repeated recovery, and shutdown ordering have dedicated
regressions. Physical mixed-DPI monitor transitions and Windows 10 runtime
compatibility still require separate machine testing.

Verified locally on Windows 11 build 26300 with clang-cl 21.1.1 and SDK
10.0.26100.0: Debug and Release builds passed both smoke tests at 96 DPI.
Two captures of the ordinary demo also confirmed the rendered canvas and changing
tile position/status opacity.

The interop follows the Windows SDK's
[Composition surface BeginDraw contract](https://learn.microsoft.com/en-us/windows/win32/api/windows.ui.composition.interop/nf-windows-ui-composition-interop-icompositiondrawingsurfaceinterop-begindraw).

## License

[MIT](LICENSE). Third-party dependencies retain their respective licenses.
