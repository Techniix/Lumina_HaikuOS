# 09 — System integration architecture

How Lumina's modules fit together across the three integration levels, how
capabilities are detected and negotiated, and what happens when any piece
is missing or fails. Companion to docs/03-architecture.md (the base
architecture), docs/07 (research behind the Level 2/3 features) and docs/08
(multitasking roadmap).

Status vocabulary: **host-tested** (portable code passes unit tests),
**compile-verified** (builds for Haiku with `-Werror`), **type-checked**
(app_server patch translation units pass `clang -fsyntax-only`),
**designed**, **planned**. Nothing has run on real Haiku yet.

## 1. Invariants

1. **Stock Haiku is always reachable.** Every module can be absent, fail or
   be disabled, and the desktop that remains is stock Haiku with Deskbar and
   Tracker.
2. **No hard dependencies between unrelated features.** Snapping doesn't
   need transparency. Cursor themes don't need the Modern personality.
   Lifecycle transitions don't need the panel (they fall back to a generic
   minimize target).
3. **Detection, not assumption.** Every capability is probed at run time and
   reported. Missing capabilities disable features instead of failing.
4. **Nothing of Lumina outlives Lumina.** Engine unload, restore-stock and
   lumina_server exit each revert what they changed (cursors, proxies,
   overlays, parked windows, decorator, Deskbar).
5. **Default off for risk.** Everything in Level 2 beyond the base engine,
   and all of Level 3, is opt-in.

## 2. Levels and modules

```
                        ┌──────────────── user session ─────────────────┐
 Level 1  native        │ lumina_server   panel   lumina CLI   preflet  │
                        │   │ BMessage IPC (Protocol.h)                 │
                        │   │ proxy windows (LifecycleProxies)          │
                        └───┼───────────────────────────────────────────┘
                            │ BWindow::Set/GetDecoratorSettings
                        ┌───▼────────────── app_server ─────────────────┐
 Level 2  enhanced      │ Window Engine (decorator add-on + listener)   │
  (stock app_server)    │   timeline · snapping · parking · lifecycle   │
                        │   capture · cursor themes (CursorManager)     │
                        │        │ get_image_symbol (optional)          │
 Level 3  advanced      │   OverlayCompositor  ◄── patched app_server   │
  (patched)             │   (HWInterface::CopyBackToFront)              │
                        └───────────────────────────────────────────────┘
                        ┌──────────────── input_server ─────────────────┐
 Level 2                │ LuminaInputFilter (emergency chord)           │
                        └───────────────────────────────────────────────┘
```

| Module | Level | Process | Depends on | If it fails | State |
|---|---|---|---|---|---|
| Portable core (`src/core`): config, schema, profiles, rules, easing, timeline, fx strategy, lifecycle policy, overlay compose, cursor themes | — | all | nothing | n/a (pure code) | host-tested |
| lumina_server: modes, registry, effects controller, clock, proxies, cursor theme builder | 1 | own team | core, liblumina | watchdog restores Deskbar; proxies die with the team | compile-verified |
| Panel and applets | 1 | own team | lumina_server (optional) | Deskbar comes back (panel watches the server) | compile-verified |
| CLI, preflet, Deskbar replicant | 1 | own teams | liblumina | none | compile-verified |
| Window Engine | 2 | app_server | stock app_server of the same revision | ABI guard keeps it passive; load failure → app_server uses its built-in decorator | compile-verified |
| Lifecycle transitions | 2 | app_server + lumina_server | engine, proxy pool | per-window fallback to stock hide/minimize | compile-verified |
| Cursor themes | 2 | app_server + lumina_server | engine | stock cursors | compile-verified |
| Input filter | 2 | input_server | none | chord unavailable; CLI `restore-stock` still works | compile-verified |
| Overlay compositor | 3 | app_server | patched app_server, engine | `compositor: false`; Level 1 glass | type-checked patch |

## 3. Capability negotiation

1. **Login.** launch_daemon starts lumina_server only if
   `~/config/settings/Lumina/enabled` exists (`data/user_launch/lumina.in`).
2. **Engine probe.** `EngineLink::Probe` fills `EngineInfo`:
   - `present`: is the decorator add-on loaded?
   - `active`: did the ABI guard pass (build revision equals `uname`)?
   - `compositor`: was `lumina_overlay_get_api` resolved?
   - `statsJson`: counters, including lifecycle proxies and the cursor
     theme.
3. **Effects capabilities.** `VisualEffects::MeasureCapabilities` times
   capture and blur. `fx::SelectPlan` picks glass and transition strategies
   from `fx.*` settings plus capabilities. `fx::Governor` lowers quality
   under load.
4. **Configuration.** `ServerApp::_ConfigureEngine` sends one JSON document
   (`snap`, `animate`, `reserved`, `lifecycle` …). Then, depending on the
   lifecycle setting, it registers or releases the proxy pool, and applies
   or reverts the cursor theme.
5. **Health.** `_CheckEngineHealth` (every 10 s) resubscribes after an
   engine reload. If the engine reports `"proxies":0` while lifecycle is on,
   it re-registers the pool.

Feature gating is therefore a conjunction: user setting ∧ module present ∧
ABI ok ∧ capability measured. Each feature evaluates only its own terms.

## 4. Loading and compatibility

| Boundary | Mechanism |
|---|---|
| Engine ↔ app_server C++ internals | ABI guard: the build revision is compiled in and compared with the running system; on mismatch the engine stays loaded but passive. Missing exported symbols prevent loading, and app_server falls back to its built-in decorator (to verify on hardware) |
| Engine ↔ Level 3 compositor | Run-time `get_image_symbol`; `lumina_overlay_api.version`; `lumina_overlay_params.size`. Fields and functions are append-only |
| Client ↔ engine | Op strings in `Protocol.h`; unknown ops are ignored; full status only for `application/x-vnd.lumina-*` clients |
| Client ↔ server | BMessage protocol (`Protocol.h`); `LuminaClient` timeouts |
| Settings | Schema with defaults and ranges (`ConfigSchemaData.cpp`); unknown keys are reported, not fatal; profiles validated before import |
| Patch ↔ Lumina | `patches/haiku/apply.sh` verifies the embedded compose core is byte-identical to the tested copy; `--check` / `--revert` |

## 5. Fallback matrix

| Feature | Missing or failing piece | Result |
|---|---|---|
| Lifecycle close/minimize | Feature off, no proxy, window < 80% visible, too large, unsupported buffer format | Stock disappearance; `skipped` counter |
| Lifecycle restore | No cached snapshot, size changed | Stock reappearance |
| Lifecycle any | Client clock stops | Animation completes on the next mouse event or engine call; proxies released after `duration + proxyTimeout` |
| Crossfade | Ring check fails, window moved/resized | Shrink transition |
| Cursor theme | Theme missing or invalid, engine absent | Stock cursors; problems logged |
| Overlay glass | Stock app_server | Level 1 glass (capture / edge / wallpaper / solid via `SelectPlan`) |
| Engine | ABI mismatch | Passive engine: identical to the stock decorator; server uses client-side fallbacks |
| lumina_server | Crash or quit | Panel restores Deskbar; app_server removes proxy windows with the team; engine releases proxies on timeout |
| Everything | User emergency | Ctrl+Alt+Shift+Backspace → `lumina restore-stock` (cursors reset, proxies released, animations finished, decorator and Deskbar restored) |

## 6. Safety and resource limits

| Concern | Measure |
|---|---|
| Work under app_server's write lock | One bounded `memcpy` per transition (`maxPixels`); measured as `maxCaptureUsecs` |
| Memory | Proxies: two screen-sized bitmaps, only while lifecycle is on. Snapshot and background caches: LRU with `fx.lifecycle.cacheBudgetMB` (default 48). Composed buffer: one screen copy, only while overlays exist |
| Runaway animations | Client-clocked engine (no engine threads); stale-animation completion; proxy timeout |
| Foreign windows | Proxies and overlays must belong to a Lumina team; overlays are refused for other windows |
| Diagnostics | Engine stats JSON (lifecycle counters, cursor theme, timing); syslog `lumina-engine:` lines; server logs under `~/config/var/log/Lumina`; experiment reports |
| Performance | Governor on measured frame cost; overlay compose scales with damage, so idle cost is zero |

## 7. Test strategy

| Level | Tests |
|---|---|
| Portable | `tests/` (host, CI): policy, caches, crossfade, ring validation, cursor manifests, overlay composition including the partial-equals-full property and timings |
| Level 1/2 | Experiments A–E on Haiku (`tools/haiku/run-fx-experiments.sh`) with JSON reports; acceptance checklist in docs/development.md |
| Level 3 | Experiment F on a patched VM image; on stock Haiku it must report the fallback |

## 8. Open risks

1. Not yet run on Haiku: symbol exports from app_server, timing under real
   drivers, and the exact behaviour of `View::SetViewBitmap` on a window
   shown server-side while the client believes it is hidden.
2. Engine internals change between Haiku revisions; the ABI guard prevents
   damage, but each new revision needs a rebuild and re-test.
3. The installation path for a patched app_server on real hardware is
   unverified (see patches/haiku/README.md).
4. Lifecycle restore via `ActivateWindow()` on a minimized window is not
   notified and therefore not animated.
