# 07 — Advanced integration research: revisiting the "impossible" list

Status: research complete for all seven items; proof-of-concept code exists
for five of them (see the summary table). **Nothing in this document has run
on real Haiku yet.** "Compile-verified" means the code compiles against the
Haiku headers and app_server sources of the revision in use
(`b9fdc8a`, hrev-era 2026) with `-Wall -Wextra -Werror`. "Host-tested" means
the portable code passed its unit tests. Earlier documents (docs/vfx.md,
docs/status.md before this revision) listed these items as "impossible
without changing Haiku". This document replaces that verdict with traced
source paths and working designs.

## 0. Integration levels

| Level | What it may use | Default |
|---|---|---|
| **1 Native** | Public Haiku APIs and stable private client APIs (WindowInfo, LaunchRoster, DecoratorPrivate). Deskbar and Tracker stay untouched. | Always available |
| **2 Enhanced** | Modules that run inside system servers on an **unmodified** Haiku: the Window Engine (an app_server decorator add-on that also registers as a `DesktopListener`) and an input_server filter. Guarded by a Haiku-revision ABI check. | Opt-in per feature |
| **3 Advanced** | An optional, reproducible patch set for Haiku (`patches/haiku/`), discovered at run time. | Off; requires a patched app_server |

The key observation behind most of this revision: **the Window Engine
already runs inside app_server.** Every limitation earlier documents traced
to "the client is told too late" or "the client cannot see that" was a
property of Level 1, not of Haiku. Inside app_server, the engine sees the
same data structures app_server uses, at the same moments, under the same
locks.

## Summary

| Feature | Earlier verdict | Now | Level | Implementation |
|---|---|---|---|---|
| C. Close animations started by applications | Impossible | **Possible** | 2 | `LuminaEngineLifecycle.cpp` (compile-verified) |
| D. Minimize animations started outside Lumina | Impossible | **Possible** (all paths) | 2 | same; restore too |
| E. Fade transitions | Needs a compositor | **Possible** (precomposed, validated background); live fades at Level 3 | 2 / 3 | crossfade in engine; true alpha in patch |
| G. Cursor themes | No global setting | **Possible** without patching | 2 | `CursorManager::SetCursorSet` via engine; `CursorThemes.cpp` builder |
| A. Genuine see-through panels | Impossible | **Possible** with a small optional patch | 3 | `patches/haiku/0001-…` (type-checked) + `fx/OverlayCompose` (host-tested) |
| B. Live blur / frosted glass | Impossible (live) | **Possible** at Level 3; Level 1/2 keep snapshot glass | 3 | same patch; blur in compose core |
| F. Shadows and rounded corners | Impossible | Rounded (aliased) corners **possible** at Level 2 by decorator footprint; soft shadows and antialiased corners on overlays at Level 3; on ordinary windows a designed Level 3 extension | 2 / 3 | overlay shadows/corners implemented in compose core; ordinary-window extension designed |

Two genuine limits remain, and they are stated precisely below:
- Pixels of a window that is covered by another window do not exist anywhere in stock app_server. This is a consequence of having no backing store, not of any API restriction.
- Real translucency of *ordinary application windows* (as opposed to overlay windows that Lumina owns) would need per-window offscreen rendering, which means a full compositor. The Level 3 patch deliberately does not attempt that.

---

## C. Close animations started by applications

### Trace

Every way a window leaves the screen ends in one function:

| Initiator | Path |
|---|---|
| `BWindow::Hide()` | `AS_HIDE_WINDOW` → `ServerWindow::_Hide()` (`ServerWindow.cpp:372`) → `Desktop::HideWindow()` |
| `BWindow::Quit()`, close box, app exit | `ServerWindow` teardown (`ServerWindow.cpp:209`) → `Desktop::RemoveWindow()` (`Desktop.cpp:1769`) → `HideWindow()` first, then `NotifyWindowRemoved()` |
| `BWindow::Minimize()` from any source | see §D → `Desktop::MinimizeWindow()` → `HideWindow(window, true)` |

`Desktop::HideWindow()` (`Desktop.cpp:1425–1481`):

1. `LockAllWindows()`: the all-window **write** lock (`fWindowLock`, a `MultiLocker`).
2. `_HideWindow()` (`Desktop.cpp:3224`): rebuilds clipping, then `_SetBackground()` fills only the area **no window covers** (normally empty, because Tracker's desktop window covers the screen). Then `MarkDirty()` → `_TriggerWindowRedrawing()` → `Window::ProcessDirtyRegion()`, which only *posts* `AS_REDRAW` to each affected `ServerWindow` thread (`ServerWindow::RequestRedraw`).
3. `NotifyWindowHidden(window, fromMinimize)` (`Desktop.cpp:1474`), **still holding the write lock**.
4. `UnlockAllWindows()`.

Repainting a window requires the window **read** lock, which cannot be
taken while step 3 runs. So in `DesktopListener::WindowHidden()` the
departing window's pixels are still intact in app_server's drawing buffer.
That buffer is the RGBA32 `MallocBuffer` back buffer, which
`AccelerantHWInterface::SetMode` always allocates (`AccelerantHWInterface.cpp:638–659`).
The earlier verdict came from client-side notifications, which arrive after
step 4.

### Approaches considered

| # | Approach | Mechanism | Verdict |
|---|---|---|---|
| 1 | **Server-side lifecycle capture** | `WindowHidden()` hook in the engine (Level 2) | **Chosen.** Covers every initiator; no patch needed |
| 2 | Destruction observers (decorator / `WindowBehaviour`) | The decorator is destroyed with the window. `WindowBehaviour` sees only mouse-driven actions | Partial: misses `Quit()` and API calls |
| 3 | Client API interception | A wrapper libbe or a replacement `BWindow::Quit` | Rejected. Haiku has no `LD_PRELOAD`-style injection for an existing app, statically placed code bypasses it, and patching libbe is a Level 3 change with a far larger blast radius than #1 |
| 4 | app_server patch adding `WindowWillHide` | New listener hook before `_HideWindow` | Not needed: #1 already runs before any repaint |

### Implementation (Level 2)

`src/engine/LuminaEngineLifecycle.cpp`, `src/server/LifecycleProxies.cpp`:

- **Proxy pool.** lumina_server creates two hidden, borderless,
  floating-all, `B_AVOID_FOCUS` windows. Each child view carries a
  screen-sized `B_RGB32` view bitmap. It registers their server tokens with
  the engine (`LUMINA_OP_PROXIES`). The windows belong to lumina_server's
  team, so if it dies, app_server removes them, and nothing can be left on
  screen by the engine.
- **Capture.** In `WindowHidden()` the engine checks eligibility
  (`fx::Decide`). It must be a normal feel, a bordered look and not the
  desktop shell, not in a Stack & Tile group, on the current workspace, at
  least `minVisible` (80%) visible, and smaller than `maxPixels`. It then
  copies the window's outer frame from `HWInterface::DrawingBuffer()` (back
  buffer, no software cursor in it) straight into the proxy's
  `ServerBitmap`. It takes `DrawingEngine::LockExclusiveAccess()` after the
  window lock, the same order `ServerWindow` drawing uses.
- **Takeover in the same transaction.** It resizes and moves the proxy,
  re-describes its view bitmap (`View::SetViewBitmap` with `B_FOLLOW_ALL`,
  reference balanced with `AcquireReference()`), and calls
  `Desktop::ShowWindow(proxy)`. The rw lock is recursive for its holder,
  just as Stack & Tile moves windows from its own hooks. When the lock is
  released, windows below repaint *around* the proxy. The user never sees a
  gap.
- **Animation.** The existing retargetable timeline animates the proxy to
  `CloseTarget` (shrink about the centre). Because the view bitmap follows
  the view, app_server rescales it server-side every frame
  (`View::ResizeBy` → `resize_frame` at `View.cpp:735`; drawing at
  `View.cpp:1079`). No client drawing happens per frame. When it lands
  (`_ProxyLanded`) the proxy is hidden. `Desktop::RemoveWindow()` notifies
  `WindowRemoved()` right after, and that upgrades the transition kind from
  "hide" to "close" for reporting.
- **Safety.** Proxies are released after `duration + proxyTimeout` even if
  the client clock stops. Mouse hooks double as a watchdog. Engine unload
  (`ListenerUnregistered`) releases everything. A failure at any step
  leaves the stock behaviour (the window just disappears).

### Limits and risks

- **Obscured windows.** Pixels of a covered window do not exist anywhere
  (no backing store). The visibility check skips those transitions instead
  of animating wrong pixels. Removing this limit requires per-window
  backing (a full compositor, beyond Level 3 scope).
- **Holding the write lock.** Capture is a `memcpy` of the frame, about
  1.9 MB for 800×600, roughly 0.3–0.6 ms. It is measured and reported as
  `maxCaptureUsecs`. `maxPixels` bounds the worst case.
- **Exported symbols.** The engine calls more app_server internals than
  before: `View::SetViewBitmap`, `CursorManager::*`, `DrawingEngine::Lock*`.
  Haiku's own decorators link against the app_server binary in the same
  way. But if a symbol is not exported, the add-on fails to load, and
  app_server falls back to its built-in decorator. **Verify on Haiku.**
- **Tests.** Experiment E1 (`experiments/fx-lifecycle-engine`): the
  application quits its own window, the test waits until app_server has
  removed it, then compares the screen with the window's last image. The
  proxy must still show it, and later the desktop. E4 checks the obscured
  skip.

---

## D. Minimize and restore started outside Lumina

### Trace

| Initiator | Path |
|---|---|
| Application `BWindow::Minimize(true)` | `AS_MINIMIZE_WINDOW` (`ServerWindow.cpp:620–628`) → `Desktop::MinimizeWindow()` |
| Title-tab double-click / minimize gesture | `DefaultWindowBehaviour` (`decorator/DefaultWindowBehaviour.cpp:125`) → `ServerWindow::NotifyMinimize(true)` (`ServerWindow.cpp:431`) → client `B_MINIMIZE` → `BWindow::Minimize` → `AS_MINIMIZE_WINDOW` |
| Deskbar, Twitcher, `do_window_action` | `Desktop::WindowAction()` (`Desktop.cpp:2375`) → `NotifyMinimize()` → same as above |
| Team minimize | `Desktop::MinimizeApplication()` (`Desktop.cpp:2340`) → `NotifyMinimize` per window |
| Keyboard shortcuts | delivered to the application or Deskbar → one of the above |

`Desktop::MinimizeWindow(minimize=true)` (`Desktop.cpp:1484`) calls
`HideWindow(window, true)`. So minimizes reach the **same locked hook as
closes**, with `fromMinimize == true`. A single interception point covers
all of them. There is no need for the input-filter or decorator-specific
paths the brief suggested; those would each have seen only a subset.

Restore: `MinimizeWindow(false)` → `ActivateWindow()` (which shows the
minimized window, `Desktop.cpp:1246`) → `NotifyWindowMinimized(window,
false)`, still locked, before the window has painted.

### Implementation (Level 2)

- **Minimize.** Same capture and takeover as §C. The target is the window's
  panel button. Lumina's window-list applet pushes all button rectangles to
  the engine (`LUMINA_OP_ANCHORS`, sent only when the layout changes). With
  no anchor, the target is a small rect at the bottom centre. The
  snapshot is cached (`fx::SnapshotCache`, LRU, byte budget) for the
  restore.
- **Restore.** In `WindowMinimized(window, false)` the engine fills a proxy
  from the cached snapshot and shows it at the anchor. It **parks the real
  window off-screen** (it hasn't painted yet), flies the proxy to the frame,
  unparks the window *beneath* the proxy, and hides the proxy 60 ms later so
  the application has repainted. The park deadline counts from the expected
  landing, so the existing park watchdog cannot cut the animation short.
- **Lumina-initiated actions** (the panel) no longer run the old
  client-side proxy when the engine path is live. They send the anchor and
  perform the plain action, and the engine animates it like any other
  minimize.

### Limits

- `ActivateWindow()` can unminimize directly (`Desktop.cpp:1246`) without
  `NotifyWindowMinimized(false)`, for example `BWindow::Activate()` on a
  minimized window. That path is not animated; the window simply appears.
  A one-line Level 3 notify would close this gap.
- The restore image is the last image before minimize. Content that changed
  while minimized "pops" when the proxy lifts. That is the same trade-off
  as experiment D2.

---

## E. Fade transitions

### Level 2: precomposed crossfade with a verified background

A fade needs the pixels *behind* the window. At close time those are
unknown: the window covers them, and with no backing store nothing retains
them. The engine instead records them **before the window first paints**.
`Desktop::ShowWindow()` → `_ShowWindow()` only posts redraws, and
`ActivateWindow()` → `NotifyWindowActivated()` (`Desktop.cpp:1202`) runs
under the write lock before any paint. In `WindowActivated()`, for a
window that was just added or re-shown (a 250 ms window to tell a first
activation from a click), the engine copies the area under the window
**plus a 3-pixel ring** around it (`fBackgrounds`, budgeted LRU).

At close time the engine re-reads the ring, which the window never covered
and which is therefore live, and compares it with the remembered ring
(`fx::BackgroundStillValid`, mean absolute difference ≤ 6/255):

- **Ring unchanged.** The remembered background is very likely what the
  windows below will repaint. The proxy then crossfades from the window
  image to that background (`fx::CrossFadeRows`, 0.9 ms per frame for
  800×600 at -O2, measured on the host). The proxy's ServerBitmap is
  updated in place and its view is invalidated server-side each tick. At
  the end the proxy is hidden over an identical background, so the switch
  is seamless.
- **Ring changed, or the window moved or resized (cache dropped).** Fall
  back to the shrink transition.

This is the "precomposed opaque animation frames" approach from the brief,
with the missing piece, a trustworthy background, supplied by capture
timing and validated before use. The opaque proxy displays blended frames,
and nothing pretends to be transparent. Default: off (`fx.lifecycle.fade`).

### Level 3: real fades

With the overlay compositor (§A), a proxy can be an overlay window. Its
opacity then animates with true alpha over the *live* content beneath it,
which app_server keeps repainting, because overlays don't clip windows
below. Experiment F2 animates an overlay's opacity 1 → 0 → 1. The same
mechanism serves panel and menu transitions for Lumina-owned surfaces.
Fading *ordinary application windows* in place still needs a full
compositor.

---

## G. Cursor themes

### Trace

- `CursorManager::SetCursorSet(const char* path)` (`CursorManager.cpp:248`)
  loads a CursorSet file (a flattened `BMessage`) and replaces the system
  cursor for every `BCursorID` it contains. **Nothing in Haiku calls it**
  (searched the whole app_server and kits).
- Applications request system cursors by ID (`BCursor(B_CURSOR_ID_I_BEAM)`
  → token = ID → `ServerApp` `AS_SET_CURSOR` → `CursorManager::FindCursor`).
  Replacing the cursor at that token therefore reaches every native
  application. Custom bitmap cursors have their own tokens and are left
  alone.
- Stock cursors are rendered from vector data at `bold font size / 12`
  (`Desktop.cpp:578`, `CursorManager::InitializeCursors`) and stored
  **premultiplied** (`CursorManager.cpp:454`).

### Upstream defects found (worth reporting)

1. `CursorSet::AddCursor()` writes `"class": "bitmap"`, but
   `CursorSet::FindCursor()` requires `"cursor"` (`CursorSet.cpp:106` vs
   `:181/:234`). Files written by the class itself cannot be loaded.
2. `CursorSet::_CursorWhichToString()` has no case for
   `B_CURSOR_ID_CREATE_LINK`, so it maps to "Invalid". The create-link
   cursor cannot be themed through CursorSet.

### Implementation (Level 2, no patch)

- **Theme format.** `theme.json` plus PNGs per nominal size (22, 33, 44 …),
  with hotspots at the base size (`src/core/lumina/CursorTheme.*`,
  host-tested). It covers 28 roles, including all resize directions, text,
  progress (wait), grab, grabbing and not-allowed.
- **Builder.** `CursorThemes::Build()` (liblumina) picks the size for the
  current UI scale, loads the PNGs (Translation Kit), premultiplies them,
  and writes the CursorSet with the class key app_server's loader accepts.
  It caches the file under `~/config/settings/Lumina/cache/`.
- **Apply.** lumina_server sends the file path with `LUMINA_OP_CURSOR_THEME`.
  The engine calls `SetCursorSet()` and then `Desktop::SetCursor(NULL)` so
  the visible cursor updates immediately. Reset re-runs
  `InitializeCursors(scale)`. Restore-stock and engine unload both reset.
- **Tools.** `lumina cursors`, `lumina cursors check <name>`,
  `lumina cursors use <name>|stock`. Sample theme:
  `data/cursors/LuminaOutline` (original artwork generated by
  `tools/make-sample-cursor-theme.py`).

### Not yet covered

- **Animated cursors.** `ServerCursor` is a static bitmap. Animation would
  periodically swap the cursor at the progress token from the engine,
  driven by the client clock. That is designed, not implemented.
- **Scale changes after login.** The builder picks the size when the theme
  is applied. Re-apply on font-size change (the server already rebuilds on
  every configuration change).

---

## A. Genuine see-through windows and panels

### Why Level 1 and 2 cannot do it

An app_server window's visible region excludes everything above it
(`Desktop::_RebuildClippingForAllWindows`, `Desktop.cpp:3493`). The windows
below never draw where a panel sits. There is no alpha channel between
windows, and the back buffer contains exactly one layer per pixel. Screen
capture (`ReadBitmap`) and edge synthesis (docs/vfx.md) give convincing
*snapshots*: correct when the panel appears and refreshed on workspace
switches. But content moving under the panel is simply not drawn anywhere.

### Approaches evaluated

| # | Approach | Finding |
|---|---|---|
| 1 | Screen-region capture plus dirty tracking (Level 1) | Already implemented (capture-before-show, workspace recapture, edge glass). The region under an *opaque* panel is never redrawn, so no amount of polling can make it live |
| 2 | Off-screen surfaces for every window | Requires each `ServerWindow` to draw into its own `BitmapDrawingEngine` (app_server already has `BitmapHWInterface`/`BitmapDrawingEngine` for `OffscreenWindow`) plus a compositor for all windows. That is a full compositor: large, touching every drawing path, with big memory costs (8 MB per 1080p window) |
| 3 | Hooks in `DrawingEngine` / `HWInterface` | `HWInterface::CopyBackToFront()` (`HWInterface.cpp:351`) is the single path every pixel takes to the display (the accelerant interface is always double-buffered). It is the natural composition point |
| 4 | **Hybrid: overlays only** | Ordinary windows render natively. A few Lumina-owned "overlay" windows do not clip what is below them and are composed in during the back-to-front copy. **Chosen** |

A Level 2 variant of #3, patching `HWInterface`'s vtable from the add-on at
run time, was considered and rejected. It would be undefined behaviour
across compiler versions, would bypass the ABI guard's intent, and would
leave a hook in app_server after the add-on's code is unloaded. A
reviewed, isolated patch is the honest form of the same idea.

### The Level 3 patch

`patches/haiku/0001-app_server-optional-overlay-compositor.patch` applies
cleanly to the revision in use and was type-checked with
`clang -fsyntax-only` for every touched translation unit. **It has not been
built with jam or run.**

| Change | Where | Lines |
|---|---|---|
| Overlay windows get an empty clipping region and don't subtract from `stillAvailableOnScreen` | `Desktop::_RebuildClippingForAllWindows`, `RebuildAndRedrawAfterWindowChange` | ~12 |
| `Desktop::_UpdateOverlays()` after each clipping rebuild: picks up moves, resizes, show/hide and workspace switches of overlays and recomposes old and new footprints | `Desktop.cpp` | ~20 |
| `Desktop::SetWindowOverlay()`; layer removal in `RemoveWindow()` so the compositor never outlives a window | `Desktop.cpp/.h` | ~35 |
| Hit-testing overlays by frame (they have no visible region) | `Desktop::WindowAt` | ~5 |
| Compose before copy; the copy and `_DrawCursor` read the composed buffer | `HWInterface.cpp/.h` | ~30 |
| `Window::IsOverlay()` | `Window.h` | 7 |
| New: `OverlayCompositor` (layers, composed buffer allocated only while overlays exist), `OverlayCompose` (pixel core), `LuminaOverlayApi.h` (versioned C API) | `drawing/` | new files |

Pixel core (`OverlayCompose.cpp`): backdrop blur (3 box passes, optional
1/2 or 1/4 resolution on a screen-aligned grid), tint, content blend with
opacity, antialiased rounded corners and a soft drop shadow. **Composing any
sub-rectangle yields exactly the full-screen result** (unit test: a tiling
of odd-sized rectangles is compared byte-for-byte with one full
composition, at 1×, 2× and 4× downsampling). That is what lets app_server
compose only the rectangles it copies anyway: cost follows damage, and idle
cost is zero. Host timings (-O2): a full 1920×40 glass panel at 1/4
resolution with radius 12 takes 1.8 ms; a 64×40 clock update takes
0.085 ms.

**API and fallback.** The patch exports
`extern "C" lumina_overlay_get_api()`. The engine resolves it with
`get_image_symbol()` on the app_server image, never at link time, so the
same engine binary runs on stock and patched systems. `EngineInfo.compositor`
reports availability. Only Lumina's own windows can be made overlays.

**Known gaps in the patch** (documented, not hidden):
- The backdrop of an overlay samples the back buffer only. Overlays that
  overlap each other don't blur one another, which is fine for panels.
- Overlay content updates need an explicit damage call
  (`LUMINA_OP_OVERLAY_DAMAGE`), because the overlay's view never draws.
- The panel does not yet render into a bitmap to become an overlay.
  Experiment F uses a standalone glass card. Converting the panel is the
  next step.
- Not yet reviewed against `DrawingEngine::CopyRegion` for overlays that
  move while composed (the composed buffer is recomposed from the back
  buffer on every copy, so it should be correct; to verify on hardware).

## B. Live blur and frosted glass

| Tier | Technique | State |
|---|---|---|
| Level 1 | Capture-before-show glass, workspace-switch recapture, edge glass, wallpaper glass, solid; governor picks quality (docs/vfx.md) | Implemented |
| Level 1 refinement | Event-driven refresh: the engine already pushes moved/resized/activated events (coalesced to 30 Hz), and the panel can recapture only when a window *adjacent to* the panel changes. That never works under an opaque panel (see §A) | Designed |
| **Level 3** | Overlay glass: the backdrop is the live back buffer, blurred per damaged rectangle | Implemented in the patch and compose core; untested on hardware |

GPU acceleration: Haiku's app_server draws in software (Painter/AGG), and
accelerant 2D hooks are limited to blits and fills on a few drivers. A GPU
blur path would need a 3D driver stack that app_server does not use. **CPU
with downsampling is the realistic engine.** The governor
(`fx::Governor`) already lowers quality when frame cost exceeds its budget.
The overlay `downsample` parameter is the matching knob at Level 3.

A shared background sampling service, so that panels, menus and widgets
reuse one capture, is natural at Level 3: the back buffer *is* the shared
sample. At Level 1 it is lumina_server's capture cache (designed, not
implemented).

## F. Shadows and rounded corners

| Technique | Level | Result | State |
|---|---|---|---|
| Decorator footprint: `Decorator::_GetFootprint(BRegion*)` (`decorator/Decorator.h:279`) is virtual; a decorator may return any region, and windows below are clipped by it and draw around it | 2 | **Rounded outer corners** with stair-step regions (no antialiasing: edge pixels belong to the windows below) | Designed; the LuminaDecorator subclass hook exists |
| Shadow proxy windows | 1 | Separate windows behind each window. Rejected: they double the window count, fight Stack & Tile and focus order, and their pixels are opaque | Rejected |
| Snapshot shadows | 1/2 | Precomposed shadow over the captured background; valid only while nothing moves | Not pursued (stale under live content) |
| **Overlay shadows and corners** | 3 | Soft shadow plus antialiased corners on overlay windows (panels, cards, proxies during transitions) | Implemented in the compose core and patch |
| Ordinary-window shadows | 3 (extension) | Shadow-only compositor layers clipped to "screen minus windows at or above the shadowed window" (the engine knows the z-order) | Designed |
| Ordinary-window antialiased corners | 3 (extension) | Footprint without the corner pixels, plus a per-window corner sprite rendered by the decorator (it knows its border colours) and blended by the compositor | Designed |

Haiku Classic keeps the stock decorator geometry and look: every one of
these is an optional style.

---

## Multitasking and productivity

Window management, switching, workspaces and overview are covered in
docs/08-multitasking-roadmap.md. Two findings from this research feed it:
- Engine-side capture is also the source for **switcher and overview
  thumbnails**. It is the same copy, taken at activation and minimize, so
  thumbnails of visible windows are fresh and the rest are the last images
  seen.
- Every key event passes the engine's `KeyPressed` hook first, so a
  Lumina switcher can take Ctrl+Tab only while it is running, with stock
  Twitcher as the automatic fallback.

## Test plan (on Haiku)

1. `tools/haiku/build.sh --engine`, then select the engine
   (`windows.engine.enabled`).
2. `tools/haiku/run-fx-experiments.sh`: experiments A–D (existing), E
   (lifecycle: E1 app close, E2 app minimize, E3 restore, E4 obscured skip,
   E5 cursor theme) and F (overlay compositor: on stock it must report
   "not available", which is the expected fallback).
3. Level 3, in a VM only:
   `patches/haiku/apply.sh <haiku-tree>`, build app_server
   (`jam -q app_server`) or a full image, boot it, and re-run F.
4. Send back the report archive (`~/Desktop/lumina-fx-reports-*.zip`) and
   `/var/log/syslog` lines containing `lumina-engine`.
