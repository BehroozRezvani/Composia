# Testing Composia

Composia's tests drive the real platform: real HWNDs, the real compositor, and Direct3D on
both a hardware device and WARP. Where a result is visible, they read back pixels: from a
drawing surface, from a captured frame of a window's visual tree, or from a hosted control
through `PrintWindow`. They use only the public API, except where the test library injects
failures or a stand-in system appearance (see [Test hooks](#test-hooks)).

## Running the tests

```powershell
.\scripts\build.ps1 -Preset release -Test               # everything
.\scripts\build.ps1 -Preset release -Test -TestLabel core  # no windows needed
```

or, from an x64 Visual Studio developer shell, `ctest --preset release`. Each test
executable in `out/build/<preset>/tests` also runs alone, with a scenario name and an
optional `--warp` for the WARP software device:

```powershell
.\out\build\release\tests\composia-foundation-tests.exe native --warp
```

Tests labeled `core` need no desktop. Tests labeled `desktop` open windows, so they need an
interactive session and run one at a time; they send messages rather than moving the
mouse, so they do not need the foreground and can run while you work. A scenario that the
machine cannot run, such as screen capture without Windows Graphics Capture, exits with 77,
which CTest reports as skipped rather than passed. CTest keeps output in
`out/build/<preset>/Testing/Temporary/LastTest.log`, and `build.ps1 -Test` also writes JUnit
results to `out/build/<preset>/test-results-*.xml`.

The test build also compiles every public header on its own, twice, so a header that is not
self-contained, or lacks an include guard, fails the build.

## What the tests check

Tests are named `<area>-<scenario>`; a `-warp` suffix runs the same scenario on WARP.

| Tests | Check |
| --- | --- |
| `core-signal`, `core-layout`, `core-text` | Signal connection lifetimes, nested and reentrant emission, and failing subscribers; stack layout, shortening, and invalid input; text measurement, wrapping, font family, weight, and locale, and invalid arguments |
| `application-*` | Device replacement that fails at each step and changes nothing; repeated recovery and its notifications; a persistent device loss; applications one after another on a thread, also after a failed start; the fallback to WARP; shutdown ordering; posted callback errors; calls from another thread |
| `appearance-*` | `Appearance::current()` against the system's settings; changes reaching every window and subscriber, only when the appearance changed and only through top-level windows, with errors reported; the dark title bar; what a `Button` painter receives, `set_painter`, and `set_label`; and the default look's pixels in light, dark, and high contrast, pressed and not |
| `window-*` | Destroyed windows and parents rejecting every operation; several top-level windows; errors from a secondary window; the `WM_QUIT` exit code; a handler's error kept for `rethrow_callback_error` |
| `smoke-lifecycle` | A window's first paint, coalesced resizes, drawing scopes unwound by exceptions, DPI sizing, minimize and restore, device loss while drawing and while idle, and a composition animation that completes without a render loop |
| `foundation-pointer` | DIP conversions; hover, including during capture; capture released, taken, cancelled, and released on destroy; the hover and capture hooks and signals, also on a plain `Window` |
| `foundation-focus` | Focus and enabled notifications; an ancestor's enabled state reaching every descendant; focus restored after activation and after minimize and restore |
| `foundation-render` | `CompositionWindowTarget::render`: sizing, the minimized skip, device replacement, the device-loss retry, scope-owned brushes, rejected windows, the area of partial repaints, and full repaints after a resize or a device replacement; the log handler |
| `foundation-partial` | Composited pixels after partial repaints, inside and outside the repainted areas |
| `foundation-swapchain` | Composited pixels of a `SwapChainSurface` after a present, a resize, a device replacement, and a device loss during `present`; the last frame staying on screen; a resize after a replacement; invalid sizes, alpha modes, and renderers |
| `foundation-accessible` | An `Accessible`'s properties, focus and enabled reporting, `WM_GETOBJECT`, patterns, Invoke and Value actions marshalled to the UI thread, a read-only value, missing arguments, duplicates, and the element after its window is gone |
| `foundation-native` | Hosted EDIT, multiline EDIT, check box, push button, combo box, and list view: placement, text, notifications to every subscriber, fonts at the DPI, colors and the pixels they produce, visual styles, owner-draw and scroll routing, enabled and visible state, Tab through the dialog loop and out of a multiline EDIT, construction errors, and destruction with the parent |
| `interaction-input`, `interaction-accessibility` | `Button` activation by pointer, Space, and Enter, with cancellation; a UI Automation client in another apartment finding and invoking a `Button` |
| `dpi-monitors`, `dpi-messages` | A window moved across every monitor, with child and composition sizes at each DPI; `WM_DPICHANGED` and `WM_DPICHANGED_AFTERPARENT` handling |
| `surface-pixels` | The target's visual tree, and drawn pixels, text, and atlas offsets at 96 to 192 DPI, before and after a device replacement |
| `animation-helpers` | The helpers' visuals, a sprite kept centered as its parent resizes, and an offset change that animates rather than jumps, through composited pixels |
| `virtual-*` | Bounded caching across 80 distant viewports, pixels after trimming, tile reuse and invalidation, paint failures, resize, device replacement, the largest extent, partial updates at five DPIs, and invalid arguments |
| `texture-surface` | Composition textures, where the system supports them |
| `capture-window`, `capture-monitor` | `ScreenCapture` of an owned window and its monitor: changing pixels, a target resize, device replacement, close and restart, and the target closing |
| `package-consumer` | The installed package, moved elsewhere, consumed with `find_package`: every header, the version macros, and the embedded manifest |
| `subproject-consumer` | The source tree added to another build with `add_subdirectory`, using that build's DLL runtime library, with no tests or examples |

## Test hooks

Some failures cannot be caused from outside: a device replacement failing halfway, or no
hardware device at all. `src/TestHooks.hpp` defines failure points that only the
`composia-testing` library compiles in, and only the `application-*` and `appearance-*` tests
link it; the library that is installed never carries them. The same library lets the
`appearance-*` tests stand in a chosen appearance for the system's, since changing the real
settings would change the desktop. The `UISettings` events that also report changes are not
triggered by any test. Device loss itself is injected as an
`HRESULT` from a drawing callback or by signaling the removal event: the tests do not reset
a GPU or qualify driver recovery.

## Coverage

```powershell
.\scripts\build.ps1 -Preset coverage -Test
```

builds the library and the tests with clang source-based coverage, runs every test, and
writes to `out/build/coverage`: `coverage-html/index.html`, a summary in `coverage.txt`, and
each function's coverage in `coverage-functions.txt`. It warns about a library source that
no test links, since such a file would be missing from the report rather than shown as
uncovered.

On the machine below, the tests run 301 of the library's 305 functions and 98.7% of its
lines. The functions left are the `NativeControl` constructor's cleanup after a failure that
follows creating the control, and the handler for `UISettings` change events, which runs only
when the real settings change. The other lines left are failure-only cleanup and error logging,
and the window constructor's correction for a monitor whose DPI differs from the system DPI.

## Continuous integration

The Windows workflow builds Debug and Release and runs the `core` tests on every push to
`main` and every pull request. The `desktop` tests run when the workflow is started by hand
with its `desktop` input, since they need an interactive session.

## Verified, and not verified

The suite passes on Windows 11 build 26300 with clang-cl 21.1.1, Windows SDK 10.0.26100.0,
and one 96-DPI monitor, on hardware and on WARP. The checks do not establish:

- Windows 10 behavior, including `SwapChainSurface`, which exists for Windows 10 but ran only
  on Windows 11, and hosted controls without a common-controls version 6 manifest.
- Moving between monitors with different scales. DPI changes are checked by sending the
  messages Windows sends; the classic check box glyph that colored check boxes use was not
  inspected at high DPI.
- Real pointer and keyboard input. The checks send messages; focus restoration is driven by
  `SetActiveWindow` and minimize and restore rather than Alt+Tab.
- IME composition in hosted EDIT controls, and what a screen reader announces. UI
  Automation is checked through provider calls and one system client.
- The system screen-capture picker, which needs a person; the capture checks pick their
  target through the SDK's desktop interop.
- Whether the update clip is ever needed: on this machine, drawing outside a partial
  update's rectangle did not reach the surface even without it.
- Real appearance changes. The change path is driven with a stand-in appearance and the
  messages Windows sends; switching the real dark mode, contrast theme, accent, or text size,
  and the `UISettings` events that report some of them, are not exercised.
- Consumers built with MSVC (`cl.exe`) rather than clang-cl.
