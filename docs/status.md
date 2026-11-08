# Implementation status

*Updated 2026-11-08.* The legend is deliberately strict:

* **tested** — covered by automated tests that run (host and Haiku)
* **compile-verified** — compiles with `-Werror` against real Haiku
  headers (clang, `x86_64-unknown-haiku`); **not yet run on Haiku**
* **prototype** — research code in `experiments/`, never installed
* **planned** — designed, not written

> Nothing has been executed on a Haiku system yet. The first real-hardware
> run is the next step (`tools/vm/README.md`, acceptance checklist S1–S8).

## Components

| Component | Path | Status |
|---|---|---|
| Config: JSONC, schema, layered store, diffs | `src/core/lumina` | **tested** (host) |
| Profiles: load/save/duplicate, bundles (export/import, checksum, script stripping, path-traversal guard) | `src/core/lumina/Profile.*` | **tested** |
| Window rules, placement presets, snap zones | `src/core/lumina/Rules.*` | **tested** |
| Logging (ring buffer, rotating file, syslog sink) | `Log.*`, `HaikuLog.*` | core **tested**; syslog sink compile-verified |
| Animation: easings, cubic-bezier, analytic spring, retargetable timeline, geometry stepper | `src/core/lumina/anim` | **tested** |
| Bitmap effects, glass pipeline, strategy selection, governor | `src/core/lumina/fx` | **tested** |
| Shipped personalities (Classic, Modern, Power User, Minimal) | `data/personalities` | **tested** (schema-valid; Classic = stock behaviour) |
| liblumina: IPC protocol & client, private-API compat layer, paths, mode control, system snapshot/restore, OS interface, visual-effects backends | `src/liblumina` | compile-verified |
| Window Engine (decorator add-on, listener, ABI guard, events, commands, snapping, geometry animation, parking) | `src/engine` | compile-verified against app_server headers |
| lumina_server (config broker, modes, watchdog, registry, rules, engine clock, effects controller) | `src/server` | compile-verified |
| Lumina Panel (launcher, window list, workspaces, clock, show-desktop, spacer; applet add-on API; solid/wallpaper/edge/capture skins) | `src/panel` | compile-verified |
| `lumina` CLI | `src/cli` | compile-verified |
| Lumina Mode preflet + Deskbar replicant + `.luminaprofile` import | `src/mode` | compile-verified |
| Emergency chord input filter | `src/input` | compile-verified |
| launch_daemon integration, install layout | `data/user_launch`, `cmake/LuminaInstall.cmake` | written; untested |
| VFX prototypes A–D with self-tests and reports | `experiments/` | prototype, compile-verified |
| Lifecycle policy, snapshot cache, crossfade, background ring check | `src/core/lumina/fx/Lifecycle.*` | **tested** (host) |
| Engine lifecycle capture + proxies (close/hide/minimize/restore from any initiator; optional crossfade) | `src/engine/LuminaEngineLifecycle.cpp`, `src/server/LifecycleProxies.*` | compile-verified |
| Panel → engine button anchors | `src/panel/BuiltinApplets.cpp` | compile-verified |
| Cursor themes: manifest, sizes, premultiply | `src/core/lumina/CursorTheme.*` | **tested** (host) |
| Cursor themes: CursorSet builder, engine apply/reset, `lumina cursors`, sample theme | `src/liblumina/.../CursorThemes.*`, engine, CLI, `data/cursors` | compile-verified |
| Overlay compose core (blur, tint, opacity, rounded corners, shadow; partial = full) | `src/core/lumina/fx/OverlayCompose.*` | **tested** (host) |
| Optional app_server overlay compositor patch | `patches/haiku/` | applies cleanly; type-checked; **not built or run** |
| Engine bridge to the compositor (run-time symbol lookup) | `src/engine/LuminaEngineOverlay.cpp` | compile-verified |
| Experiments E (lifecycle, cursor theme) and F (overlay compositor) | `experiments/fx-lifecycle-engine`, `experiments/fx-overlay-compositor` | prototype, compile-verified |
| Haiku compile-check toolchain + CI | `cmake/`, `tools/`, `.github/` | working (this repo's builds) |
| VM / remote test scripts | `tools/vm`, `tools/haiku` | written; untested |
| HaikuPorts recipe | `haikuports/` | draft |
| Settings Center (Qt 6/QML) | — | planned (milestone 6) |
| QML widget host, desktop replicant widgets | — | planned |
| Parametric decorator styles | — | planned |
| Keyboard tiling, custom WindowBehaviour | — | planned |
| Theme packs, icon packs, sound themes | — | planned |
| Notification appearance control | — | planned (uses Haiku's Notifications preflet today) |

## Tests

```
host:  57 unit tests / 560+ checks  (ctest: core, personalities)
haiku: the same suite runs on Haiku via tools/haiku/build.sh
```

## Known limitations & technical debt

1. **Untested on Haiku.** Highest risk: runtime behaviour of the engine
   inside app_server (lock nesting, hook timing) and private API details
   (`WindowInfo`, `BLaunchRoster` job names). Mitigated by the ABI guard,
   the thread-free engine design and instant fallbacks, but only a real run
   proves it.
2. The engine needs a Haiku source tree at the exact revision; the
   HaikuPorts recipe still clones it (HaikuPorts builds offline).
3. Rules' placement in lumina_server approximates decoration insets from
   the window's tab/border metrics (the engine's own snapping is exact).
4. The panel polls the window list once a second in addition to server
   notifications (cheap, mirrors Deskbar; could become purely event-driven).
5. Panel glass refresh cost is not yet fed to the governor (proxies are).
6. Battery/brightness/volume applets aren't written; `OSInterface` has
   battery and brightness, volume (Media Kit) is missing.
7. Deskbar "hidden" mode relies on launch_daemon job `x-vnd.Be-TSKB`
   (`data/launch/user`); verify on the target Haiku.
8. Stack & Tile is not notified of engine-driven moves; stacked windows
   are skipped, tiled groups aren't detected yet.
12. lumina_server is a launch_daemon *job* (not an auto-restarting
    service): the crash watchdog counts unclean exits across logins, and if
    the server dies mid-session the panel restarts Deskbar so the user is
    never left without it.
13. Window rules apply when a window is first activated (at "added" it is
    still hidden and the app may still position it).
14. Lifecycle transitions call more app_server internals than before
    (`View::SetViewBitmap`, `CursorManager`, `DrawingEngine` locks). If any
    symbol is not exported by the app_server binary, the add-on will not
    load and app_server falls back to its built-in decorator; verify first.
15. Windows less than 80% visible are not animated (their hidden pixels
    don't exist); restores via `BWindow::Activate()` on a minimized window
    are not notified and appear without animation.
16. The panel does not yet render into a bitmap, so it can't be an overlay
    window; Level 3 is exercised by experiment F only.
17. The Level 3 patch has not been built with jam. Installing a patched
    app_server on real hardware has no verified reversible path yet; use a
    VM image (patches/haiku/README.md).
18. Animated cursors are designed but not implemented.

Rule matching now receives window feel (and look, from engine events), and
rule placement honours panels on all four edges (both gaps were found while
writing docs/08).

An independent review of the engine and server against the app_server
sources (2026-11-07) found 12 issues; all are fixed in the code (parking vs.
client positioning, suppressed notifications, rule timing, exit landing,
engine re-attach, quit-time lifetimes, self-messaging, snapshot retirement,
SATDecorator parity, keyboard-launched parked windows, Deskbar-poll cost).
9. `fx.proxies.restore` is in the schema but restore transitions are
   always enabled together with `fx.proxies.minimize` for now.
10. "Restore stock" restores only what Lumina changes (decorator, Deskbar);
    `lumina restore-stock --full` also resets colours, control look, mouse
    and workspaces to the pre-Lumina snapshot.
11. If your decorator isn't Default, Lumina keeps it and runs without the
    engine (no snapping/animation/rules) unless
    `windows.engine.replaceDecorator` is set — the engine's decorator only
    exists in the Default look so far.
