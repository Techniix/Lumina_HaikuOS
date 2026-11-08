# Visual effects on a non-compositing app_server

This chapter records what Lumina's visual-effects research found, what was
built, and what is honestly impossible without changing Haiku. It is the
reference for `src/core/lumina/fx`, `src/liblumina/lumina/haiku/VisualEffects.*`,
`src/server/EffectsController.*`, `src/panel/PanelSkin.*` and `experiments/`.

> **Verification status.** Everything below compiles against real Haiku
> headers. The CPU-side numbers were measured (host CPU, see the table).
> **No technique has run on Haiku yet.** Each prototype in `experiments/`
> runs its hypothesis as an automatic self-test and writes a JSON report;
> run `tools/haiku/run-fx-experiments.sh` on real hardware or a VM to
> confirm or refute them. Integration choices are marked *provisional*
> until those reports exist.

## 1. Facts about app_server that decide everything

Read from the Haiku sources (paths relative to `src/servers/app/`):

| Fact | Source | Consequence |
|---|---|---|
| No per-window backing store; windows draw straight into the (back) frame buffer, clipped to their visible region | `Window.cpp`, `View.cpp` | Pixels under an opaque window **do not exist anywhere**. Nothing can blur or tint "what is behind" a visible panel |
| A view whose colour is `B_TRANSPARENT_COLOR` is never pre-filled | `View::Draw()` (`fViewColor != B_TRANSPARENT_COLOR`) | A transparent window shows whatever was in the frame buffer — at the moment it appears that is the *true* background; afterwards it is stale |
| `BScreen::ReadBitmap()` copies from the **front buffer** and hides floating overlays (cursor) during the copy | `ServerApp.cpp` `AS_READ_BITMAP` → `DrawingEngine::ReadBitmap()` | Readback costs a VRAM read (driver dependent, must be measured); capturing next to the cursor can make it blink |
| Default `BView` view colour is opaque white, including BWindow's top view | `src/kits/interface/View.cpp` `ViewState()` | Panels must use a full-size transparent root view or layout gaps show white (fixed in `PanelWindow`) |
| Desktop listeners are unregistered under the desktop write lock and the add-on image is unloaded right after | `Desktop::ReloadDecor()`, `DecorManager::SetDecorator()` | The Window Engine must own **no threads** (see `docs/engine.md`) |
| Moving a window is a blit; resizing makes the client re-layout and redraw | `Desktop::MoveWindowBy/ResizeWindowBy` | Animate position freely; throttle or defer size changes |

## 2. Techniques, evidence and status

### A. Native alpha blending in a panel window — `experiments/fx-alpha-panel`

Hypotheses the self-test checks:

* **H1 reveal capture** — a transparent window that draws nothing leaves the
  pixels underneath intact, so they can be read back right after `Show()`.
* **H2 no compositing** — when the window underneath repaints or moves, the
  panel region does not follow.
* **H3 accumulation** — `B_OP_ALPHA` fills into a transparent view blend
  with the panel's *own previous output*: repeated redraws darken.
* **H4 snapshot composite** — drawing a captured background first, then the
  translucent content, is stable.
* **H5 offscreen** — `B_ALPHA_COMPOSITE` (not `B_ALPHA_OVERLAY`) yields a
  correct alpha mask when drawing into a transparent `B_RGBA32` bitmap.

**Expected result (from the source facts):** H1–H5 pass. Then: genuine
per-window translucency is impossible without app_server compositing;
`B_OP_ALPHA`/`B_ALPHA_*` are useful only *within* our own surfaces and for
building masks offscreen. **Status:** prototype complete, awaiting report.

### B. Screen-capture-assisted glass — `experiments/fx-capture-glass`

| Variant | What it does | Flicker | Status |
|---|---|---|---|
| **edge** | Sample the live screen band *adjacent* to the panel, mirror it into the panel area, blur/tint | none | **integrated** (`fx.panelGlass.mode = edge`, Modern) |
| **true capture before show** | Capture the panel rectangle while the panel is not on screen, render glass, then `Show()` | none by construction | **integrated** (`capture` mode) |
| **true capture during workspace switch** | Hide → capture → show while the whole screen repaints anyway | masked by the switch (≤ 80 ms bounded) | **integrated** (`capture` mode), provisional |
| hide-capture-show while idle | Hide the panel to sample, show again | the hidden time *is* flicker | measured only (B4/B5); not used unless it measures ≤ 1 frame |
| wallpaper | Frosted rendering of the desktop image region from Tracker's `be:bgndimginfo` attribute | none | **integrated** (`wallpaper`) — does not show windows |

The `capture` mode in the panel therefore means: true pixels whenever the
panel isn't covering them, edge-synthesised refreshes in between (only when
the surroundings change significantly), never a deliberate blink.

Targets checked by the prototype: edge refresh ≤ 2 ms for 1920×32;
hide-capture ≤ 16.7 ms to be usable at all; first glass paint ≤ 33 ms;
readback < 40 ms/MP for the automatic "capture" choice.

**CPU cost of the glass pipeline** (measured, `-O2`, single thread, Intel
Xeon @ 2.1 GHz host — Haiku numbers come from the B2/B3 reports):

| Panel | downsample 1 | downsample 2 (Medium) | downsample 3 (Low) |
|---|---|---|---|
| 1920×32 | 1.46 ms | **0.91 ms** | 0.71 ms |
| 2560×40 | 2.52 ms | 1.50 ms | 1.28 ms |
| 3840×48 | 4.84 ms | 2.79 ms | 2.54 ms |
| edge synth + glass, 1920×32 | | 1.03 ms | |

(Averages of 200 runs. The first version of the pipeline took 1.49 ms for
1920×32 at downsample 2; the optimisations below brought it to 0.91 ms.)

Optimisations already applied: blur at reduced resolution (box blur ×3 ≈
Gaussian, O(1) per pixel); grade and tint moved *before* upscaling (they are
linear, so they commute with bilinear filtering — exact, 1/4ⁿ the work);
precomputed bilinear column tables; dither and alpha fused into one pass;
change detection (`SampledDifference`, 0.13 ms for a full screen) so an
unchanged band costs only the readback.

### C. Real window geometry animation — `experiments/fx-window-anim`

The Window Engine interpolates the **real** window frame inside app_server
(`docs/engine.md`): position every frame (cheap blit), size per
`resizeMode` — `live` (every frame), `throttled` (≤ 30 Hz, default) or
`final` (once, at the end). Curves: easings, CSS cubic-bezier, and an
**analytic** damped spring (exact position/velocity at any time, settle time
computed), with velocity-preserving retargeting so a window snapped again
mid-flight curves smoothly to its new target.

The prototype acts as the frame clock and samples the window's real frame
every 2 ms to measure achieved fps, lag vs. the curve, landing accuracy and
client `Draw()`/`FrameResized()` counts per resize mode.

**Status: integrated.** Snapping animations and the `animate` command are
production paths (off by default, on in Modern/Power User). Timeline cost:
0.006 ms per tick for 64 simultaneously springing windows (host).

Limits: S&T is not notified during engine moves (notifications are
suppressed inside our hook), so stacked windows are skipped entirely.

### D. Snapshot proxies — `experiments/fx-snapshot-proxy`

A proxy is a borderless always-on-top window showing a bitmap. On Haiku the
crucial property is that a proxy animating **geometry** (move/scale) always
uncovers the **live** desktop — app_server repaints what it uncovers — so
zoom/scale transitions need no frozen background at all.

| Effect | Mechanism | Status |
|---|---|---|
| Minimize to panel | capture window → proxy on top (pixel-identical) → minimize under it → shrink to the button | **integrated** for panel-initiated minimize |
| Restore from panel | grow from the button with the image cached at minimize (else a window placeholder with the app icon) → raise real window under it → 40 ms settle → remove | **integrated** |
| Open | Engine **parks** a new window off-screen in `WindowAdded()` (before its first `Show()`, so it can't flash) → placeholder zooms from the launcher → `unpark` → remove | **integrated** (`fx.proxies.open`), engine required |
| Workspace slide | capture current workspace, cover screen, switch underneath, slide to the cached image of the target (desktop colour on first visit), uncover live | **integrated** (`fx.proxies.workspace = slide`) for Lumina-initiated switches |
| Close | only if Lumina initiates the close (capture first); see §3 | prototype only |
| Fade | needs to see *through* the proxy → requires a frozen background; possible as a one-frame-hide capture but not flicker-free | not implemented |

## 3. What is impossible without app_server changes — and why

> **Superseded (2026-11-08).** The table below was the Level 1 (client-side)
> verdict. docs/07-advanced-integration-research.md re-examined every row in
> the app_server source. Close and minimize animations started *anywhere*,
> cursor themes, verified-background fades and rounded corners are possible
> on stock Haiku from the Window Engine (Level 2). True translucency, live
> blur, soft shadows and real fades are possible for Lumina's own surfaces
> with a small optional app_server patch (Level 3). The rows are kept for
> history; in particular, the "close" row was wrong for in-app_server code:
> the pixels are still intact when `WindowHidden()` runs.

| Wanted | Why not | Closest honest alternative (implemented) |
|---|---|---|
| Per-window translucency (see-through windows/panels) | No compositing, no backing stores: blended pixels underneath don't exist | Frosted glass from captured/adjacent pixels (frozen or edge-synthesised); never called "transparency" in the UI |
| Live blur of moving content behind a visible panel | Same | Edge glass follows content *near* the panel |
| Close animations for app-initiated closes | The window hides before any listener hook runs; its pixels are erased in the same locked call chain | Lumina-initiated closes can animate; others close instantly |
| Minimize animations for minimizes Lumina didn't start (tab double-click, keyboard) | `WindowMinimized()` fires after the window is hidden | Future: a custom `WindowBehaviour` in the engine can intercept the decorator's minimize action before it happens |
| Window drop shadows / rounded window corners | Window shapes and per-pixel window alpha are not supported by app_server | none (decorator can draw borders differently in future styles) |
| Cursor themes | No cursor-theme setting in app_server | none |

## 4. The unified engine

```
lumina_core (portable, unit-tested)          liblumina (Haiku backends)          users
───────────────────────────────────          ─────────────────────────          ─────
EffectProfile  ← config (fx.*, windows.*)    VisualEffects::MeasureCapabilities  EffectsController
Capabilities   ← measured                    VisualEffects::CaptureScreen         (lumina_server)
Governor       ← per-frame cost              VisualEffects::RunProxyTransition    PanelSkin (panel)
SelectPlan()   → EffectPlan                  VisualEffects::RunWorkspaceSlide     Window Engine
anim::Curve / Spring / Timeline / Stepper                                         (geometry)
fx::Image ops / GlassRenderer
```

* **Policy vs mechanism**: `SelectPlan()` decides strategies from the user's
  profile, the measured capabilities and the governor's quality level;
  backends only execute plans.
* **Automatic fallback chain**: capture → edge → wallpaper → solid glass;
  engine animation → instant; proxy → instant. Every action still happens
  when its effect is unavailable.
* **Governor**: per-frame cost of proxies and captures feeds a hysteresis
  governor (8 net over-budget frames → one level down, at most every 0.5 s;
  5 s under half budget → one level up). `fx.quality = auto` lets it roam.
* **Zero cost when off**: with `fx.quality = off` (Classic, Minimal) nothing
  is measured, no capture happens, no proxy windows exist.

## 5. Running the prototypes

```sh
tools/haiku/build.sh --engine
lumina mode lumina modern          # experiment C/D need the engine
tools/haiku/run-fx-experiments.sh  # ~1 min; don't touch the machine
```

Reports land in `~/config/var/log/Lumina/fx-reports/` and are zipped to the
Desktop. Each contains the measurements, the hardware, and PASS/FAIL
verdicts for every hypothesis and target.
