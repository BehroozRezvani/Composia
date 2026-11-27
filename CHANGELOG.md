# Changelog

Composia follows [semantic versioning](https://semver.org). Before 1.0, a minor version may
change the API; each change is listed here.

## 0.4.0 (unreleased)

### Added

- `ElementLayout::padding` insets a container's children by `layout::Insets` and adds to its
  intrinsic size.
- `HostedWindow` places a child window over a retained element: it follows layout and
  scrolling, is clipped by its ancestors, hides when clipped out or detached, is enabled
  with the element, and takes its turn in Tab order (`leave` hands the focus on).
- `Window::on_key_preview` gives key presses aimed at a window or anything inside it to
  handlers before the dialog manager and the focused control, for accelerators.
  `PointerEvent` and `KeyEvent` moved to `composia/Input.hpp`, which `Element.hpp` includes.
- `StyleTokens` adds accent foreground, hover, pressed, border, focus and disabled colors
  and a font size for renderers. `StyleTokens::from(Appearance)` derives tokens from the
  system appearance, high contrast included, for applications that follow it. `StyleScope`
  gives the controls inside it the same tokens.
- `ScrollView`, and so `List`, draws an overlay scrollbar while its content overflows: thin
  at rest, wider under the pointer, with a draggable thumb and a track that pages.
  `set_scrollbar_renderer` draws it instead, from its track, thumb and state, and
  `set_scrollbar` hides it. Elements can draw above their children (`paint_over`) and take
  pointer input before them (`claims_pointer`).
- `Element::set_tooltip`: an `ElementWindow` shows the text in a native tooltip after the
  pointer rests on the element or keyboard focus reaches it, and serves it as the UI
  Automation help text when the element has none.
- Context menu requests reach elements: `Element::on_context_menu` and
  `ElementTree::context_menu`, with `ContextMenuEvent` telling a click from the keyboard.
  `ElementWindow` routes `WM_CONTEXTMENU`, controls raise `on_context_requested`, and a list
  selects the row under a right click first. `Signal::connected` reports a subscriber.
- `Items::height` gives each list row its own height; a list without it keeps one row height.
- `Items::selectable` marks rows that cannot be selected, such as headings. The keyboard and
  the pointer can still rest on them: `List::current` is that row, `go_to` moves there as the
  arrows do, and Enter and the keyboard's context menu request act on it.
- Lists can be trees: `Items::level` and `Items::expanded` describe the rows that show, Right
  and Left open and fold rows or move to a child or the parent, and `List::on_expand` asks the
  owner to change its rows. Tree items report their level and support ExpandCollapse.
  `ElementAccessibility::level` serves the UI Automation Level property.
- `List::replace_items` changes the rows under a view that stays: it keeps the scroll position
  and selects the row the owner gives without raising a selection change.
- `Items::content` places elements over a realized row, such as a button, ahead of the row for
  the pointer. `List::index_at` finds the row at a point and `List::refresh` reads a row's
  label again and repaints it.
- `HostedVisual` places a Composition visual over a retained element: it follows layout and
  scrolling, is clipped by its ancestors, hides when clipped out or detached, and lets the
  pointer through, so applications animate content in a tree with the compositor.
- `List::set_overlay` lays a `HostedVisual` over the rows that scrolls with them, and
  `List::row_bounds` gives each row's place in it.
- `ElementWindow::snapshot` pictures an area of the tree in a sprite of its own, to animate in
  the compositor during a transition; `ElementTree::paint` can paint an area of the viewport.
- `Timer` runs a callback once or repeatedly on the dispatcher queue. It never fires after
  `stop` or its destruction, and its errors end `Application::run` like other callbacks.
- `Element::set_focus_on_press(false)` keeps a focusable element, such as a splitter, out of
  the focus a left press gives; the keyboard still reaches it.
- `Element::set_cursor` and `Cursor` give an element a pointer shape, or a protected
  `cursor_at` one per point. The window shows the shape of the element under the pointer, or
  of the one that captured it; a `TextBox` shows the I-beam.
- `ElementWindow::set_dark_tooltips` draws element tooltips in the dark style, and
  `tooltip_hwnd` reaches the tooltip window.
- Keyboard focus shows from a key press until a mouse press, as the web's focus-visible, for
  every window in a top-level window: `Window::focus_visible` and `on_focus_visible`,
  `Element::focus_visible`, and `focusVisible` in `controls::State` and `Button::State`.
  Default painters draw focus rings only then.
- `Window::Options` gives a window its style, extended style, owner and content at creation.
  `Window::center_on` centres it on another window, kept on that monitor's work area, and
  `ModalScope` disables an owner while a dialog it owns is open, unless it was disabled already.
- `TextBox` says whether it can undo or redo, takes a new style (`set_style`), raises its focus
  changes (`on_focus`), and reaches Rich Edit (`send`, `text_services`). `Options::passwordChar`
  masks a password field, keeps it off the clipboard and out of UI Automation's value, and
  `Options::maxLength` and `set_max_length` limit what the user can type.
- `ElementAccessibility::acceleratorKey`, `positionInSet` and `sizeOfSet` serve UI Automation's
  AcceleratorKey, PositionInSet and SizeOfSet, with change events. An element whose control
  type is Separator is a control element but not content.
- `Items::separator` makes rows separators: lines between groups that are never selected,
  current or chosen, which the keyboard, type-ahead and the pointer pass by and UI Automation
  sees as Separators. They are `List::separatorHeight` tall without `Items::height`, and
  `List::set_separator_renderer` draws them instead of the default line.
- `Items::checked` gives list rows a check state: UI Automation's Toggle, which chooses the row,
  and `State::checked` for renderers. The list reads it again after each choice and at `refresh`.
- `Items::accelerator` gives list rows a shortcut, UI Automation's AcceleratorKey, for the row
  renderer to draw.
- List rows report their place among the list's rows, or among their siblings in a tree, with
  separators left out, as UI Automation's PositionInSet and SizeOfSet.
- `ElementLayout::crossAlign` and `mainAlign` align a stack's or flex container's children
  across and along its axis (`layout::Align`), and `Element::set_align` aligns one child.
  `ElementLayout` compares with `==`.

### Changed

- After each layout the tree hovers the element under the pointer's resting place, so content
  that appears, goes or moves under a still pointer (replaced rows, scrolling) is hovered
  without the pointer moving.
- The wheel scrolls as many 16-DIP lines per detent as Windows is set to (`PointerEvent::wheelLines`),
  or a viewport's length (`wheelPages`), instead of a fixed 48 DIPs.
- A focus move requested from a layout hook waits until layout settles, and layout hooks can
  cause at most 16 passes in one update.
- The tooltip's pause and a `TextBox`'s timers use `Timer`, so none of Composia's `WM_TIMER`
  messages reach a window procedure, and a text box's timers work without the focus.
- A whole-surface draw clips to its surface too, since the surface may share an atlas.
- A menu raises `on_selection` for every choice, the same row again included; a dropdown still
  raises it only when its value changes.

- A control without tokens of its own passes its renderer those of the nearest ancestor
  control or `StyleScope` with tokens, and `StyleTokens{}` only when there are none.
  `Control::set_tokens` and `Picker::set_popup_tokens` take `std::optional<StyleTokens>`,
  where `nullopt` inherits again, and `Control::tokens()` returns the resolved tokens by
  value; `own_tokens()` returns what was set.

### Fixed

- A dropdown or menu no longer chooses a row that cannot be selected, such as a heading, when
  the keyboard or the pointer rests on it; the popup stays open.
- A window letting go of its tree leaves the focus alone, so a hosted window that hides as it
  detaches cannot send the focus through detached elements.
- A `TextBox` that leaves the tree and comes back keeps its UI Automation provider, which Rich
  Edit would not make a second time.

## 0.3.0 (2026-11-20)

The 0.3.0 release was reissued to restore the editor's File and Edit menu bar
and fix the flicker between the arrow and I-beam when moving over a retained `TextBox`.
If you downloaded the first build (`d2d5830`), download the updated ZIP and
verify it against the new `SHA256SUMS`. The updated overlay is `0.3.0#1`.

The first release as a library: the demos moved to the `demo` branch, every public API is
documented and tested, and the package can be installed or added to another build.

`Element`, `ElementTree`, and `ElementWindow` add retained custom-drawn UI on one
surface, with nested stack, flex, grid and scroll layout, routed pointer and keyboard
events, focus traversal, clipping, z order and partial repaints.

`TextBox` adds windowless Rich Edit to the retained tree. Single-line and multiline
editors share the surface with other elements and expose UI Automation Text and Value
patterns. Applications can supply a painter for the field, text and caret.

Retained elements can expose nested UI Automation fragments with names, roles, states
and relationships. Invoke, Value, RangeValue, Selection, SelectionItem, Scroll and Text
share the retained tree's focus and geometry. Disabled ancestors govern interaction
through the keyboard, pointer and automation.

The optional `Composia::Controls` target adds lookless controls, command bindings,
fixed-height virtual lists and in-surface popups. Retained fragments can now expose
the Toggle and ExpandCollapse patterns.

The [UTF-8 editor](examples/editor/README.md) provides a complete document
workflow. This release adds a vcpkg overlay, Debug and Release SDK packaging,
a [compatibility policy](docs/releases.md), and a reproducible
[comparison with Direct2D and GDI](benchmarks/README.md).

### Upgrading from 0.2.0

0.3.0 is not source compatible with 0.2.0. Rebuild everything that links Composia, require
`find_package(Composia 0.3)`, and make these changes:

- `Button::enabled(bool)` becomes `set_enabled(bool)`, and `Button::accessibility_provider()`
  becomes `automation_provider()`, both inherited from `Window`; `enabled()` is unchanged.
- `Window::native_window()` is gone; use `hwnd()`.
- `animations::bob` and `animations::pulse` are gone; start the keyframe animation directly,
  as `examples/gpu` does for its pulse.
- spdlog is no longer used. Composia's diagnostic events go to `OutputDebugString` unless
  `set_log_handler` routes them elsewhere; forward them to spdlog from that handler if you
  relied on its default logger.
- `COMPOSIA_BUILD_DEMO` is now `COMPOSIA_BUILD_EXAMPLES`. The application manifest is
  `resources/composia.manifest`, available to CMake as `COMPOSIA_MANIFEST`.

### Added

- `Window` tracks pointer hover and capture, focus and its restoration on activation, and
  the enabled state including ancestors, with virtual hooks and signals
  (`on_hover_changed`, `on_focus_changed`, `on_enabled_changed`, `on_pointer_capture_lost`,
  `on_destroy`). It invalidates DIP rectangles (`invalidate(layout::Rect)`, `paint_rect()`),
  routes child-control notifications (`NotificationHandler`, `set_notification_handler`),
  answers `WM_NEXTDLGCTL` like a dialog, and serves a UI Automation provider
  (`set_automation_provider`).
- `CompositionWindowTarget::render` paints a window in one call and, inside `on_paint`,
  updates only the invalidated area. `ScopedSurfaceDraw` clips to its update rectangle and
  reports it (`update_bounds()`), and hands out scope-owned brushes (`solid_brush`).
- `Accessible`: a UI Automation element for any window, with the Invoke and Value patterns.
- `NativeControl`: standard Win32 controls hosted with DIP bounds, DPI-aware fonts, colors,
  and notifications.
- `SwapChainSurface`: Direct3D 11 frames in the visual tree on Windows 10 and later.
- `TextLayout` takes a font family and a locale and measures text (`metrics()`).
- `Appearance` reports the system's dark mode, high contrast, accent color, text scale,
  animation setting, and system colors. `Application::appearance()` holds it; when Windows
  reports a change, every window's `on_appearance_changed` runs, every window is
  invalidated, and `Application::on_appearance_changed` subscribers run.
- `Window::set_dark_title_bar` draws a top-level window's frame dark or light.
- `Button` takes a painter that draws it from its state (`Button::State`), with
  `set_painter`, `set_label`, `label()`, and `pressed()`. `Button::paint_default` is the look
  without a painter.
- `set_log_handler` and `LogLevel` in `composia/Log.hpp`.
- `composia/Version.hpp`, generated from the project version.
- The package installs the application manifest and defines `COMPOSIA_MANIFEST`.
- CMake options `COMPOSIA_INSTALL`, `COMPOSIA_WARNINGS_AS_ERRORS`, and `COMPOSIA_COVERAGE`,
  and a `coverage` preset.
- Examples: `hello`, `inputs`, and `gpu`.

### Changed

- `Window` handles more messages after `on_message`: `WM_GETOBJECT` with an automation
  provider; `WM_COMMAND`, `WM_NOTIFY`, `WM_CTLCOLOR*`, `WM_DRAWITEM`, `WM_HSCROLL`, and
  `WM_VSCROLL` from controls with a notification handler; `WM_NEXTDLGCTL`; and
  `WM_ACTIVATE` on a top-level window, which returns the focus to the window that last had
  it. An `on_message` override that returns a result still takes precedence.
- `on_enabled` also reports changes caused by an ancestor, so a disabled owner window
  disables and redraws its Composia controls.
- `hovered()` follows the pointer position while the window has captured it.
- `Button` no longer has a fixed dark look. Without a painter it is a flat button in the
  system's light, dark, or high contrast colors, with a 14 point Segoe UI label; give it a
  painter for any other look, as `examples/inputs` does. It draws with scope-owned brushes
  instead of caching them across frames.
- Inside another build (`add_subdirectory` or FetchContent), Composia uses that build's
  runtime library, does not use `/WX`, and builds no tests, examples, or install rules.
- Debug libraries are named `composiad.lib` and embed their debug information.
- Every public header documents what each function does, throws, and requires.

### Removed

- The texture studio, virtual atlas, mail client, and composition demos, now on the `demo`
  branch with the UI builder.
- The spdlog, fmt, and DirectX Toolkit dependencies; vcpkg supplies only WIL and C++/WinRT.
- `Button::enabled(bool)`, `Button::enabled()`, `Button::accessibility_provider()`,
  `Window::native_window()`, `animations::bob`, and `animations::pulse`.

### Fixed

- Creating an `Application` after an earlier one on the same thread had been destroyed
  crashed: C++/WinRT's cached activation factories outlived the COM servers they came from.
- The installed library carried the tests' failure-injection hooks whenever the tests were
  built.
- Failures while shutting down an application or closing a capture went to
  `OutputDebugString` directly instead of through the log handler.

## 0.2.0

Accessible buttons, keyboard navigation, and DIP layout; cached text and brushes with
coalesced redraws; direct Direct3D texture composition, Windows Graphics Capture, and sparse
virtual surfaces; installable package and Windows CI.
