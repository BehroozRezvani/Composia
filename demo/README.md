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
so on) in its working directory, through `DemoLog.hpp`. Add `--warp` to any of them for
software rendering.

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
- `media-capture-window` and `media-capture-monitor` capture an owned window and its monitor
  through the texture studio, through a resize, device replacement, stop and restart, and the
  target closing.

Each has a `-warp` variant where it renders.
