# Changelog

Composia follows [semantic versioning](https://semver.org). Before 1.0, a minor version may
change the API; each change is listed here.

## 0.3.0 (unreleased)

The first release as a library: the demos moved to the `demo` branch, every public API is
documented and tested, and the package can be installed or added to another build.

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
- `Button` draws with scope-owned brushes instead of caching them across frames.
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
