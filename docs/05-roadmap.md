# 05 — Phased Implementation Roadmap

Complexity scale: **S** ≤ 2 weeks · **M** 2–6 weeks · **L** 6–12 weeks · **XL** > 12 weeks (one experienced developer).
Every phase ends with a milestone that is demonstrable on both the Haiku VM and real x86_64 hardware.

| Phase | Scope | Depends on | Complexity | Blockers / risks | Milestone (exit criteria) |
|---|---|---|---|---|---|
| **P0 — Foundations** | Repo layout, build system (Haiku `makefile-engine` or CMake for native; CMake for Qt parts), CI that builds on a Haiku image, coding/IPC conventions, test VM images | — | S | Haiku CI runners (self-hosted VM) | `make` builds an empty `liblumina` + hello `lumina_server` on Haiku; CI green |
| **P1 — liblumina core** | Config schema + layered store + JSONC parser over `BJson`, node-monitor live reload, migration, profile load/save; IPC protocol; OS interface (battery, volume, brightness, CPU, mem, disk, power) | P0 | M | Battery ioctls vary by driver | Unit tests pass; `lumina config get/set` CLI round-trips; OS interface demo prints live values |
| **P2 — Window Engine PoC** | `DecorAddOn` that is **pixel-identical to DefaultDecorator**; `DesktopListener` streaming window events; command channel; ABI guard | P1 | **L** | R1 (ABI), R2 (crash = app_server down); debugging inside app_server | Select "Lumina" in Appearance → no visual change; `lumina windows --watch` shows live events; `lumina win move <token> x y` works; switching back to Default unloads cleanly |
| **P3 — lumina_server & Mode switching** | Launch file, session job, stock snapshot/restore, Stock ↔ Lumina switching, crash watchdog, Lumina Mode preflet + Deskbar replicant + CLI, emergency chord | P1, P2 | M | R6 (Deskbar disable/restore through `BLaunchRoster`) | 100 switch cycles with no drift in settings; kill -9 tests trigger automatic fallback; Safe Mode boot starts stock |
| **P4 — Lumina Panel** | Native panel windows, applet add-on API, stock applets (leaf menu, team list, launchers, tray, workspaces, clock, power, volume, show-desktop, spacer, JSON menu), multi-panel/multi-screen | P1, P3 | **L** | Tray parity with Deskbar replicants; work-area needs P5 for full effect | A panel can replace Deskbar for a full day of use; applets also work as Tracker desktop replicants |
| **P5 — Window management** | Snap zones, keyboard tiling, placement presets, rule engine, per-app decorator overrides, work-area reservation, S&T cooperation | P2, P3 | **L** | Interaction with Stack & Tile; focus-stealing edge cases | Rule test suite (Terminal, WebPositive, Qt and Xlibe apps) passes; maximise respects panels |
| **P6 — Settings Center** | Qt 6/QML app: search, live preview, try-mode with revert, all pages; KArchive import/export | P3 (P4/P5 for their pages) | **L** | QPA quirks; keeping Qt fully optional | Every schema key editable; Settings can be uninstalled without breaking anything |
| **P7 — Desktop widgets** | Native replicant widgets; QML widget host with `layer: desktop` rule; Lumina's calendar/notepad/RSS/system monitor/audio re-done in QML; per-workspace layouts | P4, P6 | M | Desktop-layer placement for Qt windows (R5) | Widgets survive reboot, workspace switches and Tracker restarts |
| **P8 — Personalities & sharing** | Classic, Modern, Power User, Minimal profiles; duplicate/edit/export/import `.luminaprofile`; MIME type + Tracker integration; script approval | P4, P5, P6 | M | Getting Classic "indistinguishable" right | Blind A/B: testers can't tell Classic from stock at first glance; profile round-trips between two machines |
| **P9 — Themes & polish** | Theme packs (decorator params, colour schemes, wallpapers, sounds), icon-pack overlay with backup/restore, notification behaviour, panel animations with a performance budget, localisation via Haiku Locale Kit catalogs | P8 | M | Icon overlay must be fully reversible; cursor themes blocked upstream | A theme pack installs from a repo and applies/reverts cleanly |
| **P10 — Release** | HaikuPorts recipe(s), user docs, upgrade path across hrevs (engine rebuilds), bug bash | P9 | M | HaikuPorts review; hrev-pinning policy | `pkgman install lumina` on a clean beta6 → working Classic; uninstall → stock |

**Total:** ≈ 9–14 person-months to 1.0. The critical path is **P0 → P1 → P2 → P3 → P5 → P8**.

## Decision gates

* **After P2:** if the in-server engine proves too fragile on real hardware, fall back to **"engine-lite"**. That means no in-server listener, just window list and raise/minimise via `WindowInfo.h`, with snapping/tiling/rules deferred until a stable upstream API exists. Panels, personalities, widgets and mode switching remain fully viable.
* **After P4:** decide whether Modern replaces Deskbar by default or keeps it.
* **After P6:** choose Kirigami vs plain QtQuick.Controls based on memory footprint measurements.

## Testing strategy

* VirtualBox Haiku VM (snapshot per test run) plus the real-hardware x86_64 machine.
* App matrix for every WM feature: native (Terminal, StyledEdit, WebPositive, Tracker windows), Qt 6, Xlibe/GTK and WINE.
* Crash drills: kill each Lumina process, corrupt config files, mismatched hrev engine, Safe Mode boot.
* Performance budget: in Classic, Lumina adds ≤ 15 MB RSS and no measurable input latency; Qt is not loaded in Classic or Minimal unless QML widgets are enabled.
