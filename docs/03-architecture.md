# 03 — Proposed Architecture: Lumina for Haiku

> **Implementation notes (2026-11).** Where the code deviates from this
> original design, the code and these documents win:
> * The Window Engine owns **no threads** and is clocked by lumina_server;
>   commands use the public `BWindow::Set/GetDecoratorSettings()` path
>   instead of `AS_TALK_TO_DESKTOP_LISTENER` → [engine.md](engine.md).
> * Panel "transparency" is frosted glass from captured or adjacent pixels,
>   and transitions are snapshot proxies → [vfx.md](vfx.md).
> * The emergency chord is an always-on input filter (no setting).
> * Current state of every component → [status.md](status.md).

## 1. Design principles

1. **Haiku first.** app_server draws and manages every window. Deskbar, Tracker and the preflets keep working. Lumina *adds to* and *orchestrates* existing system mechanisms rather than replacing them.
2. **The default *is* Haiku.** The Haiku Classic personality must be indistinguishable from stock Haiku at first glance: same decorator pixels, same Deskbar, same Tracker desktop, same colours and fonts. It adds capability, not a new look.
3. **Mechanism in the system, policy in Lumina.** Code that runs inside app_server is a thin mechanism layer. Decisions such as rules, layouts and themes are made in an ordinary user-space process that can crash without taking the desktop down.
4. **No OS modification.** Everything installs as a package or into `~/config`. Nothing patches Haiku, app_server, Deskbar or Tracker.
5. **Always reversible.** Stock Haiku is a first-class mode, restorable in one action, automatically after repeated crashes, and from the boot menu.
6. **Qt is optional.** Native components never link Qt. Qt/QML is loaded only for the Settings Center and QML widgets.
7. **Everything is a profile.** Personalities are data, not code.

## 2. Component overview

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                               user space                                     │
│                                                                              │
│  ┌───────────────┐  BMessage   ┌──────────────────┐  BMessage  ┌───────────┐ │
│  │ Lumina        │◄───────────►│  lumina_server   │◄──────────►│ Lumina    │ │
│  │ Settings      │             │ (session, config,│            │ Panel     │ │
│  │ Center (Qt6/  │             │  modes, rules,   │            │ (native,  │ │
│  │ QML,optional) │             │  window registry)│            │ N panels) │ │
│  └───────────────┘             └──┬────────┬──────┘            └─────┬─────┘ │
│  ┌───────────────┐                │        │ BLaunchRoster           │       │
│  │ QML Widget    │◄───BMessage────┘        ▼                         │       │
│  │ Host (Qt6,    │                 launch_daemon ── Deskbar / Tracker│       │
│  │ optional)     │                                  (stock, kept)    │       │
│  └───────────────┘   ┌───────────────┐  ┌────────────────────┐       │       │
│                      │ Lumina Mode   │  │ Desktop replicants │       │       │
│                      │ preflet + CLI │  │ (native widgets in │       │       │
│                      │ (native, no Qt)  │  Tracker's BShelf) │       │       │
│                      └───────────────┘  └────────────────────┘       │       │
├──────────────────────────────────────┬───────────────────────────────┼───────┤
│ input_server                         │ app_server                    │       │
│  └ LuminaInput filter (shortcuts,    │  └ Lumina Window Engine       │       │
│    mouse gestures) ──► lumina_server │     (DecorAddOn: Decorator +  │◄──────┘
│                                      │      WindowBehaviour +        │  work-area,
│                                      │      DesktopListener)         │  rules
└──────────────────────────────────────┴───────────────────────────────┴───────┘
```

### 2.1 `liblumina` (native shared library)

* Configuration schema, layered store, validation, migration.
* Profile (personality) load/save/import/export.
* IPC protocol constants and message builders (`'Lcfg'`, `'Lwin'`, `'Lmod'`, …).
* Wrappers around semi-private APIs: `WindowInfo.h` (window list, minimize/raise), `BLaunchRoster` (enable or stop Deskbar), decorator and UI-colour setters.
* A Haiku implementation of Lumina's `OSInterface` capability set (battery, volume, brightness, CPU, memory, disk, power), following the mapping in audit §2.
* Pure C++/Haiku API, with no Qt.

### 2.2 `lumina_server` (`application/x-vnd.Lumina-Server`)

The session brain: a background `BApplication` started by `launch_daemon`.

* **Mode manager**: Stock ↔ Lumina, personality switching, crash watchdog (§7).
* **Config broker**: owns the live merged config, watches files with `BNodeMonitor`, and broadcasts deltas to subscribers.
* **Window registry**: one authoritative window model (token, team, title, workspace, frame, minimized). It is built from Window Engine events where available, and otherwise by polling `get_token_list`/`get_window_info` plus watching `BRoster` (`B_SOME_APP_LAUNCHED`/`QUIT`/`ACTIVATED`). Panels subscribe instead of polling app_server themselves.
* **Rule engine**: evaluates window rules (§5.3) and sends *commands* to the Window Engine.
* **System appearance applier**: sets UI colours, fonts, decorator, decorator settings, control look, mouse mode, workspace count and layout. It snapshots the previous values first (§7.2).
* **Theme assets**: wallpaper per workspace (through Tracker's documented Backgrounds attributes on the Desktop directory), sound theme (system sound events through `BMediaFiles`), cursor (planned, see §6).

### 2.3 Lumina Window Engine (app_server `DecorAddOn`)

A decorator add-on installed in a *user* add-on directory (`~/config/non-packaged/add-ons/decorators/` during development; the home packages tree when installed). It provides three things:

| Part | Responsibility |
|---|---|
| `LuminaDecorator` (subclass of `DefaultDecorator`/`TabDecorator`) | Parametric drawing: tab shape, height, gradients, border widths, button set and placement, title font and alignment, colours. **Classic parameters reproduce `DefaultDecorator` exactly.** Per-window parameter overrides come from rules |
| `LuminaWindowBehaviour` (extends `DefaultWindowBehaviour`) | Drag/resize handling extended with snap zones (edges, corners, custom grid), modifier-drag move/resize, double-click actions. Magnetic borders stay as they are |
| `LuminaListener` (`DesktopListener`) | Streams window events to `lumina_server`; executes commands from it (move/resize/workspace/layer/decorator override); handles WM key bindings in `KeyPressed()`; tells the work area about reserved panel strips |

Key properties:

* **Coexists with Stack & Tile.** app_server always registers its built-in `StackAndTile` listener and then adds the decorator's listeners (`Desktop.cpp` L462–467). Lumina tiling must cooperate: it leaves S&T groups alone, or offers itself as the replacement and disables S&T through its settings.
* **Command channel.** Client → listener messages use `AS_TALK_TO_DESKTOP_LISTENER` with a Lumina identifier, the same path `BWindowStack` uses for Stack & Tile. `lumina_server` keeps one hidden `BWindow` as its link.
* **Listener → client events** go out as `BMessage`s to `lumina_server`'s port. They are posted asynchronously and never block on the event path.
* **Fail-safe.** The listener runs only while the Lumina decorator is selected. Switching the decorator back to Default (from the Appearance preflet, the CLI or the mode switch) unregisters it immediately. That is the native "off switch".
* **ABI pinning.** The package declares a dependency on the exact Haiku revision it was built for. A mismatch disables the add-on rather than loading it (§7.4).

### 2.4 Lumina Panel (`application/x-vnd.Lumina-Panel`)

* Native `BWindow`s: `B_NO_BORDER_WINDOW_LOOK`, `B_FLOATING_ALL_WINDOW_FEEL`, `B_ALL_WORKSPACES`, `B_AVOID_FOCUS`. One window per configured panel, on any edge of any screen. Deskbar-style vertical "expando" layouts are also supported.
* Drawn with `be_control_look` and `ui_color()`, so it follows the system theme automatically.
* **Applets are native add-ons**: shared objects exporting `instantiate_lumina_applet()` that return a `BView`. They use `BArchivable`, so the same applet can be dropped onto the Tracker desktop as a replicant.
* Stock applet set, mapped from Lumina's catalog:

| Lumina plugin | Lumina-for-Haiku applet | Native source of truth |
|---|---|---|
| `systemstart`, `appmenu`, `userbutton` | **Leaf menu** (Deskbar-compatible) + optional searchable launcher | Deskbar menu folder, `BQuery` on app signatures, recent apps (`BRoster::GetRecentApps`) |
| `taskmanager` | **Team/window list** (grouped by team, Deskbar semantics) | Window registry |
| `desktopbar`, `applauncher` | Pinned launchers | Symlinks/entries in `~/config/settings/Lumina/launchers/` (Tracker-editable) |
| `systemtray` | **Replicant tray** | Hosts Deskbar replicants (`BShelf`); in Classic/Modern, defers to Deskbar's tray |
| `desktopswitcher` | Workspace switcher | `activate_workspace`, live thumbnails optional |
| `clock`, `battery`, `systemdashboard` | Clock, power, volume, network, CPU/mem | `liblumina` OS interface |
| `spacer`, `line`, `quickcontainer`, `jsonmenu` | Same | — (`jsonmenu` runs user scripts, as in Lumina) |
| `showdesktop` | Show desktop | Minimise all, the same action as the `minimize_all` input filter |

* **Pseudo-transparency.** app_server has no compositing, so "transparent" panels draw the wallpaper region beneath them (read from the Backgrounds settings), tinted by the panel's alpha colour. Over windows they are opaque. The UI labels this honestly.
* **Work-area reservation.** The panel asks `lumina_server` to reserve its strip. The Window Engine treats that strip as excluded when maximising/zooming and placing windows.

### 2.5 Desktop widgets

There are two kinds, and both are first-class:

1. **Native widgets = Tracker desktop replicants.** Tracker's desktop pose view is a `BShelf`. Lumina applets are archivable `BView`s, so they can live on the desktop beside Tracker icons, exactly like the ActivityMonitor and Clock replicants today. Tracker saves their positions. They are zero-cost when Qt isn't present.
2. **QML widgets = `lumina-widgethost`** (Qt 6/QML process). These are frameless windows marked by a window rule as `layer: desktop` (kept below normal windows, above Tracker's desktop). They suit user-authored widgets with animations, charts and network data. Lumina's pure-Qt desktop plugins (calendar, notepad, rssreader, systemmonitor, audioplayer) are re-expressed here in QML.

Desktop *layouts* (which widgets, where, per workspace and per screen) are part of the profile.

### 2.6 LuminaInput (input_server filter)

* Global launch shortcuts, mouse gestures, hot corners and edge actions → `BMessage` to `lumina_server`.
* WM key bindings (tile left, move to workspace N, cycle layouts) are handled in the Window Engine's `KeyPressed()` instead, so they work even if `lumina_server` is busy.
* Coexists with Haiku's Shortcuts preflet (`shortcut_catcher`). Lumina Settings can import those bindings.

### 2.7 Lumina Settings Center (Qt 6 + QML, optional)

* Search-first settings UI with live preview, organised into Personality, Panels, Desktop, Windows, Rules, Input, Notifications, Sounds, Profiles and Advanced.
* Talks only to `lumina_server`, never to files directly. The server validates every change, so the UI can't corrupt the config.
* Uses KF6 where it helps: **KArchive** for profile bundles, **KWidgetsAddons** or **Kirigami6** for UI building blocks.
* A small native fallback, **Lumina Mode** (a preflet in the Interface Kit, ~1 kLOC), provides mode/personality switching and "Restore stock Haiku". Recovery never depends on Qt.

## 3. Process & lifecycle

```
launch_daemon (user session, target "desktop")
 ├─ app_server ............................. stock
 ├─ x-vnd.Be-TRAK  Tracker ................. stock, always
 ├─ x-vnd.Be-TSKB  Deskbar ................. stock; may be disabled per personality
 └─ lumina (package-provided launch file)
     ├─ job   x-vnd.Lumina-Server  if setting ~/config/settings/Lumina/session enabled
     │                             no_safemode, on initial_volumes_mounted
     ├─ service x-vnd.Lumina-Panel requires x-vnd.Lumina-Server   (auto-restart)
     └─ job   x-vnd.Lumina-WidgetHost  only if QML widgets present
```

* Everything uses native application lifecycle: `B_QUIT_REQUESTED`, `B_ARGV_RECEIVED`, `B_REFS_RECEIVED`, and settings saved on `QuitRequested()`.
* Shutdown/restart through Lumina calls the same registrar path as Deskbar, so apps get their normal quit requests.

## 4. IPC protocol (sketch)

All messages are `BMessage`s; constants live in `liblumina/LuminaProtocol.h`.

| `what` | Direction | Payload |
|---|---|---|
| `'Lsub'` subscribe | client → server | `topics` (strings: `config.panels`, `windows`, `mode`, …) |
| `'Lcfg'` config delta | server → subscribers | `path` + typed value(s), `generation` |
| `'Lset'` set config | client → server | `path`, value, `preview` (bool), `revertAfter` (ms) |
| `'Lwin'` window event | engine/server → subscribers | `op` (added/removed/moved/activated/workspace/minimized), `token`, `team`, `frame`, `title`, `workspaces` |
| `'Lcmd'` window command | server → engine | `op` (move/resize/tile/layer/decor/workspace), `token`, args |
| `'Lmod'` mode | any → server | `mode` (`stock`/`lumina`), `personality` |
| `'Lres'` reserve area | panel → server → engine | `screen`, `edge`, `thickness`, `owner` |

## 5. Configuration & theming system

### 5.1 Layers (low → high priority)

1. **Schema defaults**, compiled into `liblumina`.
2. **Personality preset**, read-only: `/boot/system/data/Lumina/personalities/<name>/`.
3. **Theme packs**, read-only, installed as packages (`lumina_theme_<name>`). They provide decorator parameter sets, colour schemes, icon packs, cursors, wallpapers and sound themes.
4. **User profile**: `~/config/settings/Lumina/profiles/<name>/`.
5. **Per-application overrides**, keyed by app signature, inside the profile.
6. **Session overrides** (preview/try-mode, in memory only).

Merged values are resolved per key. Every key has a schema entry with type, range, default and a "requires" constraint (for example, `windows.tiling.*` requires the Lumina decorator).

### 5.2 On-disk format

* One directory per profile: `profile.lumina` (manifest), `panels.lumina`, `desktop.lumina`, `windows.lumina`, `rules.lumina`, `input.lumina`, `appearance.lumina`, `notifications.lumina`, `apps/<signature>.lumina`, plus `assets/`.
* The text format is **JSON with comments**, parsed by `liblumina` (a comment-stripping pre-pass, then Haiku's private `BPrivate::BJson` from `headers/private/shared/Json.h`). It's human-diffable, easy to share, and maps 1:1 to `BMessage`.
* Files carry `schemaVersion`. `liblumina` migrates old versions forward and never rewrites a file it can't parse; such a file is quarantined as `*.broken` and the layer below is used.

### 5.3 Window rules

```jsonc
{ "match": { "signature": "application/x-vnd.Haiku-Terminal", "title~": "ssh .*" },
  "set":   { "workspace": 3, "decor.tabColor": "#3b6ea5", "placement": "right-half",
             "layer": "normal", "alwaysOnTop": false } }
```

Matchers: signature, title regex, window look/feel, workspace, screen and team. Actions: workspace, initial geometry or placement preset, tile slot, layer, decorator overrides, focus-stealing prevention and minimised-on-start. The engine applies them at `WindowAdded` and on demand.

### 5.4 Personalities = profiles

| Personality | Decorator | Control look | Deskbar | Lumina panels | Widgets | WM extras | Effects |
|---|---|---|---|---|---|---|---|
| **Haiku Classic** (default) | Lumina @ Default-exact params | Haiku | **On**, stock position | None by default (optional extra applets via Deskbar replicants) | Native replicants allowed | Snap-to-edge off, S&T on, rules on | Off |
| **Haiku Modern** | Lumina "modern" params (taller tab, softer gradients, updated title font) | Flat | On, or replaced by a Lumina panel matching Deskbar's look | Optional bottom dock | QML widgets enabled | Snap zones on | Panel fades and slides, pseudo-transparency |
| **Haiku Power User** | Lumina classic or modern | User choice | Off (Lumina panels replace it) | Multiple panels, multi-screen | Both | Keyboard tiling, rule sets, per-workspace layouts | User choice, with performance budget |
| **Haiku Minimal** | Lumina compact (thin tab, auto-hide buttons) | Flat | Auto-hide | None | None | Snap, maximise-by-default rule option | Off |

Each one is a profile directory, so users duplicate, edit, export and share them like any other profile.

### 5.5 Import / export / share

* Export creates a `.luminaprofile` file: a zip of the profile directory and its assets, with a manifest (name, author, schemaVersion, required theme packs, checksum).
* It gets its own MIME type, `application/x-vnd.Lumina-Profile`, with an HVIF icon. Double-clicking it in Tracker opens the Lumina Mode/Settings import dialog: preview, diff against the current profile, and apply-with-revert.
* Imported profiles are **data only.** `jsonmenu` scripts in an imported profile are disabled until the user explicitly approves them.
* Theme packs that need distribution (icon packs, decorators' bitmaps, cursors) ship as normal Haiku packages through HaikuDepot or a custom repository.

### 5.6 Safe apply

Any change that affects display or input (decorator, colours, panel positions, key bindings) is applied in **try mode** with a revert countdown, like the Screen preflet. The last known-good generation of every file is kept, so an "Undo last change" is always available.

## 6. Customisation feature matrix

| Requested capability | Mechanism | Feasibility |
|---|---|---|
| Multiple panels / positions | Lumina Panel windows + work-area reservation via Window Engine | ✅ |
| Custom launchers & app menus | Panel applets; Deskbar menu folder; JSON/script menus | ✅ |
| Panel transparency, size, spacing, appearance | Panel settings; **pseudo**-transparency | ✅ (transparency ⚠️ pseudo only) |
| Decorations, themes, borders, colours, typography | LuminaDecorator params + `set_ui_colors` + system fonts + control-look add-ons | ✅ |
| Desktop widgets & layouts | Tracker replicants + QML widget host | ✅ |
| Keyboard shortcuts & mouse behaviour | Window Engine `KeyPressed`; LuminaInput filter; `set_mouse_mode`, accept-first-click | ✅ |
| Multiple workspace configurations | `set_workspace_count`, layout; per-workspace wallpaper (native) and panel/widget sets | ✅ |
| Snapping, tiling, placement | LuminaWindowBehaviour + listener | ✅ (cooperate with S&T) |
| Notification appearance & behaviour | `notification_server` + Notifications preflet settings (position, width, timeout, per-app); *custom visuals* need a replacement server → **phase 6, optional** | ⚠️ partial natively |
| Per-app appearance/behaviour overrides | Window rules (decorator params, placement, workspace); per-app Qt style hints | ✅ (deep in-app colours only for apps that honour UI colours) |
| Icon packs | Overlay that sets per-type/app icons via `BMimeType::SetIcon` in the **user** MIME DB, fully reversible from a backup | ✅ with care (no system DB writes) |
| Cursors | app_server cursor theming isn't exposed as a setting today | ⚠️ needs upstream work; Lumina cursors apply to Lumina surfaces only |
| Wallpapers | Backgrounds attributes (per workspace) | ✅ |
| Sound themes | System sound events (`BMediaFiles`) | ✅ |
| Advanced WM rules | §5.3 | ✅ |
| Animations / effects with performance controls | Lumina surfaces only (panel slide, menu fade, widget transitions); global effects need app_server compositing | ⚠️ limited |

## 7. Switching between stock Haiku and Lumina

This is the most important user-facing guarantee, so it is designed in layers.

### 7.1 The two modes

* **Stock Haiku:** `lumina_server` isn't running. The Lumina Window Engine isn't the selected decorator. Deskbar is enabled, and system appearance equals the user's saved pre-Lumina snapshot. *Lumina leaves zero runtime footprint.*
* **Lumina:** `lumina_server` running with a personality; the Window Engine is selected.

"Haiku Classic" is a Lumina personality and differs from Stock in having the extra capabilities. Stock is the escape hatch.

### 7.2 Snapshot and restore

The first time Lumina is enabled, `lumina_server` saves `~/config/settings/Lumina/stock-snapshot`. It contains the current decorator path and settings, UI colours, control look, fonts, mouse mode and focus settings, workspace count and layout, Deskbar settings (location, expanded, auto-raise, always-on-top, auto-hide) and its enabled state, and the background attributes. "Restore stock Haiku" reapplies this snapshot exactly, so users get back *their* Haiku, not a factory default.

### 7.3 Ways to switch

1. **Lumina Mode replicant** in Deskbar's tray (and in Lumina panels): Stock / Classic / Modern / Power User / Minimal / user profiles.
2. **Lumina Mode preflet** (native, no Qt).
3. **CLI**: `lumina mode stock|lumina`, `lumina personality <name>`, `lumina restore-stock`.
4. **Emergency key chord** in the LuminaInput filter (proposed: Ctrl + Alt + Shift + Backspace) → immediate restore-stock.
5. **Boot menu**: Lumina's launch jobs are marked `no_safemode`, so booting in Safe Mode never starts Lumina. The engine and input filter are meant to be installed as *user* add-ons so that the boot option *"Disable user add-ons"* also skips them. (PoC must verify which install locations — home packages vs. non-packaged — that option covers, and the package defaults to the covered one.)
6. **Uninstall**: removing the package with HaikuDepot or `pkgman` leaves a stock system. A package pre-uninstall script restores the snapshot.

### 7.4 Automatic fallback

* **Crash watchdog:** if `lumina_server` or the panel crashes three times within 60 s, the server (on its next start) or the Lumina Mode replicant switches to Stock, re-enables Deskbar, and posts a `BNotification` explaining what happened.
* **ABI guard:** the Window Engine checks the running Haiku revision against the one it was built for. On mismatch, it stays a pure pass-through `DefaultDecorator` and reports "engine disabled: rebuild required". The desktop never fails to start.
* **Deskbar guarantee:** whenever Lumina has disabled Deskbar, the server re-enables it in its quit path *and* the watchdog does so on crash. Nobody ends up with no way to launch apps.

## 8. Packaging

| Package | Contents | Requires |
|---|---|---|
| `lumina_base` | `liblumina`, `lumina_server`, CLI, Lumina Mode preflet + replicant, personalities, launch file | `haiku` |
| `lumina_engine` | Window Engine decorator add-on, LuminaInput filter | `haiku == <hrev>` (ABI pin) |
| `lumina_panel` | Panel app + stock applets | `lumina_base` |
| `lumina_settings` | Qt 6/QML Settings Center | `lumina_base`, `qt6_base`, `qt6_declarative`, (`kirigami6`, `karchive6`, `kwidgetsaddons6`) |
| `lumina_widgets` | QML widget host + stock QML widgets | `lumina_base`, `qt6_declarative` |
| `lumina_theme_*` | Theme packs | `lumina_base` |

The recipe goes to HaikuPorts (`haiku-apps/lumina_haiku`) once the PoC is stable.

## 9. What we deliberately do *not* do

* No X11, Wayland, D-Bus, systemd, PAM, polkit or XDG runtime.
* No replacement of Tracker or of the Desktop.
* No patches to app_server. Everything that needs a stable upstream API (listener ABI, cursor themes, compositing, QPA native handle) is tracked as an **upstream proposal**, not a fork.
* No porting of Lumina's stand-alone apps (lumina-fm, textedit, …) in the core. They duplicate native apps.
