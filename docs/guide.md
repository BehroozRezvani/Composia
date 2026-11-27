# Composia guide

This guide explains how Composia's parts work together and what each one expects. The
public headers in `include/composia` document every function; the examples in
`examples/` show the pieces in use.

For splitters and moving child panes, follow the [shared-pane update contract](resizing.md).
It covers composition window creation, coordinated painting, and buffered native text fields.

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

Keyboard accelerators go through `on_key_preview`. `Application::run` offers each key
press (`WM_KEYDOWN` and `WM_SYSKEYDOWN`) to the previews of the window it is aimed at and of
every window that contains it, innermost first, before the dialog manager, the focused
control or a retained tree sees it. A handler that returns true consumes the key, so it
produces no character and Tab or Enter do nothing else:

```cpp
auto shortcuts = window.on_key_preview([&](const composia::KeyEvent& key) {
    if (key.control && !key.shift && !key.alt && key.key == 'N') { new_message(); return true; }
    return false;
});
```

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
animation on its own, so no animation needs a timer. For a wait, such as a delay before saving or
a tooltip's pause, `Timer` (`Timer.hpp`) runs a callback on the UI thread from the dispatcher
queue: never after `stop()` or its destruction, with its errors rethrown by `run()` as a posted
callback's are, and without the `WM_TIMER` a window procedure would have to tell apart from its own.

## Content inside one window

`ElementWindow` hosts a tree in a top-level window or a child pane. It sizes the root
to the client area, forwards mouse and keyboard input, and schedules partial paints.
Pass a parent HWND to place a pane in the tab order beside native controls.
For a window with a native Win32 menu bar, pass `Window::Content::native` before
the root element. Windows can then paint the menu into the window's backing bitmap
while Composia renders the document into a standalone Composition target.

```cpp
#include <composia/ElementWindow.hpp>
#include <composia/ScopedSurfaceDraw.hpp>

class Swatch : public composia::Element {
public:
    explicit Swatch(unsigned rgb) : rgb_(rgb) {
        set_preferred_size({64, 32});
        set_focusable(true);
    }
private:
    void paint(composia::ScopedSurfaceDraw& draw) override {
        const D2D1_RECT_F box{0, 0, bounds().width, bounds().height};
        draw.context()->FillRectangle(box, draw.solid_brush(rgb_));
        if (focused()) { draw.context()->DrawRectangle(box, draw.solid_brush(0xffffff), 2); }
    }
    unsigned rgb_;
};

// Inside an application, keep the window alive through app.run().
using namespace composia;
auto row = std::make_shared<Element>(ElementLayout{ElementLayout::Kind::flex, layout::Axis::horizontal, 8});
for (unsigned rgb : {0x005a9eu, 0x28754eu, 0x884477u}) {
    auto swatch = std::make_shared<Swatch>(rgb);
    swatch->set_flex_grow(1);
    row->add(swatch);
}
ElementWindow window{app, L"Swatches", 320, 80, row};
window.show();
```

`ElementTree` retains custom-drawn elements on one surface. Each parent owns its
children through `shared_ptr`. Remove a child before adding it elsewhere. Work with the
tree on the UI thread. Override `measure_content` for intrinsic sizing such as wrapped
text, `paint` for drawing in local DIPs, `paint_over` for drawing above the children, and
`on_pointer` or `on_key` for input. `claims_pointer` lets an element take pointer input
at a point before its children there, as a scrollbar over content does. An
unbounded measurement axis is positive infinity. Return finite, nonnegative dimensions.

| Container | Placement |
| --- | --- |
| Overlay (default) | Each child fills the parent. Higher z indices paint later. Ties follow insertion order. |
| Canvas | The canvas supplies its intrinsic extent. Children keep their measured size and use their offsets as coordinates. |
| Stack | Children take their measured length and stretch across the other axis. Overflowing lengths shorten at the far edge. |
| Flex | A single row or column. Extra space follows each child's flex-grow weight. A deficit shrinks children in proportion to their measured lengths. |
| Grid | Equal-width columns in insertion order. Each row takes the height of its tallest child. The gap applies in both directions. |
| Scroll | One child measured without a limit on the scrolling axis. The viewport clips its content and clamps the offset. Nest scroll containers for two axes. |

A right click, Shift+F10 or the menu key reaches the tree as `on_context_menu`. It starts at
the element under the pointer, or at the focused element for the keyboard, and bubbles like
other input until a handler returns true. The position is local to each recipient: the
pointer, or for the keyboard the lower-left corner of the focused element's visible part.
An unhandled request goes on to the parent window, as `DefWindowProc` sends it. Custom
hosts call `ElementTree::context_menu`.

`set_tooltip` gives an element a tooltip, which its `ElementWindow` shows in a native tooltip
window: below the pointer once it rests on the element, or on a descendant without a
tooltip of its own, for the system's hover time, and below the element when keyboard focus
reaches it. A press, a key, the wheel, or the pointer leaving hides it, and it shows again
only after the pointer or focus moves on. The text is also the element's UI Automation help
text when its accessibility description has none, so icon-only controls are described.

Every container insets its children by its `padding` (`layout::Insets`, or `Insets::all(n)`
for the same space on each edge). The padding adds to the container's intrinsic size, so a
padded element without children is as large as its padding, and a scroll viewport scrolls its
content inside it.

`set_offset` translates a whole subtree after layout. Every ancestor clips both drawing
and hit-testing. Hover follows the pointer, and after each layout the tree finds the element
under the pointer's resting place again, so content that appears, goes or moves under a still
pointer, as when rows are replaced or a view scrolls, is hovered without the pointer moving. Pointer and key events bubble up the original path until a handler
returns true. Pointer positions are relative to each recipient. Input handlers can remove
elements during dispatch. Layout and paint hooks must leave tree properties and structure
alone. A painter can use the supplied surface's brushes and must balance any drawing
state it changes.

`set_focusable(true)` adds an element to keyboard navigation. A left press focuses the
nearest focusable ancestor, including the hit itself, unless that element called
`set_focus_on_press(false)`, as a splitter does: the press then leaves the focus where it was,
in the tree or in a window it hosts. Tab and Shift+Tab traverse insertion
order and scroll a focused descendant into view. At a scroll boundary, wheel input
continues to the enclosing container. Individual drawn elements have logical keyboard
focus. Call `window.focus_element(element)` to give a drawn control both Windows and retained
keyboard focus. Disabling a retained ancestor clears focus inside it and takes the subtree
out of pointer interaction and Tab traversal.

For a custom host, pass a callback to the tree that queues an update. Set the viewport
in DIPs at resize, then call `take_dirty_bounds()` in the queued update and invalidate
the returned window rectangle. Hit-testing, painting, and taking damage resolve pending
layout. Call `tree.paint(draw)` inside the target's render callback. This reconstructs
the surface's actual update area, including exposed layers after a move and full frames
after device loss. Use `invalidate(localRect)` for paint changes and
`invalidate_measure()` when custom content changes its desired size.

## Lookless controls

Link the optional controls target and include `<composia/Controls.hpp>`. The installed
package exports the same target as a source build. Configure Composia with
`-DCOMPOSIA_BUILD_CONTROLS=OFF` for a core-only build and installation.

```cmake
find_package(Composia CONFIG REQUIRED COMPONENTS Controls)
target_link_libraries(my_app PRIVATE Composia::Controls)
```

The controls live in `composia::controls`. Every control raises `on_context_requested` for
a context menu request on it or inside it, and handles the request while a callback is
connected. A renderer receives the drawing scope, control,
current `State` and the control's `StyleTokens`. It draws in local DIPs. Set the control's
preferred size for your design. Focus, hover, press, selected state and disabled state
are available on every `Control`. The retained textbox keeps its Rich Edit painter and
text operations. Use `state_of(box)` in that painter and call `draw_text` and `draw_caret`.

Composia draws nothing for a control without a renderer. The tokens hold the colors for the
foreground, background, accent and the text on it, hover, pressed, border, focus ring and
disabled content, with padding, corner radius and font size. A control passes its renderer
its own tokens, else those of the nearest ancestor control or `StyleScope` that has some,
else `StyleTokens{}`. Wrap content in a `StyleScope` to theme everything inside it at once.
`StyleTokens::from` turns the system appearance into tokens, dark mode, the accent color,
the text size and high contrast included, for an application that chooses to follow it:

```cpp
namespace ui = composia::controls;
auto scope = std::make_shared<ui::StyleScope>(ui::StyleTokens::from(app.appearance()),
    composia::ElementLayout{composia::ElementLayout::Kind::stack});
auto follow = app.on_appearance_changed([&](const composia::Appearance& appearance) {
    scope->set_tokens(ui::StyleTokens::from(appearance));
});
```

| Control | Interaction |
| --- | --- |
| Button | A left release over the pressed button, Space, Enter or UIA Invoke runs its command. Losing capture cancels a pointer press. |
| Toggle | Pointer release, Space, Enter or UIA Toggle changes the checked state. `on_change` also observes programmatic changes. |
| Slider | Drag on either axis, including past its ends. Arrows, Home, End and Page Up/Down adjust the same value exposed by RangeValue. Vertical sliders increase upwards. |
| TextBox | The retained editor handles text selection, clipboard, undo, IME and UIA Text. |
| ScrollView | Set its content, axis and preferred viewport length. Wheel, arrows, paging and UIA Scroll share its offset. While the content overflows, a scrollbar over the far edge widens under the pointer; drag its thumb, or press its track to page. `set_scrollbar_renderer` draws it from its track, thumb and state, instead of the thin bar in the foreground color. `set_scrollbar(false)` hides it. |
| List | One tab stop supports arrows, paging, Home/End, prefix search and single selection. Double-click or Enter activates the current row. A right click moves to the row under it before `on_context_requested` runs; for the keyboard the request is placed below the current row. The current row is the selected one, except on rows that `Items::selectable` refuses, such as headings: the keyboard and the pointer rest there without selecting them. |
| Dropdown | Open with a press, Enter, Space, F4 or an arrow. Preview with arrows or typing. Click or Enter commits. Escape discards the preview. |
| Menu | The popup uses menu-item roles, skips disabled commands during navigation and dismisses before executing a choice. |

Wrap the window content in one `PopupHost` to host dropdowns and menus above sibling
controls. It places each popup below or above its trigger according to the available
space, clips it to the host and dismisses it on outside press, focus loss or Tab.
The trigger exposes ExpandCollapse and a relationship to the open list.

```cpp
namespace ui = composia::controls;
auto device = std::make_shared<ui::Dropdown>(
    L"Output device", ui::Items::strings({L"Headphones", L"Speakers"}));
device->set_selected(0);
device->set_renderer(draw_device_field);
device->set_row_renderer(draw_device_row);
auto selection = device->on_selection([&](ui::List::Index index) {
    if (index) { select_output_device(*index); }
});
auto content = std::make_shared<composia::Element>(
    composia::ElementLayout{composia::ElementLayout::Kind::stack});
content->add(device);
auto root = std::make_shared<ui::PopupHost>(content);
composia::ElementWindow window(app, L"Audio output", 360, 240, root);
```

`Command` combines an action with observable availability. Share a command between buttons
and menu entries. Its availability combines with inherited disabled state. `Observable<T>`
publishes unequal values on the UI thread. Change and activation subscriptions return a
`Connection` whose lifetime controls the subscription.

A list becomes a tree when its `Items` give each row a `level` and, for rows with children, an
`expanded` state. The rows stay flat, in order, with each row's children after it: the owner
keeps the tree and lists the rows that show. Right asks to open a folded row or moves to the
first child of an open one, and Left asks to fold an open row or moves to the parent.
`on_expand` receives those requests, and UI Automation's ExpandCollapse, on a tree item that
also reports its level; the owner changes its rows and sets them again. The list does not fold
rows for the pointer: a renderer draws the expander, and the owner toggles from `on_activate`
or from a control in the row.

`Items::content` puts elements over a row each time it is realized, such as a button inside
it. They draw above the row renderer and take the pointer first; a press they leave selects
the row. `index_at` finds the row at a point in the list, for a drop target, and `refresh`
reads a row's label again and repaints it after a change that keeps its height.
`set_overlay` lays a `HostedVisual` over all the rows: it scrolls with them, and `row_bounds`
gives each row's place in it, so an application can glide an indicator from row to row with a
compositor animation on its own visual.

For large lists, supply an `Items` count and indexed label callback. The list realizes
the rows covering the viewport, including partially visible rows, and retains
the selected row when it scrolls offscreen. Rows take the list's row height, or each its own
from an `Items::height` callback, which the list reads for every row when the items are set;
paging moves by the viewport's height either way. Existing rows keep their UIA identity while
realized. For indexed data sources, supply `findPrefix` to search directly. The fallback
scans labels when the user types. Call `set_items` when the backing collection changes, and
`replace_items` when the rows change under a view that stays, such as a tree row opening: it
keeps the scroll position and takes the selected and current rows from the owner.
The control owns the child elements used for realization and popup placement.

## Accessibility

### Controls drawn into an ElementWindow

Include `<composia/ElementAccessibility.hpp>` and call `set_accessibility` on each element
that represents a control or a meaningful group. The nearest described ancestor becomes
its UI Automation parent. Layout-only wrappers are transparent. A screen reader can walk
from a group to a custom slider and a native Rich Edit text range on the same surface.
Fragments retain their identity while attached. Removing an element disconnects its
provider, including patterns already held by clients.

```cpp
using namespace composia;
// VolumeControl derives from Element and draws the current range value in paint().
void VolumeControl::initialize() {
    set_focusable(true);
    ElementAccessibility info{.name = L"Volume", .automationId = L"volume",
                              .controlType = UIA_SliderControlTypeId};
    info.value = ElementAccessibility::RangeValue{25, 0, 100, 1, 10,
        [this](double level) { set_accessible_value(level); }};
    set_accessibility(std::move(info));
}
```

Keyboard focus, pointer hit-testing, UIA hit-testing and screen bounds use the retained
tree's arrangement, clipping and window DPI. Tab traversal reveals a focused item inside
a scroll container. A disabled control remains discoverable. Its enabled ancestors and
host window determine whether it can take focus or perform an action.

| Pattern | How to expose it |
| --- | --- |
| Invoke | Set `invoke` to the control's action. Enter and Space also activate a focused control. |
| Value | Set `value` to an `ElementAccessibility::Value` containing its current text and an optional setter. |
| RangeValue | Supply the current value, limits and small/large increments. Arrow keys, Page Up/Down and Home/End use the same setter as automation. |
| Level | Set `level` to an item's depth in a tree or outline, from 1. |
| Selection and SelectionItem | Give a container a `Selection`, and give each item an initial `selected` state. Use `select_item` from pointer handlers. Space selects an item and Ctrl+Space adds or removes it. Selection operations enforce the container's single/multiple and required rules and call its `changed` callback. Nested selection containers manage their own items. |
| Scroll | Describe an element with scroll layout. UIA reads and changes the same horizontal or vertical offset used by the wheel and layout. |
| Text | Use a `TextBox`, including read-only mode, for native ranges, selections and screen geometry. A custom document renderer can supply an `ITextProvider` through `textProvider`. |

Setters run on the application's UI thread. Use `set_accessible_value` for a value change
and `set_accessibility` to replace the description. A required group selects its first
remaining item if its selection disappears. Omitting a value setter makes
the value read-only. A control's own `on_key` can handle a key before the default action.
`labeledBy`, `describedBy` and `controllerFor` are weak relationships to accessible elements
in the same window. Provide a translated `localizedControlType` for a custom role.

### Window-based controls

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

`TextBox` paints directly into an `ElementWindow`. It takes part in retained layout,
clipping, z order and Tab navigation. Windows' windowless Rich Edit engine handles
Unicode shaping, IME composition, pointer selection, keyboard commands and undo history.
Use the accessible name to identify each field to screen readers.

```cpp
#include <composia/ElementWindow.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <composia/TextBox.hpp>

using namespace composia;
auto body = std::make_shared<TextBox>(
    TextBox::Options{.name = L"Message", .multiline = true, .fontSize = 18,
                     .foreground = RGB(246, 248, 250), .background = RGB(27, 42, 54)},
    L"Draft message");
ElementWindow window{app, L"Compose", 460, 200, body};
auto changed = body->on_text_changed([&] { save_draft(body->text()); });
```

The editor wraps multiline text to its arranged width and scrolls to keep the caret
visible. Selection offsets count UTF-16 code units, with one carriage return per
paragraph. `select(0, -1)` selects the document. `replace_selection` participates in
undo and redo. `set_text` installs a new document and clears its history. Attached
editors coalesce text change callbacks on the application thread. `set_read_only(true)`
keeps selection and copying available.

The default painter uses the font, padding and colors in `Options`. A custom painter
can draw its own chrome, then call `draw_text` for the shaped glyphs, selection and IME
decorations, and `draw_caret` for the blinking insertion point. `caret_bounds()` and
`selection_rectangles()` expose local DIP geometry. Keep the text origin aligned with
the element so pointer, IME and accessibility coordinates agree.

```cpp
body->set_painter([](ScopedSurfaceDraw& draw, TextBox& box) {
    const auto r = box.bounds();
    draw.context()->FillRoundedRectangle({{0, 0, r.width, r.height}, 8, 8},
        draw.solid_brush(0x1B2A36));
    box.draw_text(draw);
    box.draw_caret(draw);
});
```

An `ElementWindow` exposes its editors as UI Automation fragments. Clients can read,
select and navigate native text ranges or edit the Value. Applications that install
their own provider on the window take ownership of its automation tree.

For interfaces built from child windows, `NativeControl` can host a standard EDIT:

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

### Child windows in a retained tree

A `HostedWindow` gives a child window a place in an `ElementWindow`'s tree, for content
that keeps its own HWND: a native control, a Composia `Window` that draws on its own, or a
browser's host window. Create the window as a child of the `ElementWindow`, then add the
element where the window belongs:

```cpp
#include <composia/HostedWindow.hpp>

ElementWindow window{app, L"Settings", 480, 320, root};
NativeControl search{window, L"EDIT", WS_TABSTOP | WS_VISIBLE | ES_AUTOHSCROLL};
auto slot = std::make_shared<HostedWindow>(search.hwnd());
slot->set_preferred_size({0, 32});
form->add(slot);
```

After each layout the window moves to the element's bounds. Ancestors clip it with a
rectangular window region, which shared composition targets follow, so a hosted window
scrolls with its container. It is hidden while it is clipped out of view, while its element
is detached, and before the element is first placed, and it is disabled with the element.
Place it nowhere else yourself: the next layout moves it back.

The element is a tab stop in insertion order. When Tab, Shift+Tab or `focus_element`
reaches it, the window takes the keyboard focus, and Tab in the window continues with the
next element, or the previous one with Shift. A window that asks for Tab through
`WM_GETDLGCODE` keeps it. A window that moves the focus out by other means, such as a
browser raising `MoveFocusRequested`, or whose focus rests in a window inside it, calls
`leave(backwards)`.

### Composition content in a retained tree

A `HostedVisual` gives a Composition visual a place in the tree, for content the application
builds and animates in the compositor: a progress bar, an indicator that glides from row to
row, or a picture of part of the tree during a transition. The tree keeps the visual over the
element's bounds as layout and scrolling move it, clips it by the element's ancestors, and
hides it while the element is out of view or detached. The visual's parent takes the
element's size, so `RelativeSizeAdjustment({1, 1})` fills it. It lies above the tree's own
drawing, and the pointer passes through it to the elements underneath.

```cpp
#include <composia/HostedVisual.hpp>

// A busy bar that sweeps along a 3 DIP strip, in the compositor, with no render loop.
auto track = app.compositor().CreateContainerVisual();
auto bar = app.compositor().CreateSpriteVisual();
bar.Size({60, 3});
bar.Brush(app.compositor().CreateColorBrush(accent));
track.Children().InsertAtTop(bar);
auto sweep = app.compositor().CreateScalarKeyFrameAnimation();
sweep.InsertKeyFrame(0, -60);
sweep.InsertKeyFrame(1, 240);
sweep.Duration(std::chrono::seconds{1});
sweep.IterationBehavior(composia::composition::AnimationIterationBehavior::Forever);
bar.StartAnimation(L"Offset.X", sweep);
auto progress = std::make_shared<composia::HostedVisual>(track);
progress->set_preferred_size({0, 3});
```

`ElementWindow::snapshot(area)` pictures an area of the tree as it draws now, in a sprite with
a surface of its own, for a transition: put it in a `HostedVisual` over the area, change the
tree underneath, and animate the picture away in the compositor (sliding rows apart, fading
out a page), then drop it. It holds the tree's own drawing only and keeps what it showed.
Custom hosts paint an area with `ElementTree::paint(draw, area)`.

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
