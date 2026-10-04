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

Create one `Application` on the UI thread before creating its windows; destroy
windows and Composition objects before destroying the application. All framework
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

Device removal is registered with D3D11 and watched alongside the Win32 message
queue, including while idle. Recovery replaces the D3D/D2D devices through
`SetRenderingDevice`, preserves Composition visuals/surfaces, and invokes the
redraw callback. `Application::render` retries a drawing operation once after
device loss; persistent or unrelated errors propagate. Recreate consumer-owned
device-dependent caches when `GraphicsDevice::generation()` changes. Raw device
access does not transfer ownership or extend a drawing scope's validity.

## Validation

`ctest --preset debug` and `ctest --preset release` run hardware-preferred and
forced-WARP desktop checks. They briefly show a window and require an interactive
Windows desktop. Logs are `smoke-desktop.log` and `smoke-warp.log` in the build
directory; normal runs write `composia.log` in the working directory.

The checks exercise real HWND attachment, vector/text drawing, exception-unwind
cleanup, resize, current-monitor DPI sizing, minimize/restore, native animation
completion, and redraw after graphics replacement. Device loss is injected as
an HRESULT and a removal-event signal; these checks do not reset the GPU or
qualify driver/TDR recovery. Mixed-DPI monitor transitions and Windows 10 runtime
compatibility still require separate machine testing.

Verified locally on Windows 11 build 26300 with clang-cl 21.1.1 and SDK
10.0.26100.0: Debug and Release builds passed both smoke tests at 96 DPI.
Two captures of the ordinary demo also confirmed the rendered canvas and changing
tile position/status opacity.

The interop follows the Windows SDK's
[Composition surface BeginDraw contract](https://learn.microsoft.com/en-us/windows/win32/api/windows.ui.composition.interop/nf-windows-ui-composition-interop-icompositiondrawingsurfaceinterop-begindraw).
