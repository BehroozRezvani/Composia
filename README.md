# Composia

A small C++23 foundation for native Windows applications: windowing, Windows
Composition, Direct2D/DirectWrite drawing, Direct3D 11 interop, and reusable
platform integration (input state, focus, accessibility, native controls), without
prescribing an application architecture. It requires neither an HWND per UI
element nor continuous rendering. No custom IDL, XAML, UWP application, or Windows
App SDK is needed. The optional `Button` and `Layout` helpers and the demos are
built on the public API.

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

vcpkg supplies WIL and generated C++/WinRT SDK projections, which the library's
public headers use; nothing else comes from vcpkg. The application/framework compile with clang-cl; vcpkg
builds ABI-compatible static dependencies using the MSVC toolchain and the static
CRT (`x64-windows-static`), and the project sets `CMAKE_MSVC_RUNTIME_LIBRARY` to
match. The executables import only Windows system DLLs; no Visual C++
redistributable is needed. `builtin-baseline` in `vcpkg.json` pins the package
versions; the submodule supplies the vcpkg tool and must contain that baseline
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
when unavailable. The ordinary demo keeps the framework's Windows 10 baseline; on
Windows 10, Direct3D content reaches the visual tree through `SwapChainSurface`
instead, as `examples/gpu` shows (see [GPU content](#gpu-content)).

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
three choices keep the statically linked release executable small, about 475 KB.

## Framework

Link `Composia::UI` from CMake; public headers are in `include/composia`.

The demo and tests are optional (`COMPOSIA_BUILD_DEMO` and
`COMPOSIA_BUILD_TESTS`). Both default off when included with `add_subdirectory`.
To install and consume the library:

```powershell
cmake --install out/build/release --prefix out/install
```

```cmake
find_package(Composia 0.3 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE Composia::UI)
```

Set `CMAKE_PREFIX_PATH` to the installation and configure the consumer with the
same vcpkg toolchain/triplet and the same static runtime library, as
`tests/consumer` does; the linker rejects a consumer built with the dynamic CRT.
Installed targets carry their dependencies.

| Module | Responsibility |
| --- | --- |
| `Application` | STA, dispatcher queue, compositor, message loop, and graphics recovery |
| `Window` | Owning HWND, exception-safe message dispatch, resize and per-monitor DPI events, pointer hover and capture, focus and its restoration, enabled state including ancestors, state signals, partial invalidation, child-control notification routing, and a UI Automation provider slot |
| `GraphicsDevice` | D3D11 device5, D2D device6/context6, DirectWrite factory7, and Composition graphics device |
| `CompositionWindowTarget` | Desktop HWND bridge, visual root, canvas surface/brush, pixel/DIP sizing, and one-call client rendering that updates only the invalidated area |
| `ScopedSurfaceDraw` | Balanced surface BeginDraw/EndDraw with DPI and atlas-offset translation, an update clip, and scope-owned solid brushes |
| `VirtualSurface` | Sparse drawing surface, bounded viewport tile cache, trimming, dirty regions, and recovery |
| `TextureSurface` | Direct D3D11 texture composition, capability check, and availability fence (Windows 11) |
| `SwapChainSurface` | A swap chain for Composition: Direct3D 11 frames in the visual tree on Windows 10 and later, presented on demand and rebuilt after device replacement |
| `ScreenCapture` | WGC session, captured-frame polling, target resize, and device replacement |
| `AnimationHelpers` | Containers/sprites, implicit offset, vector/scalar keyframes, and expression layout |
| `TextLayout` | Reusable DirectWrite format/layout with a chosen family and locale, incremental bounds updates, and measurement |
| `Accessible` | UI Automation provider for any window, built on the window's public signals: name, control type, automation ID, focus and enabled state, Invoke and Value patterns, cross-thread marshalling |
| `NativeControl` | Hosts standard Win32 controls (EDIT, BUTTON, COMBOBOX, ...) with DIP bounds, DPI-aware fonts, colors, and notifications, built on the public notification route |
| `Button` | Composition-rendered child HWND, pointer/keyboard input, and UI Automation Invoke, built only on the mechanisms above |
| `Layout` | DIP points and rectangles, hit testing, and horizontal/vertical stack placement |
| `Log` | An optional handler for the library's diagnostic events; `OutputDebugString` otherwise |

`demo/main.cpp` only initializes logging and starts the application/window.
`demo/DemoWindow.cpp` demonstrates drawing and scene assembly. The moving tile
uses vector keyframes; the status dot uses scalar opacity and implicit offset
animations; an expression centers the tile's container. Composition runs these
animations independently of the UI message loop. The ordinary demo has no
rendering timer.

Resize events invalidate the canvas and coalesce into paint work. Text layouts
are retained across redraws and device replacement; solid brushes come from the
draw scope, so nothing device-dependent is cached across frames. `Window.hpp`
includes only native windowing support (and a forward declaration of the UI
Automation provider interface); Composition and graphics headers are separate.

`Window` tracks the state every control and application window needs, so that
plumbing is not repeated:

- **Hover.** `hovered()` and `on_hover` report whether the pointer is over the
  client area. While the window has captured the pointer, Windows keeps sending
  moves from outside and reports no leave until the capture ends, so hover follows
  the pointer position then.
- **Capture.** `capture_pointer()` and `release_pointer()`, with `on_capture_lost`
  when another window takes the capture or `WM_CANCELMODE` cancels it, never for an
  explicit release.
- **Focus.** `focus()`, `focused()`, and `on_focus`. When a top-level window is
  activated again (after Alt+Tab, another window, or minimize and restore), it gives
  the focus back to the window inside it that last had it, as dialogs do, as long as
  that window is still visible and enabled. The message loop records the focus,
  because minimizing clears it before the window is deactivated. A multiline edit
  control that passes Tab on with `WM_NEXTDLGCTL` gets the same handling as in a dialog.
- **Enabled state.** `set_enabled()` and `enabled()`, where `enabled()` is false while
  any ancestor is disabled. Windows sends `WM_ENABLE` only to the window whose own
  state changed, so the window also refreshes every Composia window below it, and
  `on_enabled` reports each change of the effective state. A disabled top-level
  window, such as the owner of a modal dialog, therefore disables its Composia
  controls too: they stop accepting input and UI Automation actions, and `Button`
  draws itself disabled. Native child controls keep their normal look in that case.
- **DIPs.** `pointer_position()` converts a client-relative mouse message to DIPs,
  `pointer_position_from_screen()` does the same for wheel messages, `scale()` is the
  DPI factor, and `client_bounds()` is the client area in DIPs.

The tracking runs before `on_message`, so an override can handle a raw message and
still read the state. Code that does not derive from the window, such as a parent
watching a `Button` or an object drawn into the window, subscribes to the same
changes with `on_hover_changed`, `on_focus_changed`, `on_enabled_changed`,
`on_pointer_capture_lost`, and `on_destroy`; each fires after the virtual hook.

`invalidate()` schedules a repaint of the whole client area and
`invalidate(layout::Rect)` of a DIP rectangle, rounded out to whole pixels. Requests
coalesce into one `WM_PAINT`, and `paint_rect()` reports the pixels being repainted
while `on_paint` runs. `CompositionWindowTarget::render` paints a window in one
call: it sizes the surface to the client area at the window's DPI, opens a draw
scope through `Application::render` (one retry after device loss), and passes the
scope and the logical size to the callback, skipping minimized or empty windows.
Inside `on_paint`, once the surface holds a complete frame, only the area being
repainted is updated: Composition keeps the rest of the surface, the scope clips
drawing to the area, and `ScopedSurfaceDraw::update_bounds()` reports it so a
painter can skip anything outside. A resize, a DPI change, or a replaced graphics
device repaints everything. A target renders only the window it was created for.
`ScopedSurfaceDraw::solid_brush` hands out brushes owned by the scope, created once
per color and released when it finishes. `Button` and the demos are built on these
public mechanisms only.

Elements drawn inside one HWND need no window of their own: they share the window's
pointer messages, capture, invalidation, and DIP conversions. Two things stay per
HWND in Windows, keyboard focus and the UI Automation element, so drawn content that
must be reachable with Tab next to native controls, or exposed to screen readers,
goes in a child `Window` (a tab stop by default) with an `Accessible`, as the slider
in `examples/inputs` does. That child can still draw many parts; exposing those
parts individually would need a UI Automation fragment provider, which Composia does
not supply.

`Accessible` gives any window a UI Automation presence, using only the window's
public API: the provider slot that answers `WM_GETOBJECT`
(`set_automation_provider`) and the focus, enabled, and destroy signals. Construct
one with the window, a name, a control type, optionally an automation ID and a
localized control type (UI Automation expects the latter for
`UIA_CustomControlTypeId`), an `invoke` callback (the Invoke pattern), and `value`
with an optional `setValue` callback (the Value pattern, read-only without the
callback). It reports focus and enabled changes, including those of ancestors, and
disconnects when the native window is destroyed or the `Window` object goes away,
whichever comes first; after that, `window()` is null and the provider reports the
element as unavailable. Properties are answered from state kept under a lock
because UI Automation calls arrive on other threads; actions are posted to the UI
thread through the Application and dropped if the control is gone by then. Call
`set_name`, `set_value`, and `raise_invoked` to keep clients informed. One provider
per window; a second attachment throws.

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

Composia reports its diagnostic events (graphics device creation and the fallback to
WARP, device loss and removal, and shutdown) as `event=name key=value` text, at an
info or warning level. They go to `OutputDebugString` unless `set_log_handler` in
`composia/Log.hpp` routes them elsewhere, such as an application's own log; set it
before creating the `Application` to see every event. Composia depends on no logging
library. The demos write these events, with their own, to log files through
`demo/DemoLog.hpp`.

## Text input and native controls

Two paths exist for text input. The recommended one reuses the system's EDIT
control through `NativeControl`. Windows then supplies IME composition, selection,
the clipboard, keyboard conventions, and the UI Automation Text and Value patterns:

```cpp
composia::NativeControl name{window, L"EDIT", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL};
name.set_bounds({120, 96, 400, 32});
name.set_colors(RGB(234, 242, 244), RGB(14, 22, 31));
auto changed = name.on_command([&](UINT code) { if (code == EN_CHANGE) { /* name.text() */ } });
```

`NativeControl` creates any standard control class as a child of a `Window`,
places it in DIPs, applies a font sized in points for the window's monitor
(Segoe UI 9pt by default, rebuilt on `WM_DPICHANGED_AFTERPARENT`), and delivers
`WM_COMMAND` notification codes and `WM_NOTIFY` headers to `on_command` and
`on_notify`. Windows does not move child windows when the DPI changes, so set the
bounds again from `on_resize`, which runs after a DPI change; `examples/inputs`
does this. `text`, `set_text`, `show`, `set_enabled`, `focus`, and `send` cover the
common operations, and `hwnd()` remains available for everything else, such as
`EM_SETCUEBANNER` or `BM_GETCHECK`. The control is destroyed with its wrapper or
with the parent window, after which calls throw `ERROR_INVALID_WINDOW_HANDLE`.

`set_colors` answers the control's `WM_CTLCOLOR*` requests with the given text and
background colors so that it matches a composition-drawn surface. Edit fields,
static text, list boxes, and combo boxes (their edit field and drop-down list
included) honor both colors. Visual styles draw the text of check boxes, radio
buttons, and group boxes in the theme's color, so `set_colors` turns visual styles
off for those controls, which then take the classic look, and `clear_colors`
restores them. Push buttons draw with system colors and use the background only
around their edges; use `BS_OWNERDRAW` or a composition `Button` for colored ones.

Notifications travel through a public route: `Window::set_notification_handler`
registers a `NotificationHandler` for a child control's HWND, and the parent asks
it about `WM_COMMAND`, `WM_NOTIFY`, `WM_CTLCOLOR*`, `WM_DRAWITEM`, `WM_HSCROLL`, and
`WM_VSCROLL` from that control or from windows inside it. `NativeControl` uses this
route, and so can wrappers for other controls. The parent asks after its own
`on_message`, so an override that answers `WM_COMMAND` for every source also
silences hosted controls; return `std::nullopt` for messages it does not handle.

Child HWNDs always draw above the parent's composition content, so hosted controls
sit on top of a `CompositionWindowTarget` canvas and cannot be clipped, transformed,
or animated by Composition. Embed a common-controls v6 manifest dependency, as
`demo/app.manifest` does, to get the themed look.

The other path is a composition-rendered field such as `demo/mail/TextField`,
for when rendering must be custom. It is sample code: it handles typing, editing
keys, paste, scrolling, and caret placement, but has no selection, IME composition,
or accessibility. A product-quality custom field needs those: an `Accessible` with
the Value pattern exposes the text, but screen readers also need the Text pattern
for the caret, selection, and moving through the text, which Composia does not
provide.

`examples/inputs/main.cpp` is a standalone program using only the public headers:
text drawn into a composition canvas, two hosted EDIT controls and a native check
box with matching colors, a framework `Button`, and a slider drawn into one child
window that is a tab stop with its own UI Automation element. Changes repaint only
the areas they affect, and the window watches the slider's focus through its public
signal to show keyboard help.

```powershell
.\out\build\release\composia-inputs-example.exe
```

## GPU content

Composia composes; it does not render 3D or run a render loop. An application draws
with Direct3D 11, through `app.graphics().d3d_device()` and `d3d_context()` and any
library it likes, into one of two surfaces that sit in the visual tree, where
Composition clips, transforms, animates, and composes them like any other visual:

- `TextureSurface` wraps an `ID3D11Texture2D` as a composition texture, with no copy
  and an availability fence. It needs a recent Windows 11 runtime and driver support,
  which `TextureSurface::supported` reports.
- `SwapChainSurface` is a DXGI swap chain made for Composition, for Windows 10 and
  later. Put its `brush()` on a sprite visual sized to it in DIPs, resize it in pixels
  with the visual, and call `present` with a callback that draws into the back
  buffer.

Neither surface runs a render loop or a frame clock. Composition keeps showing the
last frame and runs its own animations without new frames, so an application
presents only when its content changes: after input, new data such as a decoded
video frame, a resize, or a device replacement. Content that changes continuously
brings its own timing, such as its video source's frame events. Swap chains, textures,
and every other Direct3D object belong to the device that made them: after a graphics
device replacement, `SwapChainSurface` rebuilds its swap chain, empty, on the next
`present` or `resize`, and Composia's repaint of every window after recovery is the
moment to present again and to rebuild the application's own Direct3D objects.

```cpp
composia::SwapChainSurface frame{app, {1280, 720}};
sprite.Brush(frame.brush());
frame.present([&](ID3D11RenderTargetView* target, ID3D11Texture2D*) {
    const float clear[]{0.1f, 0.1f, 0.1f, 1};
    app.graphics().d3d_context()->ClearRenderTargetView(target, clear);
    // Draw the scene into target here.
});
```

`examples/gpu/main.cpp` draws a triangle with Direct3D 11 into a `SwapChainSurface`
behind a rounded composition clip. Dragging or the arrow keys turn it, each change
presents one frame, and a frame counter shows that nothing presents while a
compositor animation keeps running.

```powershell
.\out\build\release\composia-gpu-example.exe
```

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
capture/cancellation, Space/Enter activation, Tab/Shift+Tab focus, disabled state
(including a disabled parent), and a visible focus outline. Each button carries an
`Accessible` with name, button role, focus/enabled properties, Invoke, and
focus/invocation events.
UI Automation invocation is marshaled to the application queue; retained providers
reject calls after the control is destroyed. The demo's buttons change and reset
the tile's motion.

## Validation

`ctest --preset debug` and `ctest --preset release` run the test suite, including
hardware-preferred and forced-WARP desktop checks. Desktop checks briefly show
windows and require a Windows desktop. CTest retains output in
`out/build/<preset>/Testing/Temporary/LastTest.log`; `build.ps1 -Test` also writes
JUnit results in `out/build/<preset>/test-results-*.xml`. The demos write their
log files (`composia.log`, `composia-mail.log`, and so on) in the working directory.
Smoke-test code lives in separate test executables. `mail-model` (core) checks the
in-memory mailbox and its formatting helpers without windows; `mail-client` and
`mail-client-warp` (desktop) drive the mail demo through pointer, keyboard, and
button input: selection, stars, trash and undo, folders, search, scrolling, compose,
send, reply, forward, drafts, Escape handling, resize, and device replacement.

The Windows CI matrix builds Debug/Release and runs `-L core`: signals, layout,
text measurement, and a relocated installed-package consumer that compiles and
links every public header, including the hosted-control and accessibility ones.
Desktop checks, which include all the window-mechanism checks below, need an
interactive desktop session; they are a separate `-L desktop` group that CI runs
only through the workflow's manual `desktop` input. Use
`./scripts/build.ps1 -Preset debug -Test -TestLabel core` to build and run only
checks that do not open desktop windows.

The checks exercise real HWND attachment, vector/text drawing, exception-unwind
cleanup, resize, current-monitor DPI sizing, minimize/restore, native animation
completion, and redraw after graphics replacement. The `foundation-*` checks (each
with a WARP variant) cover the window mechanisms:

- `foundation-pointer`: DIP conversion of pointer and screen coordinates; hover
  entry and leave, hover that follows a captured pointer out of and back into the
  client area, and hover from a button press; capture released explicitly, taken
  by another window, or cancelled; and the hover and capture signals.
- `foundation-focus`: focus and enabled notifications; enabled changes of an
  ancestor reaching every descendant, including a `Button` that repaints, and
  staying silent while an ancestor decides the state; the focus and enabled
  signals; and focus restoration after another window was activated, after
  minimize and restore, to the window itself, and not to a hidden control. Steps run
  from the message loop, which records the focus between them.
- `foundation-render`: the rendering helper's sizing, minimized skip, device
  replacement, injected device-loss retry, scope-owned brushes, and rejection of
  other or destroyed windows; and partial rendering: the update area for one or
  several invalidated rectangles, clipping to the client area, and full repaints
  after a resize or a device replacement.
- `foundation-partial`: captures the target's visual with Windows Graphics Capture
  and checks the composited pixels after partial repaints, inside and outside the
  repainted areas. It reports a CTest skip when capture is unavailable.
- `foundation-swapchain`: checks, through the same capture, the composited pixels
  of a `SwapChainSurface` after its first present, a resize, a graphics device
  replacement (which must rebuild the swap chain), and an injected device loss
  during `present`; that its last frame stays on screen while only the canvas
  changes; and that invalid sizes, alpha modes, and renderers are rejected.
- `foundation-accessible`: an `Accessible` on a plain window in-process: properties
  including the automation ID and localized control type, focus and enabled
  reporting, focusability while disabled, renaming, `WM_GETOBJECT` routing, the
  host provider, pattern availability, Invoke and Value actions marshalled to the
  UI thread, rejection while disabled or under a disabled parent, duplicate
  attachment, unavailability after the HWND is destroyed, and an `Accessible` that
  outlives its `Window` object. The separate MTA client in `accessibility` still
  discovers and invokes a `Button` through the system.
- `foundation-native`: hosted EDIT, multiline EDIT, check box, combo box, and list
  view controls: class and placement in pixels, text, typed `EN_CHANGE` routing, the
  default, replaced, and DPI-rebuilt fonts, colors answered through the
  `WM_CTLCOLOR*` messages and cleared again, the drawn pixels of colored text in an
  EDIT and a check box label (through `PrintWindow`), visual styles turned off and
  back on for the check box, combo box edit-field and drop-down colors, `NM_SETFOCUS`
  reaching `on_notify`, a raw child control on the public notification route,
  `BN_CLICKED` and check state, enabled and visible state including a disabled
  parent, Tab navigation through the application's dialog loop and out of a
  multiline EDIT, and destruction with the parent.

Device loss is injected as an HRESULT and a removal-event signal; these checks do
not reset the GPU or qualify driver/TDR recovery. Pixel readback checks cover text,
color, atlas offsets, and 96/120/144/168/192 DPI before and after recovery.
Multiple-window lifetime, callback exceptions, failed/repeated recovery, and
shutdown ordering have dedicated regressions. Input tests cover capture,
cancellation, keyboard activation, disabled ancestors, tab navigation, and stale
providers. A separate MTA UI Automation client discovers and invokes the button.
Monitor tests move a window across all attached monitors and check child
HWND/Composition sizing; their output records monitor count and observed DPIs.

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

These behaviors are not verified by the checks above:

- Moving windows between monitors with different scale settings. The DPI checks
  ran on a single 96-DPI monitor; the hosted-control font rebuild is checked by
  sending `WM_DPICHANGED_AFTERPARENT`, not by a real DPI change, and the classic
  check box glyph that colored check boxes use was not inspected at high DPI.
- IME composition in hosted EDIT controls, and what a screen reader announces. UI
  Automation is checked through in-process provider calls and one system client
  that invokes a `Button`.
- Real pointer and keyboard input. The checks send messages; hover during capture
  follows documented Windows behavior (no leave until the capture ends) rather
  than an observed mouse drag, and focus restoration is driven by
  `SetActiveWindow` and `WM_SYSCOMMAND` minimize and restore rather than Alt+Tab.
- Windows 10, including `SwapChainSurface`, which exists for it but ran only on
  this Windows 11 machine, and hosted controls without a common-controls v6 manifest.
- Whether the update clip is ever needed: on this machine, drawing outside a
  partial update's rectangle did not reach the surface even without the clip, so
  the clip guards against a behavior the checks could not produce.

The interop follows the Windows SDK's
[Composition surface BeginDraw contract](https://learn.microsoft.com/en-us/windows/win32/api/windows.ui.composition.interop/nf-windows-ui-composition-interop-icompositiondrawingsurfaceinterop-begindraw).

## Compatibility

0.3.0 is source compatible with 0.2.0 for code that uses the public API; rebuild
everything that links Composia, because `Window` gained virtual functions and
members, and require `find_package(Composia 0.3)`. Behavior changes:

- `Window` answers more messages after `on_message`: `WM_GETOBJECT` when an
  automation provider is set; `WM_COMMAND`, `WM_NOTIFY`, `WM_CTLCOLOR*`,
  `WM_DRAWITEM`, `WM_HSCROLL`, and `WM_VSCROLL` from controls with a notification
  handler; `WM_NEXTDLGCTL`; and `WM_ACTIVATE` on a top-level window, which now
  returns the focus to the window that last had it. Overrides of `on_message` that
  return a result still take precedence.
- `on_enabled` now also reports changes caused by an ancestor, so a disabled
  top-level window (for example, the owner of a modal dialog) disables and redraws
  its Composia controls.
- `hovered()` follows the pointer position while the window has captured it.
- Inside `on_paint`, `CompositionWindowTarget::render` updates only the area being
  repainted once the surface holds a complete frame; painters that draw the whole
  scene stay correct because drawing is clipped to that area. `ScopedSurfaceDraw`
  clips drawing to its update rectangle whenever it is given one.
- `NativeControl::set_colors` turns visual styles off for check boxes, radio
  buttons, and group boxes while colors are set.
- `Button` no longer caches brushes across frames; its header still includes
  `d2d1_1.h`.
- spdlog is no longer a dependency of the library or of the installed package, which
  also drops fmt. Composia's diagnostic events go to `OutputDebugString` unless a
  handler is set with `set_log_handler`; an application that saw them through
  spdlog's default logger can forward them to spdlog from that handler. Release
  executables are 230 to 275 KB smaller.
- DirectX Toolkit is no longer a dependency; only the demo used it, for one vector
  type, and the installed package never required it. Applications that want it
  add `directxtk` to their own manifest and use it with Composia's device.
- `SwapChainSurface` is new: Direct3D 11 content in the visual tree on Windows 10.

## License

[MIT](LICENSE). Third-party dependencies retain their respective licenses.
