# Composia demos

Larger programs built on Composia's public API. They live on the `demo` branch, which
follows `main`; the library's own documentation is in the [README](../README.md) and the
[guide](../docs/guide.md).

Build them with the library, from the repository root:

```powershell
.\scripts\build.ps1 -Preset release -Test
```

They are built into `out\build\<preset>\demo`. `COMPOSIA_BUILD_DEMOS` turns them off. Each
writes a log file of its own and Composia's events (`composia.log`, `composia-mail.log`, and
so on) in its working directory, through `DemoLog.hpp`. Every `Button` they create draws the
demos' rounded accent look through `DemoButton.hpp`, the painter Button takes. Add `--warp`
to any of them for software rendering.

## Composition demo

```powershell
.\out\build\release\demo\composia-demo.exe
```

`main.cpp` only initializes logging and starts the application and window.
`DemoWindow.cpp` shows drawing and scene assembly: the moving tile uses vector keyframes, the
status dot a looping opacity animation and an implicit offset animation, and an expression
centers the tile's container. Composition runs these animations independently of the UI
message loop; the demo has no rendering timer. Its two buttons change and reset the tile's
motion.

## Texture studio demo

```powershell
.\out\build\release\demo\composia-media-demo.exe
.\out\build\release\demo\composia-media-demo.exe "C:\Videos\clip.mp4"
```

The demo starts with an animated GPU landscape. **Capture…** opens the Windows picker to
select a window or display for live Windows Graphics Capture. The captured content appears
beneath composed text, with the GPU landscape in picture-in-picture. **Stop capture** ends the
session and returns to the landscape; closing the captured window also ends capture. Windows'
capture indicator stays enabled. Canceling the picker keeps the current content. If you close
the demo while the system picker is open, dismiss the picker to finish exiting.

You can also open or drop a local video. **Play / pause** controls the video, or the landscape
when no video is open; it is disabled during screen capture. **GPU only** stops either source
and returns to the landscape. Videos loop; supported formats depend on the codecs installed on
the PC.

This demo needs a recent Windows 11 runtime and a graphics device supporting composition
textures. It checks support at runtime and displays an explanation when unavailable.

The landscape is rendered with a Direct3D 11 pixel shader into `TextureSurface`s; WinRT
`MediaPlayer` copies decoded video frames into another set of Direct3D 11 textures with
`CopyFrameToVideoSurface`. Neither path reads pixels back to the CPU. Each stream rotates three
shared textures: before writing, the demo checks `TextureSurface::available()` and skips a
frame when all buffers are busy. It recreates these textures after a graphics-device
replacement, restoring both playing and paused content. Preview updates use a 16 ms UI timer
and pause while minimized. Captured frames are copied on the GPU into a shared composition
texture before the capture's buffer is returned, so captured textures are never held after
their frame is closed. Shader compilation, media playback, and file-picker dependencies are
confined to `demo/media`.

## Virtual atlas demo

```powershell
.\out\build\release\demo\composia-virtual-demo.exe
```

A scrollable, procedural map on a **1,048,576 × 1,048,576** pixel `VirtualSurface`. The demo
draws only 512-pixel tiles covering the viewport plus one tile of overscan; scrolling away
releases the old regions and returning redraws them. No document-sized texture or CPU bitmap is
created. The status bar reports cached tiles and retained pixel bytes, an estimate of BGRA
content rather than a measurement of GPU memory.

Drag to pan, use either native scroll bar, or scroll with the mouse wheel (Shift for
horizontal). Ctrl+wheel zooms around the pointer; the zoom buttons use the viewport center.
Home/End jump to opposite corners, arrow keys move by a line, and Page Up/Down move by a
viewport. Click the map to give it keyboard focus. The demo limits zoom-out to the surface's
tile budget and scales the cached raster when zooming.

## Mail client demo

```powershell
.\out\build\release\demo\composia-mail-demo.exe
```

A three-pane mail client built only from the framework's primitives: folders, a message list, a
reading pane, and a compose form, over an in-memory mailbox of fictional messages. **It is a UI
exercise.** There is no account, sign-in, storage, or network code; sending a message only
appends it to the Sent folder for the lifetime of the process, and attachments are labels.

The list scrolls with the wheel, Page Up/Down, or the keyboard. Clicking a row opens it and
marks it read; stars toggle from the row or the header; **Archive** and **Delete** move messages
and offer **Undo** in the status bar; deleting from Trash is permanent. Search filters the
current folder as you type. **Compose**, **Reply**, and **Forward** open an editable form;
**Send** files the message under Sent, **Save draft** keeps it under Drafts, and Escape saves a
non-empty draft. Open a draft with Enter or a click to continue editing it. With the list
focused: C composes, R replies, F forwards, E archives, S stars, U marks unread, Delete trashes,
Up/Down and Home/End move, Ctrl+F focuses search, and Ctrl+Z undoes.

The chrome, folders, rows, header, and status bar are drawn into the window's canvas surface on
each paint; each paint also records the DIP rectangles of the clickable regions, which pointer
messages hit-test against. Buttons are the framework's accessible `Button` controls.
`mail/TextField` is sample code for a composition-rendered, editable text box: a child HWND that
draws its text into its own surface and positions a blinking caret visual with a composition
animation, so blinking never redraws text. It supports typing, Backspace/Delete (with Ctrl for
words), arrows, Home/End, Enter, Ctrl+V paste, wheel scrolling of multiline content, and pointer
caret placement, and it hands Tab to the parent's dialog navigation. It has no selection, IME
composition, or UI Automation provider. `mail/MailModel` holds the mailbox and formatting helpers
and has no UI dependencies.

## UI builder demo

```powershell
.\out\build\release\demo\composia-builder-demo.exe
```

A visual form designer built from the framework's primitives: a palette of widgets,
a design surface, an inspector, a live preview, and **Build app**, which writes a
standalone executable that runs the design. It opens with a sample sign-in form.
Designs are saved as small text files; the preview runs inside the demo window.

The palette offers panels, labels, buttons, text fields, checkboxes, sliders, and
image placeholders. Click an entry to add it to the selected panel (or the form),
or drag it onto the form; dropping onto a panel nests the widget inside it. Click a
widget to select it, drag it to move it (dragging onto another panel re-parents it),
and drag its corner handles to resize it. Moves snap to an 8 DIP grid and to the
edges of neighbouring widgets; the status-bar toggle turns snapping off. The outline
lists the tree, and selecting there works the same way.

Each selected widget shows four **anchor pins**, one per edge. Click a pin to anchor
that edge to the parent at its current margin; click again to release it. Drag a
pin onto empty parent space to anchor to the parent, or onto a neighbour to anchor
to the nearer edge of that sibling, so a Cancel button can sit a fixed gap to the
left of Sign in, or share its bottom edge. The inspector lists each edge's target and
margin; the target can also be cycled by clicking it, and margins typed directly.
Anchoring both edges of an axis makes the widget stretch. Moving or resizing an
anchored widget rewrites its margins, so what you see is always what resolves.
Anchor cycles are tolerated: the solver ignores one link, marks the widget in red,
and the inspector explains. Position, size, name, text, checkbox state, and slider
value are editable; **Delete**, **Duplicate**, **Bring forward**, and **Send backward**
act on the selection, and the form's own size and title are editable when nothing
is selected, or by dragging the form's bottom-right corner. The form also has a
**minimum size**, outlined on the design surface: a running form cannot be resized
below it, which keeps anchored layouts from collapsing, and the designer's form
cannot be made smaller than it either. It never exceeds the form's size.

**Preview** (F5) fills the window with the form and instantiates the framework's
real `Button` and `TextField` controls where the design has them; checkboxes and
sliders respond to the pointer; clicking a button reports it in the status bar.
Resize the window to watch the anchors reflow the layout. Preview changes never
reach the design; Escape returns to editing. **Save** and **Open** use a line-based
UTF-8 text format (`.cui`), and the title shows unsaved changes. Undo and redo cover
every edit, with consecutive keystrokes in one field sharing a step. With the form
focused: arrows nudge by 1 DIP (Shift for 8), Delete removes, Ctrl+D duplicates,
Ctrl+Z/Ctrl+Y undo and redo, Ctrl+S saves, Ctrl+O opens, Ctrl+N starts over,
Ctrl+B builds an app, and Ctrl+] / Ctrl+[ change z-order. Add `--warp` for
software rendering. The demo keeps the Windows 10 baseline.

**Build app…** (Ctrl+B) asks for a file name and writes a standalone `.exe`: a
Windows application whose window is titled and sized by the design, refuses to
shrink below its minimum size, and whose controls are the same real framework
controls the preview uses, with the anchors reflowing on resize. Previewing in
the designer honours the same minimum, enlarging the designer window if needed. The status bar then offers **Run app**. No compiler runs: the
output is a copy of `composia-form-player.exe`, a compiled player built alongside
the demo, with the design embedded as an `RCDATA` resource through
`UpdateResource`. The builder carries its own copy of the player as a resource,
so it needs nothing beside it; a `composia-form-player.exe` next to it is used
when that copy is absent. Built apps are statically linked and import only
Windows system DLLs, like the demos. Running the player directly opens a `.cui`
given on the command line, and `--validate` loads the design and exits with 0
without showing a window, which is how the tests check built apps. A built app
has no save, settings, or network behaviour; it is the form, running.

`builder/BuilderModel` holds the document and has no UI dependencies. A
widget stores a parent, a position and size for its free edges, and an `Anchor`
per edge: none, the parent, or a sibling edge, plus an inward margin. `resolve()`
walks each container, orders siblings by their dependencies, and returns DIP
rectangles in draw order; `fit()` is the inverse, deriving free coordinates and
margins from a target rectangle. `History` keeps document snapshots.
`FormPainter` draws each widget kind; `FormView` runs a document inside any host
window, creating the real controls and handling checkbox and slider input, and
is shared by the designer's preview and by `FormPlayerWindow`, the built app's
window, so both render identically. `AppPackager` reads and writes the embedded
resources. The designer paints the chrome and the design-time form into its
canvas surface on each draw, records hit regions for the chrome, and hit-tests
the form spatially from the resolved placements. It reuses the mail demo's
`TextField` for the inspector and for running fields.

## Tests

The demos' tests build with the library's tests and use the same labels:

- `demo-smoke` drives the composition demo through its first paint, coalesced resizes, an
  unwound drawing scope, DPI sizing, minimize and restore, and device loss.
- `virtual-demo-navigation` drives the atlas through keyboard, scroll bar, zoom, drag, resize,
  minimize, and device replacement.
- `mail-model` (core) checks the mailbox and its formatting helpers without windows;
  `mail-client` drives the mail client through pointer, keyboard, and button input: selection,
  stars, trash and undo, folders, search, scrolling, compose, send, reply, forward, drafts,
  Escape, resize, and device replacement.
- `media-gpu` and `media-video` check changing GPU and decoded video pixels, opaque alpha,
  pause and resume, resize, device replacement while playing and paused, Unicode file paths,
  and a missing file. The [synthetic video fixture](tests/assets/README.md) is generated
  locally. They skip when composition textures are unsupported.
- `builder-model` (core) checks the UI builder's document: anchored layout across form
  sizes, sibling anchors, fitting, validation, cycles, re-parenting, duplication, z-order,
  the text format, and history coalescing. `builder-editor` and `builder-editor-warp`
  (desktop) drive the designer through pointer, keyboard, and field input: selection,
  palette click and drag, drop into panels, inspector edits, undo and redo, snapping, corner
  resize, pin clicks and drags, margins, form resize, save and reopen, the minimum size
  constraining the form, the live preview with real controls following a window resize, and
  device replacement. `builder-app` and `builder-app-warp` (desktop) build an app from the
  freshly built player, read the embedded design back from the output, run the output with
  `--validate` as a separate process, reject a bad player, run the player's window
  in-process with its real controls following a resize, its refusal to shrink below the
  minimum size, and its checkbox and slider responding to clicks, and drive the designer's
  own Build app command including its error path.
- `media-capture-window` and `media-capture-monitor` capture an owned window and its monitor
  through the texture studio, through a resize, device replacement, stop and restart, and the
  target closing.

Each has a `-warp` variant where it renders.
