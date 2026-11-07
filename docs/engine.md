# The Lumina Window Engine

`src/engine/` — an app_server **decorator add-on** (`LuminaEngine`) that
provides a decorator plus a `DesktopListener`. Selecting it as the
decorator (Appearance, or `lumina mode lumina`) loads it into app_server;
selecting any other decorator unloads it. No Haiku file is modified.

## What it does today

| Feature | How |
|---|---|
| Stock look | `LuminaDecorator` *is* `SATDecorator` — what stock Haiku allocates (`DecorAddOn::_AllocateDecorator`) — with no overrides → pixel-identical tabs, borders, buttons and Stack & Tile highlights. Users on another decorator keep it (`windows.engine.replaceDecorator`) |
| Window events | added (with hidden flag), removed, moved, resized (coalesced ≤ 30 Hz), activated, workspaces, hidden, minimized, snapped, animation-done, parked/unparked — pushed as `BMessage`s to subscribers. Engine-initiated changes are reported explicitly, because listener notifications are suppressed inside our own hooks |
| Commands | `move-resize`, `animate` (curve, duration, resize mode), `cancel`, `finish` (land everything now), `minimize`, `activate`, `workspace`, `configure`, `reserve`, `list`, `tick`, `unpark` |
| Snapping | drag-release at an edge/corner → halves, quarters, maximize; respects areas reserved by panels; animated or instant |
| Geometry animation | real window frames interpolated in app_server (`docs/vfx.md` §C) |
| Open transitions | new normal windows parked off-screen before their first show, for a proxy to animate into (opt-in). If the app positions its window itself before `Show()` (e.g. `CenterOnScreen()`), that position becomes the target and the window stays parked; unparking moves to the intended *absolute* position. Auto-unpark after 0.8 s from any mouse/key/activation hook |
| ABI guard | compares the hrev it was built for with `uname`; on mismatch every window-changing feature is disabled and `lumina engine` says why |

## Design decisions (and the reasons found in app_server's source)

**Thread-free, client-clocked.** `Desktop::ReloadDecor()` unregisters
listeners while holding the desktop write lock, and `DecorManager` calls
`unload_add_on()` immediately afterwards. An engine-owned thread would
either deadlock (joining a thread blocked on that lock) or execute code from
an unloaded image. So the engine owns no threads: all of its code runs in
app_server's own hooks with the write lock held, and animation frames are
advanced by a `tick` command that lumina_server's `EngineClock` sends at the
frame rate *only while something animates* (it sleeps otherwise).
If the clock stops, the next mouse event lands stale animations (and
parked windows) on their targets: nothing can get stuck half-way.

**Public command channel.** Commands travel through
`BWindow::SetDecoratorSettings()` / `GetDecoratorSettings()` on a hidden
window of the client — public BWindow API that app_server forwards to every
desktop listener. (`AS_TALK_TO_DESKTOP_LISTENER` would need BWindow's
private link.) If the engine isn't loaded, app_server simply ignores the
message: the fallback is "nothing happens", never a crash.

**Nested locking.** `Desktop::MoveWindowBy` and friends take the write lock
again; Haiku's `rw_lock` is recursive for its holder (`owner_count`), which
is exactly how Stack & Tile moves windows from its hooks.

**Bounded, non-blocking event delivery.** Events are sent with a zero
timeout; a busy client loses events (counted in the stats) instead of
stalling app_server.

## Building

The engine compiles against app_server's internal headers, which only exist
in the Haiku source tree, so the build needs `LUMINA_HAIKU_SOURCE_DIR`
pointing at a checkout of **the same revision as the running system**
(`tools/haiku/build.sh --engine` fetches a sparse one automatically). It
links against the `app_server` binary, like Haiku's own decorators.

Rebuild it after every Haiku update. A mismatched engine still loads as a
plain Default decorator (the ABI guard disables everything else), but
because the add-on shares app_server's internal class layouts, the package
pins the exact `haiku` version as the real protection. Override for
testing: `touch ~/config/settings/Lumina/engine-allow-abi-mismatch`.

## Not yet

* Parametric decorator styles (`windows.decorator.style` only accepts
  `haiku`).
* Keyboard tiling (`KeyPressed()` never consumes keys yet).
* Intercepting the decorator's own minimize/zoom actions (needs a custom
  `WindowBehaviour`), so minimize animations only play when Lumina starts
  the minimize.
* Stack & Tile is not notified about engine-driven moves, so windows in a
  stack are never moved, animated or snapped by the engine (tiled-but-not-
  stacked S&T groups are not detected yet).
* A decorator switched away and back (Appearance) creates a new engine
  instance; lumina_server notices within 10 s and re-attaches.
