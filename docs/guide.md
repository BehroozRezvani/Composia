# Composia guide

This guide explains how Composia's parts work together and what each one expects. The
public headers in `include/composia` document every function; the examples in
`examples/` show the pieces in use.

- [The application and its thread](#the-application-and-its-thread)
- [Windows](#windows)
- [Drawing](#drawing)
- [Content inside one window](#content-inside-one-window)
- [Accessibility](#accessibility)
- [Text input and native controls](#text-input-and-native-controls)
- [System appearance](#system-appearance)
- [Button](#button)
- [GPU content](#gpu-content)
- [Virtual surfaces](#virtual-surfaces)
- [Screen capture](#screen-capture)
- [Graphics device loss](#graphics-device-loss)
- [Errors](#errors)
- [Diagnostics](#diagnostics)

## The application and its thread

Create one `Application` on the UI thread before any window, and pass it to each
`Window`. It joins the thread to a single-threaded apartment and creates the dispatcher
queue, the compositor, and the graphics device. The thread must not already be in a
multithreaded apartment. Everything else in Composia belongs on this thread, except
`Application::post`, which queues work from any thread; `Application` and `Window`
operations called from another thread throw `std::logic_error`.

`Application::run()` runs the message loop for every window. It returns when the last
top-level window is destroyed, or with the exit code of a `WM_QUIT`. It routes keyboard
messages through the dialog manager, so Tab and Shift+Tab move between child windows
that are tab stops, as in a dialog. It also watches for graphics device removal while
idle (see [Graphics device loss](#graphics-device-loss)), and rethrows the first
exception that escaped a window's message handler or a posted callback.

To shut down, destroy the window objects and any Composition objects you hold, then call
`app.close()`. It drops posted callbacks that have not run, drains the dispatcher queue
while the graphics device still exists, releases the compositor and the device, and
rethrows a callback error not yet reported. The destructor closes an application that
was not closed, but can only log errors. The application must outlive its windows and
any thread that calls `post`. A thread can create another `Application` after one has
been destroyed.

Embed a manifest that declares PerMonitorV2 DPI awareness, such as
`resources/composia.manifest` (`COMPOSIA_MANIFEST` in CMake). Layout uses DIPs,
surfaces use physical pixels, and each window's visual tree applies the DPI scale.

## Windows

`Window` owns an HWND: a top-level window, or a child when given a parent HWND. Child
windows are tab stops. Derive from it and override the hooks you need: `on_paint`,
`on_resize` (which also runs after a DPI change), `on_hover`, `on_focus`,
`on_capture_lost`, `on_enabled`, `on_graphics_recreated`, `on_appearance_changed`, and
`on_message` for any raw message. `on_message` runs first; returning a result stops further
processing.

The window tracks the state every control needs, before `on_message` sees the message,
so an override can handle a raw message and still read the state:

- **Hover.** `hovered()` and `on_hover` report whether the pointer is over the client
  area. While the window has captured the pointer, Windows keeps sending moves from
  outside and reports no leave until the capture ends, so hover follows the pointer
  position then.
- **Capture.** `capture_pointer()` and `release_pointer()`, with `on_capture_lost` when
  another window takes the capture or `WM_CANCELMODE` cancels it, never for an explicit
  release. Destroying the window releases its capture.
- **Focus.** `focus()`, `focused()`, and `on_focus`. When a top-level window is activated
  again (after Alt+Tab, another window, or minimize and restore), it gives the focus back
  to the window inside it that last had it, as dialogs do, as long as that window is
  still visible and enabled. A multiline edit control that hands Tab on with
  `WM_NEXTDLGCTL` gets the same handling as in a dialog.
- **Enabled state.** `set_enabled()` and `enabled()`, where `enabled()` is false while any
  ancestor is disabled. Windows sends `WM_ENABLE` only to the window whose own state
  changed, so the window refreshes every Composia window below it, and `on_enabled`
  reports each change of the effective state. A disabled top-level window, such as the
  owner of a modal dialog, therefore disables its Composia controls too.
- **DIPs.** `pointer_position()` converts a client-relative mouse message to DIPs,
  `pointer_position_from_screen()` does the same for wheel messages, `scale()` is the DPI
  factor, `client_bounds()` is the client area in DIPs, and `set_bounds()` places a
  window in DIPs, rounding each edge to whole pixels.

Code that does not derive from the window, such as a parent watching a `Button`,
subscribes to the same changes with `on_hover_changed`, `on_focus_changed`,
`on_enabled_changed`, `on_pointer_capture_lost`, and `on_destroy`. Each signal fires after
the window's hook, and each returns a `Connection`: the callback runs for as long as the
connection lives.

After the native window is destroyed, by `DestroyWindow`, with its parent, or by the
`Window` destructor, `hwnd()` is null, `dpi()` is zero, and operations that need the
window throw `HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE)`.

## Drawing

`CompositionWindowTarget` puts Composition content in a window: a visual tree rooted at
`root()`, scaled for the window's DPI so that its children are laid out in DIPs, with a
canvas sprite at the bottom that shows a drawing surface the size of the client area.
Add your own visuals above the canvas through `root().Children()`.

`render(window, painter)` paints the canvas in one call. It sizes the surface to the
client area at the window's DPI, opens a drawing scope through `Application::render`
(which retries once after device loss), and passes the scope and the client size in
DIPs to the painter. It skips minimized and empty windows.

`invalidate()` schedules a repaint of the whole client area and
`invalidate(layout::Rect)` of a DIP rectangle, rounded out to whole pixels. Requests
coalesce into one `WM_PAINT`; while `on_paint` runs, `paint_rect()` reports the pixels
being repainted. Once the surface holds a complete frame, `render` called from
`on_paint` updates only that area: Composition keeps the rest of the surface, the scope
clips drawing to the area, and `ScopedSurfaceDraw::update_bounds()` reports it so a
painter can skip anything outside. A painter that always draws the whole scene stays
correct. A resize, a DPI change, or a replaced graphics device repaints everything.

`ScopedSurfaceDraw` is one balanced `BeginDraw`/`EndDraw` on any Composition drawing
surface. Its Direct2D context draws in DIPs from the surface's top-left corner, with the
surface's atlas offset already applied. Given an update rectangle in surface pixels, it
updates only that rectangle and clips drawing to it. `solid_brush(color)` hands out
brushes owned by the scope, created once per color, so nothing device-dependent outlives
the frame. Call `finish()` to see `EndDraw` failures; the destructor ends an unfinished
scope during exception unwinding. Do not nest scopes on one graphics device or call the
context's own `BeginDraw` or `EndDraw`.

`TextLayout` keeps a DirectWrite layout across frames, with a font family, weight, and
locale; `resize` sets the box it wraps in, and `metrics()` measures it. It needs only the
DirectWrite factory, so it survives device replacement.

`AnimationHelpers.hpp` has shortcuts for building a visual tree: `container` and `sprite`
visuals, `implicit_offset` (later offset changes animate), and `center_in_parent`
(an expression that keeps a visual centered as sizes change). Composition runs every
animation on its own; nothing in the application needs a timer.

## Content inside one window

Elements drawn into one window need no window of their own: they share its pointer
messages, capture, invalidation, and DIP conversions. Two things are per HWND in Windows:
keyboard focus and the UI Automation element. Drawn content that must be reachable with
Tab next to native controls, or exposed to screen readers, goes in a child `Window` with
an `Accessible`, as the slider in `examples/inputs` does. That child can still draw many
parts. Exposing those parts individually would need a UI Automation fragment provider,
which Composia does not supply.

## Accessibility

`Accessible` gives any window a UI Automation element, using only the window's public API:
the provider slot that answers `WM_GETOBJECT` (`set_automation_provider`) and the focus,
enabled, and destroy signals. Construct one with the window and its options: a name, a
control type, optionally an automation ID and a localized control type (UI Automation
expects one with `UIA_CustomControlTypeId`), an `invoke` callback for the Invoke pattern,
and `value` with an optional `setValue` callback for the Value pattern, which is
read-only without the callback.

The element reports focus and enabled changes, including those caused by ancestors, and
it is not focusable or invocable while disabled. UI Automation calls arrive on other
threads: properties are answered from state kept under a lock, and actions are posted to
the UI thread through the application, then dropped if the control is gone by then. Call
`set_name`, `set_value`, and `raise_invoked` to keep clients informed. The element
disconnects when the native window is destroyed or the `Window` object goes away,
whichever comes first; after that, `window()` is null and the provider reports the
element as unavailable. A window has at most one `Accessible`.

## Text input and native controls

There are two ways to take text input. The recommended one reuses the system's EDIT
control through `NativeControl`, so Windows supplies IME composition, selection, the
clipboard, keyboard conventions, and the UI Automation Text and Value patterns:

```cpp
composia::NativeControl name{window, L"EDIT", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL};
name.set_bounds({120, 96, 400, 32});
name.set_colors(RGB(234, 242, 244), RGB(14, 22, 31));
auto changed = name.on_command([&](UINT code) { if (code == EN_CHANGE) { /* name.text() */ } });
```

`NativeControl` creates any standard control class as a child of a `Window`, places it in
DIPs, and applies a font sized in points for the window's monitor (Segoe UI 9 pt by
default), rebuilt when the DPI changes. It delivers `WM_COMMAND` notification codes to
`on_command` and `WM_NOTIFY` headers to `on_notify`. Windows does not move child windows
when the DPI changes, so set the bounds again from `on_resize`, as `examples/inputs` does.
`text`, `set_text`, `show`, `set_enabled`, `focus`, and `send` cover the common operations,
and `hwnd()` serves everything else, such as `EM_SETCUEBANNER` or `BM_GETCHECK`. The
control is destroyed with its wrapper or with the parent window, after which calls throw
`ERROR_INVALID_WINDOW_HANDLE`.

`set_colors` answers the control's `WM_CTLCOLOR*` requests with the given text and
background colors, to match a composition-drawn surface. Edit fields, static text, list
boxes, and combo boxes (their edit field and drop-down list included) honor both colors.
Visual styles draw the text of check boxes, radio buttons, and group boxes in the theme's
color, so `set_colors` turns visual styles off for those controls, which then take the
classic look, and `clear_colors` restores them. Push buttons draw with system colors and
use the background only around their edges; use `BS_OWNERDRAW` or a `Button` for colored
ones.

Notifications travel through a public route: `Window::set_notification_handler` registers
a `NotificationHandler` for a child control's HWND, and the parent asks it about
`WM_COMMAND`, `WM_NOTIFY`, `WM_CTLCOLOR*`, `WM_DRAWITEM`, `WM_HSCROLL`, and `WM_VSCROLL`
from that control or from windows inside it. `NativeControl` uses this route, and so can
wrappers for other controls. The parent asks after its own `on_message`, so an override
that answers `WM_COMMAND` for every source also silences hosted controls; return
`std::nullopt` for messages you do not handle.

Child HWNDs always draw above the parent's composition content, so hosted controls sit on
top of the canvas and cannot be clipped, transformed, or animated by Composition. Embed a
common-controls version 6 manifest dependency, as `resources/composia.manifest` does, for
the themed look.

The other way is a composition-drawn field, for when rendering must be custom: a child
`Window` that draws its text and caret and handles keyboard input itself. A
product-quality field then needs selection, IME composition, and accessibility of its
own. An `Accessible` with the Value pattern exposes the text, but screen readers also need
the Text pattern for the caret, selection, and moving through the text, which Composia
does not provide.

## System appearance

Composia leaves the look of an application to the application, and tells it what Windows
asks for. `Application::appearance()` is an `Appearance`:

- `dark`: the app mode in Settings is dark.
- `highContrast`: a contrast theme is on. Draw with `colors` and nothing else then; users who
  turn it on rely on those colors to read the screen.
- `accent`, `textScale` (the accessibility text size, 1 to 2.25), and `animations` (whether
  animation effects are on).
- `colors`: the system colors, such as `window`, `windowText`, `buttonFace`, `highlight`, and
  `grayText`, as 0xRRGGBB like every color `ScopedSurfaceDraw::solid_brush` takes.

When the user changes a setting, Windows tells the top-level windows, and `UISettings` reports
changes to colors and text size. The application reads the settings again and, only if they
changed, runs every window's `on_appearance_changed`, invalidates every window, and then runs
the `Application::on_appearance_changed` subscribers. A window that paints from
`appearance()` is therefore repainted with the new settings without code of its own.

The title bar and frame belong to Windows. `Window::set_dark_title_bar(bool)` draws them dark
or light; Composia does not follow the system by itself, so call it with
`appearance().dark`, in the constructor and in `on_appearance_changed`, as `examples/hello`
does. With "Show accent color on title bars" on, Windows draws the accent instead.

## Button

```cpp
composia::Button close{window, L"Close"};
close.set_bounds({24, 24, 160, 44});
auto clicked = close.on_click([&] { PostMessageW(window.hwnd(), WM_CLOSE, 0, 0); });
```

`Button` is a child window built only on the public mechanisms above. It owns the behavior:
pointer capture and cancellation, Space and Enter, Tab and Shift+Tab focus, its enabled state
(including a disabled parent), and an `Accessible` with the button role and the Invoke pattern.
`set_label` changes the label and the UI Automation name.

How it looks is up to its painter, a function that draws the whole client area from a
`Button::State`: the label, already laid out and centered, the appearance, and whether the
button is hovered, pressed, focused, and enabled. A child window cannot show its parent through
it, so the painter fills every pixel, including any corners outside a rounded shape:

```cpp
composia::Button save{window, L"Save", [](composia::ScopedSurfaceDraw& draw, composia::numerics::float2,
                                         const composia::Button::State& state) {
    draw.context()->Clear(D2D1::ColorF(state.pressed ? 0x005A9E : 0x0078D4));
    draw.context()->DrawTextLayout({0, 0}, state.label.layout().get(), draw.solid_brush(0xFFFFFF));
}};
```

Without a painter, `Button::paint_default` draws a flat button with a border and a focus
outline in neutral light or dark colors, or in the system colors under high contrast; a painter
can call it and draw over it. `set_painter` replaces the painter. `examples/inputs` gives its
button its own rounded look.

## GPU content

Composia composes; it does not render 3D or run a render loop. An application draws with
Direct3D 11, through `app.graphics().d3d_device()` and `d3d_context()` and any library it
likes, into one of two surfaces that sit in the visual tree, where Composition clips,
transforms, animates, and composes them like any other visual:

- `SwapChainSurface` is a DXGI swap chain made for Composition, for Windows 10 and later.
  Put its `brush()` on a sprite visual sized to it in DIPs, resize it in pixels with the
  visual, and call `present` with a callback that draws into the back buffer.
- `TextureSurface` wraps an `ID3D11Texture2D` as a composition texture, with no copy. It
  needs a recent Windows 11 runtime and driver support, which `TextureSurface::supported`
  reports. Before writing a texture, check `available()`, which polls the compositor's
  availability fence, and rotate several textures to avoid waiting. Availability does not
  synchronize other threads writing the same texture.

```cpp
composia::SwapChainSurface frame{app, {1280, 720}};
sprite.Brush(frame.brush());
frame.present([&](ID3D11RenderTargetView* target, ID3D11Texture2D*) {
    const float clear[]{0.1f, 0.1f, 0.1f, 1};
    app.graphics().d3d_context()->ClearRenderTargetView(target, clear);
    // Draw the scene into target here.
});
```

Neither surface runs a render loop or a frame clock. Composition keeps showing the last
frame and runs its own animations without new frames, so present only when the content
changes: after input, new data such as a decoded video frame, a resize, or a device
replacement. Content that changes continuously brings its own timing, such as its video
source's frame events. Direct3D objects belong to the device that made them: after a
device replacement, `SwapChainSurface` rebuilds its swap chain, empty, on the next
`present` or `resize`; recreate your own textures and other device objects, and present
again from the repaint that recovery triggers. `examples/gpu` does all of this.

## Virtual surfaces

A [`CompositionVirtualDrawingSurface`](https://learn.microsoft.com/en-us/uwp/api/windows.ui.composition.compositionvirtualdrawingsurface)
can be far larger than any bitmap: 1,048,576 pixels square would need 4 TiB as an
ordinary BGRA bitmap. `VirtualSurface` draws only the tiles covering a viewport plus one
tile of overscan, then trims everything else from the surface, so scrolling away releases
old regions and coming back redraws them. It suits maps, document pages, and image
editors.

```cpp
composia::VirtualSurface document{app.graphics(), {1000000, 1000000}};
app.render([&] {
    document.update({0, 0, 1200, 800}, [](composia::ScopedSurfaceDraw& draw, const RECT& tile) {
        // Draw the document region `tile` at local (0, 0); it starts transparent.
        draw.context()->Clear(D2D1::ColorF(0xFFFFFF));
    });
});
auto brush = app.compositor().CreateSurfaceBrush(document.surface());
```

Sizes, viewports, and tiles are in document pixels, independent of DPI; the painter draws
in tile-local pixels at 96 DPI, which keeps precision at distant coordinates. Show the
surface through a surface brush with `Stretch(None)` and zero alignment ratios, and scroll
and zoom with the visual's offset and scale. `invalidate(rect)` marks tiles for repainting
by the next `update`; `clear()` releases every tile; `resize()` changes the document size
and releases every tile. A device replacement invalidates the cache on the next `update`.
Call `update` on the UI thread, inside `Application::render`, and do not reenter the
surface from the painter.

Dimensions are at most 2²⁴ pixels. Tiles are 64 to 2048 pixels square, 512 by default, and
one viewport may retain at most 256 tiles by default; a larger viewport throws
`HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY)` before changing anything, so an application
that zooms out must keep its viewport within the budget. `cached_tiles()` and
`retained_pixel_bytes()` report what the surface keeps; the byte count estimates the BGRA
content, not GPU memory, which includes driver allocation granularity and compositor
overhead.

## Screen capture

`ScreenCapture` wraps the
[Windows Graphics Capture](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture)
frame pool and session. It takes a `GraphicsCaptureItem` from the system picker, from
`IGraphicsCaptureItemInterop` for a window or monitor, or from
`GraphicsCaptureItem::CreateFromVisual`, polls a free-threaded frame pool with
`next_frame()`, and follows the target's size. Frames are BGRA textures on the device
passed to `start`.

To show captured content, copy each frame on the GPU into a texture of your own before
closing the frame; that keeps the capture's buffers independent of the compositor. Close
every frame before polling again, recreating, or stopping. After a device replacement,
call `recreate` with the new device. `target_closed()` reports a target that went away;
the closed callback only sets that flag and never touches a window. Call everything on
the UI thread. Capture is an SDR BGRA source: it does not record files, capture audio, or
tone-map HDR displays. `ScreenCapture::supported()` reports whether the system supports it.

## Graphics device loss

The application registers for Direct3D device removal and watches for it alongside the
message queue, also while idle. `Application::render` runs a drawing operation and, if it
fails with device loss, replaces the device and runs it once more; a second loss, or any
other error, propagates. `CompositionWindowTarget::render` and `SwapChainSurface` draw
through it; call `VirtualSurface::update` and your own drawing inside it.

`GraphicsDevice::recreate` replaces the Direct3D and Direct2D devices and moves the
Composition graphics device onto them, so visuals and surfaces survive and only their
content must be drawn again. It prepares the new device and its removal registration
before switching Composition; if anything fails, nothing changes. On success the
generation advances, `GraphicsDevice::on_recreated` subscribers and each window's
`on_graphics_recreated` run, and every window is invalidated. Recreate device-dependent
objects you own there. Holding a device pointer does not extend its life past a
replacement.

## Errors

Composia reports failures with exceptions:

- Windows and graphics failures throw `wil::ResultException`, or `winrt::hresult_error`
  from C++/WinRT calls, carrying the `HRESULT`. Invalid arguments are `E_INVALIDARG`;
  operations on a destroyed window are `HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE)`.
- Misuse of lifetimes or threads, such as a call from another thread, closing the
  application while windows exist, or a second `Accessible` on a window, throws
  `std::logic_error`. `layout::stack` throws `std::invalid_argument`.
- An exception from a message handler cannot cross the window procedure. The window
  keeps it (`Window::rethrow_callback_error` rethrows it) and reports it to the
  application, whose `run()` rethrows it. Exceptions from posted callbacks reach `run()`
  or `close()` the same way.

Destructors never throw; they log what they cannot report.

## Diagnostics

Composia reports its diagnostic events, such as graphics device creation and the fallback
to WARP, device loss and removal, appearance changes, and shutdown, as `event=name key=value` text at an info
or warning level. They go to `OutputDebugString` unless `set_log_handler` in
`composia/Log.hpp` routes them elsewhere, such as an application's own log. Set the handler
before creating the `Application` to see every event; Composia depends on no logging
library.
