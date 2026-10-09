# Composia

A small C++23 UI framework for desktop HWNDs, using Windows Composition,
Direct2D, DirectWrite, and Direct3D 11. The demo renders a text/vector canvas with
an animated visual tree and accessible buttons. No custom IDL, XAML, UWP
application, or Windows App SDK is needed.

## Build and run

Use Windows 10 version 1903 or newer, an x64 Visual Studio C++ toolchain with a
Windows SDK 10.0.26100.0 or newer, clang-cl with C++23 support, CMake 3.25+, Ninja, and Git.
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
toolchain and the static CRT (`x64-windows-static`), and the project sets
`CMAKE_MSVC_RUNTIME_LIBRARY` to match. The executables import only Windows system
DLLs; no Visual C++ redistributable is needed. The baseline matches the submodule
commit.

## Texture studio demo

```powershell
.\out\build\release\composia-media-demo.exe
.\out\build\release\composia-media-demo.exe "C:\Videos\clip.mp4"
```

The demo starts with an animated GPU landscape. **Capture…** opens the Windows
picker to select a window or display for live Windows Graphics Capture (WGC).
The captured content appears beneath composed text, with the GPU landscape in
picture-in-picture. **Stop capture** ends the session and returns to the landscape;
closing the captured window also ends capture. Windows' capture indicator stays
enabled. Canceling the picker keeps the current content. If you close the demo
while the system picker is open, dismiss the picker to finish exiting.

You can also open or drop a local video. **Play / pause** controls the video, or
the landscape when no video is open; it is disabled during screen capture.
**GPU only** stops either source and returns to the landscape. Videos loop;
supported formats depend on the codecs installed on the PC. Add `--warp` to use
software rendering.

This demo needs a recent Windows 11 runtime and a graphics device supporting
composition textures. It checks support at runtime and displays an explanation
when unavailable. The ordinary demo keeps the framework's Windows 10 baseline.

`TextureSurface` uses the SDK's
[`ICompositorInterop2::CreateCompositionTexture`](https://learn.microsoft.com/en-us/windows/win32/api/windows.ui.composition.interop/nf-windows-ui-composition-interop-icompositorinterop2-createcompositiontexture)
to wrap an `ID3D11Texture2D` as a `CompositionTexture`, then attach it to a surface
brush and sprite visual. The landscape is rendered with a D3D11 pixel shader;
WinRT `MediaPlayer` copies decoded video frames into another set of D3D11 textures
with `CopyFrameToVideoSurface`. Neither rendering path reads pixels back to the
CPU. DirectWrite text, translucent plates, rounded clips, and animated visuals
remain separate layers.

Each stream rotates three shared textures. Before writing, the demo checks
`TextureSurface::available()`, which polls the compositor's availability fence;
it skips a frame when all buffers are busy. Consumers must stop referencing a
texture and observe its availability before changing its pixels. Availability
does not synchronize other application threads writing the same resource.
Recreate these textures after graphics-device replacement; the demo restores
both playing and paused content. Preview updates use a 16 ms UI timer and
pause while minimized; an active WGC session continues until stopped.
Shader compilation, media playback, and file-picker
dependencies are confined to `demo/media`.

`ScreenCapture` wraps the SDK's
[`Windows.Graphics.Capture`](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture)
frame pool and session. It accepts a `GraphicsCaptureItem` from the system picker
or desktop interop, polls a free-threaded frame pool, and adapts its buffers when
the target resizes. The demo copies each acquired frame on the GPU into a shared
composition texture before returning the capture buffer to WGC. This copy keeps
WGC's buffer lifetime independent of the compositor's availability fence; captured
textures are not held after their frame is closed. Device replacement rebuilds
capture resources for the selected target.

Call `start`, `next_frame`, `recreate`, and `close` on the UI thread. Close every
returned frame before polling again, recreating, or stopping capture. Native item,
frame-pool, and session accessors remain available. Target-closed callbacks only
update shared atomic state; they never access a window. The demo also polls picker
completion on the UI thread. Normal window closure waits for the picker to finish
and discards its result, since canceling the async operation does not reliably
dismiss the Windows picker UI.

Capture is an SDR BGRA preview: it does not record files, capture audio, or tone-map
HDR displays. Screen-capture controls are disabled when WGC is unsupported.

## Virtual atlas demo

```powershell
.\out\build\release\composia-virtual-demo.exe
```

A scrollable, procedural map on a **1,048,576 × 1,048,576** pixel
[`CompositionVirtualDrawingSurface`](https://learn.microsoft.com/en-us/uwp/api/windows.ui.composition.compositionvirtualdrawingsurface).
An ordinary BGRA bitmap of that size would require 4 TiB. The demo draws only
512-pixel tiles covering the viewport plus one tile of overscan, then calls
`Trim` with the region to retain. Scrolling away releases the old regions;
returning redraws them. No document-sized texture or CPU bitmap is created.
The status bar reports cached tiles and retained pixel bytes; this is an estimate
of BGRA content, **not a measurement of GPU memory**, which includes driver
allocation granularity and compositor overhead.

Drag to pan, use either native scrollbar, or scroll with the mouse wheel
(Shift for horizontal). Ctrl+wheel zooms around the pointer; the zoom buttons
use the viewport center. Home/End jump to opposite corners, arrow keys move by
a line, and Page Up/Down move by a viewport. Click the map to give it keyboard
focus. Add `--warp` for software rendering. The demo keeps the Windows 10 baseline.

`VirtualSurface` supplies the reusable cache for maps, document pages, and image
editors. Its painter receives a tile-local, 96-DPI drawing context and the tile's
rectangle in document pixels. Fetch/decode or generate only the relevant content
inside the callback; the sample generates map tiles without external data.

```cpp
composia::VirtualSurface document{app.graphics(), {1000000, 1000000}};
document.update({0, 0, 1200, 800}, [](composia::ScopedSurfaceDraw& draw, const RECT& tile) {
    // Draw this document region at local (0, 0); the helper clears it first.
    draw.context()->Clear(D2D1::ColorF(0xFFFFFF));
});
auto brush = app.compositor().CreateSurfaceBrush(document.surface());
```

The size and viewport use document pixels, independent of window DPI. Use a
surface brush with `Stretch(None)`, zero alignment ratios, scale for zoom, and
negative viewport-origin offset, as in `demo/virtual`. `invalidate(rect)` dirties
intersecting tiles; the next `update` repaints them. `clear()` trims all backing
regions. `resize()` clears content and changes the logical dimensions. Device
generation changes invalidate the cache automatically on the next update.
Keep the graphics device alive, and call the helper on the UI thread inside
`Application::render`. Do not reenter it or alter its surface from the painter.
If directly drawing through the native surface accessor, invalidate affected
cached tiles before returning to managed updates.

Dimensions must be positive and at most 2²⁴ pixels. Tile size is configurable
from 64 to 2048 pixels; the default cache budget is 256 tiles. Oversized viewports
are rejected before changing the retained region, rather than allocating an
unbounded number of tiles. The demo limits zoom-out to fit this budget. It scales
the cached raster when zooming; a production map viewer or document reader can
add resolution levels and asynchronous loading for its own content.

`ScopedSurfaceDraw` also accepts an update `RECT` in physical surface pixels.
Its ordinary context uses whole-surface DIPs, with the update origin and atlas
offset included in the transform. `VirtualSurface` deliberately changes that
transform to tile-local pixels to preserve precision at distant coordinates.

## Mail client demo

```powershell
.\out\build\release\composia-mail-demo.exe
```

A three-pane mail client built only from the framework's primitives: folders,
a message list, a reading pane, and a compose form, over an in-memory mailbox of
fictional messages. **It is a UI exercise.** There is no account, sign-in,
storage, or network code; sending a message only appends it to the Sent folder
for the lifetime of the process, and attachments are labels.

The list scrolls with the wheel, Page Up/Down, or the keyboard. Clicking a row
opens it and marks it read; stars toggle from the row or the header; **Archive**
and **Delete** move messages and offer **Undo** in the status bar; deleting from
Trash is permanent. Search filters the current folder as you type. **Compose**,
**Reply**, and **Forward** open an editable form; **Send** files the message under
Sent, **Save draft** keeps it under Drafts, and Escape saves a non-empty draft.
Open a draft with Enter or a click to continue editing it. With the list focused:
C composes, R replies, F forwards, E archives, S stars, U marks unread, Delete
trashes, Up/Down and Home/End move, Ctrl+F focuses search, and Ctrl+Z undoes.
Add `--warp` for software rendering. The demo keeps the Windows 10 baseline.

The chrome, folders, rows, header, and status bar are drawn into the window's
canvas surface on each paint; each paint also records the DIP rectangles of the
clickable regions, which pointer messages hit-test against. Buttons are the
framework's accessible `Button` controls. `demo/mail/TextField` is sample code
for a composition-rendered, editable text box: a child HWND that draws its text
into its own surface and positions a blinking caret visual with a composition
animation, so blinking never redraws text. It supports typing, Backspace/Delete
(with Ctrl for words), arrows, Home/End, Enter, Ctrl+V paste, wheel scrolling of
multiline content, and pointer caret placement, and it hands Tab to the parent's
dialog navigation. It has no selection, IME composition, or UI Automation
provider; the audit message in the sample inbox lists what a product version
would need. `demo/mail/MailModel` holds the mailbox and formatting helpers and
has no UI dependencies. The demo deliberately avoids `std::format` and the CRT's
time and locale formatting, and keeps its sample text as UTF-8 constant data; those
three choices keep the statically linked release executable near 720 KB, about a
quarter smaller than the first version.

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
same vcpkg toolchain/triplet and the same static runtime library, as
`tests/consumer` does; the linker rejects a consumer built with the dynamic CRT.
Installed targets carry their dependencies.

| Module | Responsibility |
| --- | --- |
| `Application` | STA, dispatcher queue, compositor, message loop, and graphics recovery |
| `Window` | Owning HWND, exception-safe message dispatch, resize and per-monitor DPI events, pointer hover and capture, focus, and enabled state |
| `GraphicsDevice` | D3D11 device5, D2D device6/context6, DirectWrite factory7, and Composition graphics device |
| `CompositionWindowTarget` | Desktop HWND bridge, visual root, canvas surface/brush, pixel/DIP sizing, and one-call client rendering |
| `ScopedSurfaceDraw` | Balanced surface BeginDraw/EndDraw with DPI and atlas-offset translation, and scope-owned solid brushes |
| `VirtualSurface` | Sparse drawing surface, bounded viewport tile cache, trimming, dirty regions, and recovery |
| `TextureSurface` | Direct D3D11 texture composition, capability check, and availability fence |
| `ScreenCapture` | WGC session, captured-frame polling, target resize, and device replacement |
| `AnimationHelpers` | Containers/sprites, implicit offset, vector/scalar keyframes, and expression layout |
| `TextLayout` | Reusable DirectWrite format/layout with incremental bounds updates |
| `Button` | Composition-rendered child HWND, pointer/keyboard input, and UI Automation Invoke |
| `Layout` | DIP points and rectangles, hit testing, and horizontal/vertical stack placement |

`demo/main.cpp` only initializes logging and starts the application/window.
`demo/DemoWindow.cpp` demonstrates drawing and scene assembly. The moving tile
uses vector keyframes; the status dot uses scalar opacity and implicit offset
animations; an expression centers the tile's container. Composition runs these
animations independently of the UI message loop. The ordinary demo has no
rendering timer.

Resize events invalidate the canvas and coalesce into paint work. Text layouts
are retained across redraws and device replacement; solid brushes come from the
draw scope, so nothing device-dependent is cached across frames. `Window.hpp`
includes only native windowing support; Composition and graphics headers are
separate.

`Window` tracks the state every control and application window needs, so that
plumbing is not repeated: `hovered()` with `on_hover`; `capture_pointer()` and
`release_pointer()` with `on_capture_lost`, raised when another window takes the
capture or `WM_CANCELMODE` cancels it, never for an explicit release; `focus()`
and `focused()` with `on_focus`; and `set_enabled()` and `enabled()` with
`on_enabled`, where `enabled()` is false while any ancestor is disabled.
`pointer_position()` converts a client-relative mouse message to DIPs,
`pointer_position_from_screen()` does the same for wheel messages, `scale()` is
the DPI factor, and `client_bounds()` is the client area in DIPs. The tracking
runs before `on_message`, so an override can handle a raw message and still read
the state. Painted elements that share one HWND use these directly; nothing
requires an HWND per element.

`CompositionWindowTarget::render` paints a window in one call: it sizes the
surface to the client area at the window's DPI, opens a draw scope through
`Application::render` (one retry after device loss), and passes the scope and
the logical size to the callback, skipping minimized or empty windows.
`ScopedSurfaceDraw::solid_brush` hands out brushes owned by the scope, created
once per color and released when it finishes. `Button` and the demo are built on
these public mechanisms only.

Create one `Application` on the UI thread and pass it to each `Window` constructor.
`Application::run()` serves all registered windows and exits when the last top-level HWND
closes. Destroy window objects and Composition objects, then call `app.close()`
to observe shutdown errors. All framework operations and surface updates belong
on that thread. The demo embeds a
PerMonitorV2 manifest; consumers should embed the same DPI declaration. Layout
uses DIPs, surfaces use physical pixels, and the root applies the DPI scale.

After native window destruction, `hwnd()` returns null and `dpi()` returns zero.
Showing, invalidating, moving, or querying client bounds on a closed window throws
`HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE)`. A button requires a live parent;
changing its enabled state or invoking it after destruction throws the same error.

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
`SetRenderingDevice`, preserves Composition visuals/surfaces, and invalidates all
windows. `Application::render` retries a drawing operation once after
device loss; persistent or unrelated errors propagate. Recreate consumer-owned
device-dependent caches in `Window::on_graphics_recreated()` or subscribe with
`GraphicsDevice::on_recreated()` and retain the returned `Connection`. Device
replacement prepares the device and removal subscription before changing
Composition; a preparation failure preserves the previous published state. Raw device
access does not transfer ownership or extend a drawing scope's validity.

## Controls

```cpp
composia::Application app;
{
    composia::Window window{app, L"Example", 640, 480};
    composia::Button close{window, L"Close"};
    close.set_bounds({24, 24, 160, 44});
    auto clicked = close.on_click([&] {
        PostMessageW(window.hwnd(), WM_CLOSE, 0, 0);
    });
    window.show();
    app.run();
}
app.close();
```

Include `composia/Application.hpp` and `composia/Button.hpp`. Keep the returned
connection alive for as long as the handler should run. Buttons support pointer
capture/cancellation, Space/Enter activation, Tab/Shift+Tab focus, disabled state,
and a visible focus outline. Each button hosts an SDK UI Automation provider with
name, button role, focus/enabled properties, Invoke, and focus/invocation events.
UI Automation invocation is marshaled to the application queue; retained providers
reject calls after the control is destroyed. The demo's buttons change and reset
the tile's motion.

## Validation

`ctest --preset debug` and `ctest --preset release` run the test suite, including
hardware-preferred and forced-WARP desktop checks. Desktop checks briefly show
windows and require a Windows desktop. CTest retains output in
`out/build/<preset>/Testing/Temporary/LastTest.log`; `build.ps1 -Test` also writes
JUnit results in `out/build/<preset>/test-results-*.xml`. Normal runs write
`composia.log` in the working directory.
Smoke-test code lives in separate test executables. `mail-model` (core) checks the
in-memory mailbox and its formatting helpers without windows; `mail-client` and
`mail-client-warp` (desktop) drive the mail demo through pointer, keyboard, and
button input: selection, stars, trash and undo, folders, search, scrolling, compose,
send, reply, forward, drafts, Escape handling, resize, and device replacement.

The Windows CI matrix builds Debug/Release and runs `-L core` (signals, layout,
and a relocated installed-package consumer). Desktop checks are a separate
`-L desktop` group, also available through the workflow's manual `desktop` input.
Use `./scripts/build.ps1 -Preset debug -Test -TestLabel core` to build and run
only checks that do not open desktop windows.

The checks exercise real HWND attachment, vector/text drawing, exception-unwind
cleanup, resize, current-monitor DPI sizing, minimize/restore, native animation
completion, and redraw after graphics replacement. `foundation-pointer`,
`foundation-focus`, and `foundation-render` (with WARP variants) check the
window mechanisms: DIP conversion of pointer and screen coordinates, hover entry
and leave, capture released explicitly, taken by another window, or cancelled,
focus and enabled notifications including disabled ancestors, the rendering
helper's sizing, minimized skip, device replacement, and injected device-loss
retry, scope-owned brushes, and rejection of destroyed windows. Device loss is injected as
an HRESULT and a removal-event signal; these checks do not reset the GPU or
qualify driver/TDR recovery. Pixel readback checks cover text, color, atlas offsets,
and 96/120/144/168/192 DPI before and after recovery. Multiple-window lifetime,
callback exceptions, failed/repeated recovery, and shutdown ordering have dedicated
regressions. Input tests cover capture, cancellation, keyboard activation, disabled
ancestors, tab navigation, and stale providers. A separate MTA UI Automation client
discovers and invokes the button. Monitor tests move a window across all attached
monitors and check child HWND/Composition sizing; their output records monitor
count and observed DPIs. Physical mixed-DPI transitions need monitors with differing
scale settings. Windows 10 runtime compatibility still requires separate machine
testing.

Texture checks run with hardware-preferred and forced-WARP devices. Media checks
verify changing GPU and decoded video pixels, opaque alpha, pause/resume, resize,
playing and paused device replacement, Unicode file paths, and missing-file
recovery. The small [synthetic video fixture](tests/assets/README.md) is generated
locally; tests need no network media. Pixel readback is test-only. The six texture
and media checks report a CTest skip when composition textures are unsupported;
a skipped check is not evidence of media playback on that machine.

Four WGC checks capture an owned test window and its monitor using hardware-preferred
and forced-WARP devices. They verify known changing pixels in the texture submitted
to the compositor, target resize, device replacement, stop/restart, target closure, and closing
the preview during active capture. Monitor checks read back only a pixel inside the
owned test window; captured desktop images are not saved. These checks skip only
when WGC or composition textures report unsupported. The system picker requires
interactive verification; automated capture tests select their owned target through
the SDK's desktop interop.

Virtual-surface checks verify sparse-cache bounds across 80 distant viewports,
pixel contents after trimming, tile reuse and dirty-region redraw, callback
failure recovery, resize, and graphics replacement on hardware and WARP.
They also check the maximum logical extent, partial-update offsets at five
DPIs, and the demo's scrolling, zoom, panning, resize, and recovery. These tests
verify the retained region and rendering; they do not measure driver VRAM usage.

Verified locally on Windows 11 build 26300 with clang-cl 21.1.1 and SDK
10.0.26100.0, with one 96-DPI monitor. Failure-injection checkpoints are compiled
out when `COMPOSIA_BUILD_TESTS=OFF`; automatic device-removal recovery remains
enabled.

The interop follows the Windows SDK's
[Composition surface BeginDraw contract](https://learn.microsoft.com/en-us/windows/win32/api/windows.ui.composition.interop/nf-windows-ui-composition-interop-icompositiondrawingsurfaceinterop-begindraw).

## License

[MIT](LICENSE). Third-party dependencies retain their respective licenses.
