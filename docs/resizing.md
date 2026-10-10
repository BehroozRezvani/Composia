# Resizable composition panes

Use shared targets for panes that move together inside a window. They keep pane positions,
rectangular clips, backgrounds and drawing surfaces in one composition tree. The HWNDs still
provide input, focus, accessibility and native-control hosting.

Create the top-level window and each composition pane with `Window::Content::composition`.
Create their targets with `CompositionWindowTarget::Hosting::shared`, parent first:

```cpp
class Pane : public composia::Window {
public:
    Pane(composia::Application& app, HWND parent = nullptr)
        : Window(app, L"Pane", 640, 480, Content::composition, parent),
          target_(*this, composia::CompositionWindowTarget::Hosting::shared) {}

private:
    void on_resize() override { invalidate(); }
    void on_paint() override {
        target_.render(*this, [](composia::ScopedSurfaceDraw& draw, composia::numerics::float2) {
            draw.context()->Clear(D2D1::ColorF(0x202830));
        });
    }
    composia::CompositionWindowTarget target_;
};
```

The composition creation mode applies `WS_EX_NOREDIRECTIONBITMAP` before HWND creation. A
shared target rejects an incompatible HWND, a second shared host, or a child whose immediate
parent has no shared target. It manages the transparent child style and prevents copying old
composition pixels during native moves. `Button` joins a shared parent automatically; under a
standalone parent it keeps its own target.

## Finish layout and painting together

Call `Application::update` from a resize or splitter handler:

```cpp
app.update(mainWindow, [&] {
    list.set_bounds({0, 64, divider, height - 64});
    reader.set_bounds({divider, 64, width - divider, height - 64});
    // Invalidate any content whose data changed without changing its size.
    list.invalidate();
});
```

The callback finishes layout first. Composia invalidates the root, synchronously paints pending
regions in its same-thread subtree, and then commits the composition update. Nested calls join
the outer update. Calls to `commit()` during the operation are deferred, including calls from
paint callbacks. A layout or paint exception propagates without committing the incomplete
operation. Native window changes are not rolled back. An update that continually requests more
updates from painting is rejected rather than looping forever.

Shared targets invalidate themselves on size and DPI changes. Composia retains their pending
paint requests even when Win32 defers a transparent child's paint behind a sibling. If device
recovery invalidates an already painted pane, the update paints it again before committing.

`commit()` alone only submits prepared visual changes; it does not perform layout or paint
pending invalid regions. Avoid pumping messages, waiting for input, or doing slow work inside an
update. Those actions can expose partially changed native windows or delay the UI thread.

## Native text and compatibility

For an EDIT embedded beside moving composition panes, request an opaque native buffer:

```cpp
composia::NativeControl search{mainWindow, L"EDIT", WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
    L"", 0, composia::NativeControl::Painting::buffered_edit};
```

This mode is restricted to EDIT controls. It keeps their pixels through resize and avoids a
separate background erase. Text editing, selection, undo, IME and accessibility stay native.
Embed Composia's supplied application manifest, as with the other native-control examples.
Other controls retain their normal painting behavior. Windows owned by another thread or
process, such as a browser, retain their own update schedule.

Shared child targets support rectangular HWND regions. Nonrectangular regions and reparenting
are reported as errors; recreate the pane under its new parent instead. Destroy child targets
before the parent target, and targets before their Window and Application. Native parent-window
destruction removes the host registrations even if the C++ child objects still exist.

Use `Hosting::standalone` or the original compositor/device/HWND constructor when an independent
target is intentional. Raw HWNDs and composition objects remain available, but direct changes
to their styles, visual parenting or presentation bypass the shared-pane contract. Native HWND
painting and composition are separate systems: this API does not promise a transaction across
arbitrary native controls, external renderers or long-running application callbacks.

## Regression coverage

`pane-*` tests cover rejected setup, destruction, nested updates, errors, rectangular clipping,
sibling order, target DPI scaling, and full-range resizing. Whole-window captures include a
populated pane, a shared Button with hover updates, and a native buffered EDIT. They check the
reader edge for copied content, compare search-text pixels, and verify that an explicit commit
inside an unfinished update cannot expose temporary geometry. Hardware and WARP variants run
the same checks. DPI geometry checks exercise 96, 120, 144 and 192 DPI; capture uses the current
desktop's actual scale. Real monitor transitions remain a separate desktop qualification.
