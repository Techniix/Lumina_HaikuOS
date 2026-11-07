# 06 — Recommended Initial Proof of Concept

## Goal

Retire the three highest risks before writing any user-facing feature:

* **R1/R2:** can a Lumina decorator add-on observe and command windows inside app_server safely?
* **R6:** can we switch between Stock Haiku and Lumina reliably, including disabling and restoring Deskbar?
* **Native panel viability:** can a native panel show a live, correct window list for native, Qt and Xlibe apps?

## Scope ("PoC-1", ≈ 4–6 weeks)

1. **`LuminaEngine` decorator add-on**
   * Subclass `DefaultDecorator` with **no visual change**.
   * A `DesktopListener` that logs `WindowAdded/Removed/Moved/Resized/Activated/WorkspacesChanged` to a ring buffer and forwards them to a registered `BMessenger`.
   * A command handler for one operation, `move-resize(token, frame)`, received via `AS_TALK_TO_DESKTOP_LISTENER`.
   * One snapping behaviour: drag to the left/right screen edge → half-screen.
   * An ABI guard that compares the build hrev with the running one.
2. **`lumina_server` (minimal)**
   * Launch-daemon job gated by `if setting ~/config/settings/Lumina/session enabled` and `no_safemode`.
   * Stock snapshot (decorator, UI colours, Deskbar settings and enabled state) and restore.
   * `lumina mode stock|lumina` CLI and a crash counter.
3. **`LuminaPanel` (minimal)**
   * One bottom panel: leaf menu (reads Deskbar's menu folder), team/window list from the server's registry, clock.
   * In "Power User" mode, Deskbar is disabled via `BLaunchRoster`, and restored on quit or crash.
4. **No Qt at all** in PoC-1. Settings are edited as JSONC files.

## Success criteria

| # | Criterion |
|---|---|
| S1 | With the Lumina decorator selected, screenshots of 10 standard windows are pixel-identical to Default |
| S2 | The window list matches Deskbar's for Terminal, Tracker, WebPositive, a Qt 6 app and an Xlibe/GTK app |
| S3 | Edge-snap works on all five app types |
| S4 | 100 automated Stock ↔ Lumina cycles leave `~/config/settings` byte-identical to the snapshot (minus Lumina's own folder) |
| S5 | `kill -9` of `lumina_server` or the panel, three times in 60 s → automatic Stock mode, Deskbar visible, notification shown |
| S6 | Safe Mode boot → stock desktop; "Disable user add-ons" → engine not loaded (verify install location) |
| S7 | Engine built for a different hrev → loads as a plain pass-through decorator and logs "engine disabled" |
| S8 | No app_server crash across a 48 h soak on real hardware with normal daily use |

## Suggested repository layout

```
Lumina_HaikuOS/
├─ docs/                     # this research
├─ src/
│  ├─ liblumina/             # config, profiles, IPC, OS interface, WindowInfo wrapper
│  ├─ server/                # lumina_server
│  ├─ engine/                # app_server DecorAddOn (decorator, behaviour, listener)
│  ├─ input/                 # input_server filter
│  ├─ panel/                 # panel app + applets/
│  ├─ mode/                  # Lumina Mode preflet, Deskbar replicant, CLI
│  ├─ settings/              # Qt6/QML Settings Center (later)
│  └─ widgethost/            # Qt6/QML widget host (later)
├─ data/
│  ├─ launch/                # launch_daemon user file
│  ├─ personalities/         # classic/ modern/ poweruser/ minimal/
│  └─ mime/                  # application/x-vnd.Lumina-Profile
├─ tests/
└─ haikuports/               # recipe drafts
```

## Upstream reading list before coding

* `src/servers/app/decorator/{DecorManager,Decorator,DefaultDecorator,DefaultWindowBehaviour,MagneticBorder}.*`
* `src/servers/app/DesktopListener.h`, `src/servers/app/Desktop.cpp` (listener registration and decorator switch)
* `src/servers/app/stackandtile/*`: the reference in-server listener
* `src/kits/interface/WindowStack.cpp`: the client side of `AS_TALK_TO_DESKTOP_LISTENER`
* `src/add-ons/decorators/*`: four add-on decorators to model packaging on
* `headers/private/interface/WindowInfo.h`, `src/apps/deskbar/{TeamMenu,WindowMenu,ExpandoMenuBar}.cpp`
* `headers/private/app/LaunchRoster.h`, `data/launch/user`
* `threedeyes/qt6-haikuplugins/src/platform/qhaikuwindow.cpp`: flag → look/feel mapping
