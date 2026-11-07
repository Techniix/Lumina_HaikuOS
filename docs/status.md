# Implementation status

*Updated 2026-11-07.* The legend is deliberately strict:

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
host:  43 unit tests / 480+ checks  (ctest: core, personalities)
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
8. Stack & Tile is not notified of engine-driven moves.
9. `fx.proxies.restore` is in the schema but restore transitions are
   always enabled together with `fx.proxies.minimize` for now.
