# 08 — Multitasking & Productivity Roadmap

> **Scope.** Window management, application switching, workspaces, an
> overview mode and productivity surfaces for Lumina for Haiku. This
> document builds on [03-architecture.md](03-architecture.md),
> [engine.md](engine.md) and [vfx.md](vfx.md). The current state of each
> component is in [status.md](status.md); where this document and the code
> disagree, the code wins.
>
> **Status legend.** **exists** = written in this repository (compile-verified
> or tested, per status.md; nothing has run on Haiku yet) ·
> **partial** = some of it exists · **planned** = designed here, not written.
> Proposed configuration keys are marked *(proposed)* and are not in the
> schema yet.

## 1. Principles

Each feature has to fit Haiku and BeOS conventions:

1. **Responsiveness over spectacle.** Nothing may add latency to input, window
   activation or workspace switching. Every effect must have an instant path,
   and nothing runs while the desktop is idle.
2. **Direct manipulation.** Windows move when you drag them, and overview
   and workspace previews accept drag and drop. Avoid modes the user has to
   learn first.
3. **Keyboard conventions are kept.** Ctrl+Tab (Twitcher), Cmd+F1…F12 and
   Cmd+` (workspaces), Cmd+Ctrl+arrows (workspace grid), Option-drag (Stack &
   Tile) and the Shortcuts preflet keep their stock meaning. Lumina's new
   bindings must not collide with them (§8.1).
4. **Native look.** Every surface is a `BWindow` drawn with `be_control_look`
   and `ui_color()`. *Haiku Classic* stays visually identical to stock Haiku.
5. **Everything is optional and independently configurable.** Each feature
   has its own enable switch in the schema, so no feature depends on a
   personality. Personalities only choose defaults.
6. **Reuse, don't reinvent.** Stack & Tile, `WindowInfo.h`, the Workspaces
   view, `notification_server`, `BShelf` and the stock input filters are used
   where they already solve the problem.
7. **The traditional Twitcher always remains selectable** and is the default
   in Classic and Minimal.

### 1.1 Integration levels

| Level | Meaning | Examples | Failure mode |
|---|---|---|---|
| **L1 — native** | Public or long-stable semi-private client API, run from `lumina_server` or the panel | `BWindow`, `BScreen`, `BClipboard`, `BNotification`, `WindowInfo.h`, `activate_workspace()` | Feature does nothing; desktop unaffected |
| **L2 — enhanced** | Code inside app_server (Window Engine `DesktopListener`) or input_server (filter add-on) | Snapping, keyboard capture before dispatch, `Desktop::SetWindowFeel`, frame-buffer capture | ABI guard disables it; package pins the hrev ([engine.md](engine.md)) |
| **L3 — app_server patch** | Needs a change upstream | Per-window off-screen buffers, multi-screen `VirtualScreen` | Tracked as an upstream proposal; Lumina never ships a patched app_server ([03](03-architecture.md) §9) |

Every L2 feature must degrade to an L1 behaviour, or to stock behaviour, when
the engine is absent, inactive or ABI-blocked.

## 2. Source facts this roadmap depends on

All paths are relative to the Haiku source tree. Each fact was checked in the
sparse checkout used for development, except where it says otherwise.

| # | Fact | Where |
|---|---|---|
| F1 | app_server registers its built-in `StackAndTile` listener first, then every decorator add-on listener | `src/servers/app/Desktop.cpp` (`RegisterListener(&fStackAndTile)`, then `gDecorManager.GetDesktopListeners()`) |
| F2 | Keyboard events pass through `KeyboardFilter::Filter` → `Desktop::KeyEvent` → `NotifyKeyPressed`. **Every** listener sees the key, and if any returns `true` the message is skipped (`B_SKIP_MESSAGE`). Modifier changes and key-ups take the same path | `Desktop.cpp`, `DesktopListener.cpp` (`DesktopObservable::NotifyKeyPressed`) |
| F3 | `KeyboardFilter` hard-codes Cmd+F1…F12 (workspace), Cmd+` (previous workspace) and Shift+Cmd+Ctrl+Esc (safe video mode) *before* listeners run | `Desktop.cpp`, `KeyboardFilter::Filter` |
| F4 | Deskbar's `BarApp` forwards `'TASK'` messages to the switcher (`fSwitcherMessenger`). `TSwitchManager` reads `when`, `modifiers` and `key`; `key == 0x11` means cycle by window | `src/apps/deskbar/BarApp.cpp`, `Switcher.cpp` (`TSwitchManager::MessageReceived`) |
| F5 | The sender of `'TASK'` (Ctrl+Tab handling in `BWindow`'s key path, `src/kits/interface/Window.cpp`) is **not in this checkout; unverified here**. Because of F2 it runs client-side, *after* app_server's listeners | — |
| F6 | Twitcher only shows its UI after a 180 ms hold (`MainEntry`, "good feel found by trial and error"), re-sorts apps after 400 ms of inactivity, polls modifier release (`TIconView::Pulse`), and offers arrows/Tab/Esc/Space/Enter/`q` (quit app)/`h` (hide app) | `Switcher.cpp` (`MainEntry`, `DoKey`) |
| F7 | Stack & Tile: Option alone activates SAT mode; Option+Tab/arrows cycle stacked tabs (`StackAndTile::KeyPressed`). Groups can be archived and restored (`kSaveAllGroups`, `kRestoreGroup`; `SATGroup::ArchiveGroup/RestoreGroup`). Each window has a persistent `window_id` exchanged through decorator settings (`SATWindow::GetSettings/SetSettings`) | `src/servers/app/stackandtile/StackAndTile.cpp`, `SATGroup.h`, `SATWindow.cpp`, `headers/private/interface/StackAndTilePrivate.h` |
| F8 | `Desktop::GetStackAndTile()` is public within app_server; `StackAndTile::GetSATWindow()`, `FindSATWindow(uint64)`, `SATWindow::GetGroup()`, `StackWindow(SATWindow*)` and `PositionManagedBySAT()` are public members | `Desktop.h`, `StackAndTile.h`, `SATWindow.h` |
| F9 | The client-side stacking API is `BWindowStack` (private), which talks over the window's private link | `headers/private/interface/WindowStack.h`, `src/kits/interface/WindowStack.cpp` |
| F10 | Windows carry a workspace **bitmask** (`B_ALL_WORKSPACES = 0xffffffff`, `BWindow::SetWorkspaces(uint32)`), with up to 32 workspaces (`kMaxWorkspaces`). app_server keeps a **per-workspace position** for each window (`Window::Anchor(index)`, `window_anchor` in `WindowList.h`) unless `B_SAME_POSITION_IN_ALL_WORKSPACES` is set | `headers/os/interface/Window.h`, `src/servers/app/DesktopSettings.h`, `Window.h`, `WindowList.h` |
| F11 | Feels: `B_NORMAL_WINDOW_FEEL`, `B_FLOATING_APP_WINDOW_FEEL`, `B_FLOATING_ALL_WINDOW_FEEL`, modal feels; flags `B_AVOID_FRONT`, `B_AVOID_FOCUS`. Private feels include `kDesktopWindowFeel`. app_server exposes `Desktop::SetWindowFeel/SetWindowFlags/SetWindowWorkspaces` | `Window.h` (public), `headers/private/interface/WindowPrivate.h`, `src/servers/app/Desktop.h` |
| F12 | `DesktopListener` has **no** hook for workspace activation or title changes. Clients get `BWindow::WorkspaceActivated()` | `src/servers/app/DesktopListener.h` |
| F13 | Multi-monitor: `VirtualScreen::AddScreen` keeps one `DrawingEngine`/`HWInterface` ("TODO: this works only for single screen configurations"), `UpdateFrame` just sums widths ("TODO: compute virtual size depending on the actual screen position!"), `RemoveScreen` returns `B_ERROR`, and `ScreenManager::AcquireScreens` ignores the wish list. **app_server is effectively single-screen today** | `src/servers/app/VirtualScreen.cpp/.h`, `ScreenManager.cpp` |
| F14 | No per-window buffers: drawing goes to the screen's `DrawingBuffer()` (back buffer if double-buffered, else front). `BScreen::ReadBitmap` → `AS_READ_BITMAP` → `DrawingEngine::ReadBitmap` under exclusive access. **Pixels of obscured, minimised or other-workspace windows do not exist** | `drawing/HWInterface.cpp`, `ServerApp.cpp`, `drawing/DrawingEngine.h` |
| F15 | app_server's own `WorkspacesView` draws schematic windows (tab + frame + title, no pixels) and lets you drag windows between workspace cells | `src/servers/app/WorkspacesView.cpp` |
| F16 | Stock input filters: `switch_workspace` (Cmd+Ctrl+arrows, +Shift takes the window along), `minimize_all` (Cmd+Ctrl, +Shift restore), `shortcut_catcher` (Shortcuts preflet) | `src/add-ons/input_server/filters/*` |
| F17 | `BNotification` (public) has groups, message IDs (replace in place) and progress. The server signature is `application/x-vnd.Haiku-notification_server` and notifications arrive as `kNotificationMessage = 'nssm'`. Settings cover timeout, width, icon size and position. There is no history API | `headers/os/app/Notification.h`, `headers/private/notification/Notifications.h` |
| F18 | `BClipboard::StartWatching(BMessenger)` delivers `B_CLIPBOARD_CHANGED` (`'CLCH'`) | `headers/os/app/Clipboard.h` |
| F19 | `BScreen::GetBrightness/SetBrightness(float)`. PowerStatus reads ACPI batteries under `/dev/power` (`ACPIDriverInterface`) | `headers/os/interface/Screen.h`, `src/apps/powerstatus/` |
| F20 | `BShelf` (public) hosts replicants. Deskbar's tray is `TReplicantShelf : BShelf`; `BDeskbar::AddItem` adds tray replicants. (That Tracker's desktop is a `BShelf` is stated in [03](03-architecture.md) §2.5; Tracker's sources are not in this checkout) | `headers/os/interface/Shelf.h`, `src/apps/deskbar/StatusViewShelf.h`, `headers/os/interface/Deskbar.h` |
| F21 | `count_workspaces`, `current_workspace`, `activate_workspace` and `set_workspace_count` are public. Haiku has **no workspace names** | `headers/os/interface/InterfaceDefs.h` |

**Consequences.** (a) Because of F2 and F5, the Window Engine can take
Ctrl+Tab before Deskbar ever sees it. (b) If Deskbar is not running (Power
User sets `deskbar.mode: hidden`), there is **no Ctrl+Tab switcher at all**
today, because `'TASK'` is handled only by Deskbar (F4). (c) Thumbnails can
only show what was last visible (F14).

## 3. What exists today (baseline)

| Area | Exists | Where |
|---|---|---|
| Edge/corner snap on drag release → halves, quarters, maximize; threshold; respects panel reservations on all four edges | **exists** | `src/engine/LuminaEngine.cpp` (`_UsableArea`), `lumina::SnapZoneAt` in `src/core/lumina/Rules.h` |
| Placement presets `maximize`/`fill`, `left/right/top/bottom-half`, four quarters, `center` | **exists**, tested | `lumina::PlacementRect` |
| Window rules: match `signature` (exact or `prefix*`), `title`, `title~`, `look`, `feel`, `workspace`, `minWidth/minHeight`; `final` | **exists**, tested (core) | `RuleSet` |
| Rule actions actually applied: `placement`, `workspace`, `minimized`, at a window's first activation | **partial** | `ServerApp::_ApplyRules` |
| Engine commands: `move-resize`, `animate`, `cancel`, `minimize`, `activate`, `workspace` (single index), `configure`, `reserve`, `list` | **exists** | `src/liblumina/lumina/haiku/Protocol.h` |
| Window registry fed by engine events, polling fallback | **exists** | `src/server/WindowRegistry.*` |
| Cached window images at minimize; per-workspace screen images at switch | **exists** | `EffectsController` (`fWindowImages`, `fWorkspaceImages`) |
| Screen capture with cost measurement | **exists** | `CaptureScreen()` in `liblumina/lumina/haiku/VisualEffects.h` |
| Panel applets `launcher`, `windows` (grouped/`nogroups`), `workspaces`, `clock`, `showdesktop`, `spacer`; add-on API | **exists** | `src/panel/BuiltinApplets.cpp`, `Applet.h` |
| Battery and brightness in `OSInterface`; volume missing | **partial** | `liblumina/lumina/haiku/OSInterface.h`; status.md #6 |
| Input filter (emergency chord only) | **exists** | `src/input/LuminaInputFilter.cpp` |
| `KeyPressed()` consumes keys | **planned** (always returns `false`) | `LuminaEngine::KeyPressed` |

**Gaps found while writing this document.** Neither is fixed here; both are
tracked for MT1.

1. `ServerApp::_ApplyRules` does not fill `WindowDescriptor::look/feel`, so a
   rule that matches on `look` or `feel` can never match at runtime.
2. Rule-driven placement in `_ApplyRules` subtracts only `top`/`bottom`
   reservations. The engine's `_UsableArea` handles all four edges.
   Placement should move into the engine (exact decoration insets, status.md
   #3) or share `_UsableArea`'s logic.

## 4. A — Advanced window management

### 4.1 Feature table

| Feature | Status | Mechanism / level | Depends on | Defaults (Cl / Mo / PU / Mi) |
|---|---|---|---|---|
| Drag-to-edge snapping | **exists** | L2 engine `MouseUp` | — | off / on / on / on (halves) |
| Configurable snap zones (custom grid, per-screen zone sets, edge-band widths, preview outline) | planned | L2 engine; zone geometry in core | 4.2 | off / halves+quarters / custom / halves |
| Half/quarter layouts | **exists** (presets) | L2 | — | as above |
| Custom layouts (thirds, 2:1, named grids) | planned | core `PlacementRect` extension; L2 apply | 4.2 | off / off / on / off |
| Keyboard tiling | planned | L2 `KeyPressed` (no dispatch latency, works if server is busy) | MT1 key router | off / off / on / off |
| Snap preview outline | planned | L1 client-drawn outline window (`B_NO_BORDER_WINDOW_LOOK`, `B_FLOATING_ALL_WINDOW_FEEL`, `B_AVOID_FOCUS`) shown on an engine "zone-hover" event | engine event | off / on / on / off |
| Rule-based placement | **partial** | rules → engine `move-resize` | gaps §3 | rules on in all; no shipped rules |
| Grouping and optional tabbing | planned | **reuse Stack & Tile** (4.3) | F7–F9 | S&T as stock in all |
| Floating / pinned / always-on-top | planned | 4.4 | — | menu actions only |
| Remembered geometry | planned | L1 store + L2 apply at first show | 4.5 | off / on / on / off |
| Per-app rules (decorator, layer, focus-stealing, snap-exempt) | planned | rule actions → engine | 4.4 | — |
| Multi-monitor | planned, **blocked upstream** | L3 (F13) | upstream | n/a |
| Smart workspace placement | planned | rules + heuristics in server | §6 | off / off / on / off |
| Restoration after Lumina restart | **partial** | engine `list` + registry | 4.6 | always on |

### 4.2 Snap zones and layouts

*Mechanism.* Zone detection stays a pure function in `src/core/lumina`
(unit-testable on the host), extended from `SnapZoneAt(x, y, screen,
threshold, layout)` to accept a zone set:

```jsonc
// windows.snap.zones (proposed)
{ "layout": "custom",
  "zones": [ { "edge": "left",  "band": [0, 0.33], "preset": "left-third" },
             { "edge": "left",  "band": [0.33, 1], "preset": "left-half" },
             { "corner": "top-left", "preset": "top-left-quarter" } ],
  "grids": { "dev": [[0, 0, 0.62, 1], [0.62, 0, 1, 0.5], [0.62, 0.5, 1, 1]] } }
```

*Engine side.* `MouseMoved` already tracks the drag (`fDragToken`). During a
drag it emits a coalesced `zone-hover` event, at most once per zone change,
never per pixel, so the client can draw the preview. The engine itself never
draws outside the decorator.

*Risks.* Zones fight with app_server's magnetic borders and with S&T's
Option-drag; snapping stays disabled while `StackAndTile::SATKeyPressed()` is
true (F8). Apps with size limits: clamp to `SizeLimitsChanged` values, which
the engine already receives.

*Tests.* Host: zone geometry tables, including degenerate screens and
reserved strips on all edges. Haiku: drag matrix (native, Qt, Xlibe, WINE)
from [05](05-roadmap.md); verify that S&T Option-drag never triggers a zone.

### 4.3 Grouping and tabbing = Stack & Tile

Lumina does **not** implement its own tabs. Stack & Tile already provides
stacked tabs (drawn by `SATDecorator`, which *is* the engine's decorator),
tiled groups, keyboard tab cycling (F7) and archive/restore (F7).

| Capability | How Lumina uses S&T | Level |
|---|---|---|
| "Stack with…" (window menu, switcher, overview drag) | Engine resolves both tokens, then `GetStackAndTile()->GetSATWindow(a)->StackWindow(b)` (F8) | L2, links app_server internals (ABI-pinned like the rest of the engine) |
| Keep engine moves consistent with groups | Today stacked windows are skipped (status.md #8). Next: detect tiled groups through `SATWindow::GetGroup()`/`PositionManagedBySAT()` and treat the whole group as the snapping/animation unit, or refuse | L2 |
| Persist groups across sessions | Call S&T's `kSaveAllGroups`-equivalent (`SATGroup::ArchiveGroup`) on Lumina exit; `RestoreGroup` on start. Windows are re-identified by `window_id` (F7), which survives only for apps that save their decorator settings | L2 |
| Switcher/overview show a stack as one entry with tabs | Registry gains a `group` field from the engine | L2 → L1 data |

*Risk.* S&T's internals are not an API; any hrev can change them. The ABI
guard (engine.md) already disables window-changing features on mismatch. Group
features must also be individually switchable (`windows.groups.useStackAndTile`
*(proposed)*). *Test:* stack two Terminals with Option-drag, then snap the
stack, animate it and switch workspaces with it; save, restart Lumina and
restore.

### 4.4 Floating, pinned, always-on-top

| User concept | Haiku mapping | Level | Notes |
|---|---|---|---|
| **Always on top** | `B_FLOATING_ALL_WINDOW_FEEL` via `Desktop::SetWindowFeel` (F11) | L2 | The client's `BWindow::Feel()` becomes stale. Lumina records the original feel and **restores it on unpin, on exit to stock and from the crash path**. Modal and floating-subset windows are refused |
| **Pinned / sticky** (all workspaces) | Workspace mask `B_ALL_WORKSPACES` via `Desktop::SetWindowWorkspaces` (F10). Optional `B_SAME_POSITION_IN_ALL_WORKSPACES` | L2 (L1 for Lumina's own windows) | Original mask stored and restored like the feel |
| **Floating** (exempt from tiling/snapping/rules) | Lumina policy flag only | — | No app_server change |
| **Keep below** (desktop widgets) | `B_AVOID_FRONT` flag (public); `kDesktopWindowFeel` is private and reserved for Tracker | L2 for foreign windows | [03](03-architecture.md) §2.5 `layer: desktop` |

These become rule actions (`layer`, `pinned`, `floating` *(proposed)*) and
window-menu items. The decorator's own menu is unchanged in Classic.

### 4.5 Remembered geometry and per-app rules

*Store.* `~/config/settings/Lumina/geometry.lumina` *(proposed)*: key =
signature + normalised title class (rule-style regex, user-editable), value =
outer frame in *screen-relative* units plus workspace mask and S&T
`window_id` when known.

*Apply.* The engine already parks or observes windows before their first show
(`WindowAdded`). Because many Haiku apps save and restore their own frames,
remembered geometry **never overrides an app that positioned itself**. It
applies only when the window appears at the app_server default position, or
when a rule says `"remember": "force"`.

*Per-app rules* extend the existing action set with `layer`, `pinned`,
`floating`, `snap: false`, `focusOnMap: false` (focus-stealing prevention)
and `decor.*` (once parametric decorators exist). All are evaluated by the
existing `RuleSet`.

*Tests.* Host: key normalisation, rule merge order. Haiku: StyledEdit (saves
its own frame) must not be fought; Terminal tabs keep their frame.

### 4.6 Restoration after Lumina restart

The engine lives in app_server and survives a `lumina_server` restart. On
start, the server asks the engine to `list` (re-send `added` for every window)
and rebuilds the registry. Lumina-applied state (pinned, on-top, S&T groups,
tiling slots) is journaled to `~/config/settings/Lumina/session-state`
*(proposed)* whenever it changes. After a restart it is reconciled by token
first, then by S&T `window_id`. On **exit to stock** the journal is used in
reverse to undo every feel, flag and mask Lumina changed. *Test:* `kill -9
lumina_server` with pinned/on-top windows, then restart; separately, run
`lumina mode stock` and check that every original feel and mask is back.

### 4.7 Multi-monitor

The data model is already per screen (`panels.*.screen`, engine
`Reserved.screen`). app_server today drives one `DrawingEngine` and treats the
virtual frame as a simple sum of widths (F13), so Lumina cannot place windows
"on monitor 2" in any meaningful way. Plan: keep every API screen-indexed,
test with one screen, and document multi-monitor as an upstream dependency.
Nothing here requires L3 work from Lumina itself.

## 5. B — Application switcher

### 5.1 Modes

| `switcher.mode` *(proposed)* | Behaviour | Default in |
|---|---|---|
| `stock` | Lumina never consumes Ctrl+Tab; Deskbar's Twitcher runs | Classic, Minimal |
| `lumina` | Lumina switcher. If Lumina's switcher process is unavailable, falls back to stock automatically | Modern, Power User |

In Power User, Deskbar is hidden, so `lumina` is required there: without it
there is no switcher (§2 consequence b).

### 5.2 Taking the shortcut

| Path | Pros | Cons |
|---|---|---|
| **Engine `KeyPressed`** (preferred) | Sees Ctrl+Tab before dispatch (F2); also sees `B_MODIFIERS_CHANGED`, so Ctrl release is detected exactly instead of polled (F6) | Requires the engine (L2) |
| **input_server filter** (fallback) | Works with any decorator; the existing `LuminaInputFilter` could host it | Runs in input_server; must forward to lumina_server; can't see window state |
| Neither | Stock Twitcher | — |

*Protocol.* The engine consumes Ctrl+Tab and Ctrl+` only when (a)
`switcher.mode` is `lumina` and (b) a switcher client has **heart-beaten
within the last 2 s** (a `subscribe` with a `switcher` role). Otherwise it
returns `false` and the key reaches the focused `BWindow`, which then
delivers `'TASK'` to Deskbar (F4, F5). So a crashed switcher falls back to
Twitcher on the next key press. The engine posts `switcher-key {key,
modifiers, when, repeat}` with the existing zero-timeout send and does no
other work in the hook (budget: < 20 µs).

*Semantics kept from Twitcher (F6).* Tap-and-release switches immediately
without showing UI, using the MRU list. The UI appears after a hold threshold
(`switcher.holdMs` *(proposed)*, default 180 ms, Twitcher's value). Keys
are Tab/Shift+Tab, arrows, Esc, Space/Enter, `q` (quit app) and `h` (hide app).
Ctrl+` cycles windows of the current app.

### 5.3 Model

* **MRU** is kept incrementally in `lumina_server` from engine `activated`
  events (registry). Without the engine it falls back to front-to-back order
  from `get_window_order` (`WindowInfo.h`), which is what Twitcher uses.
* **Grouping by app** uses team/signature, like Twitcher's `TTeamGroup`. An
  S&T stack counts as one entry (4.3).
* **Workspace awareness**: current workspace first, other workspaces after a
  divider (configurable: `current`, `all`, `current-first`). Activating a window on
  another workspace calls `activate` (app_server switches workspace).
* **Search**: typing while the switcher is open filters by app name, window
  title and signature (fuzzy prefix). Titles are refreshed for all windows
  when the switcher opens, because the engine has no title hook (F12) and
  the registry only polls every 10 s.

### 5.4 Thumbnails, honestly

app_server has no per-window buffers (F14). A thumbnail can only be one of:

| Source | Content | Cost | Level |
|---|---|---|---|
| **Live, visible part** | Pixels of the window's currently visible region read from `DrawingBuffer()` | Memory read from the back buffer if double-buffered (cheap), otherwise VRAM readback (measured in vfx.md) | L2 engine (inside a locked hook) or L1 `BScreen::ReadBitmap` |
| **Cached last-seen** | Snapshot taken when the window was last fully visible: on deactivation, `WindowSentBehind`, minimize (already done by `EffectsController`), and before workspace switch | One capture per event, downscaled to thumbnail size at copy time | L2 / L1 |
| **Schematic** | Frame + tab + title + app icon, like `WorkspacesView` (F15) | Negligible | L1 |
| Truly live for obscured windows | **Not possible** without compositing or per-window buffers | — | L3 |

*Policy.* `switcher.thumbnails` *(proposed)*: `off` (icons, Twitcher look),
`cached` (default when enabled), `cached+visible`. Each thumbnail is labelled
with its age once it is older than a configurable limit. Snapshots are
captured only for windows whose visible region covers ≥ 80 % of their frame
(otherwise they keep the older image). The cache is LRU-bounded
(`switcher.cacheMB`, default 24) and dropped on `removed`.

*Transfer.* The engine cannot allocate client memory or run threads. The
client creates an `area`, passes its id once, and the engine clones it on
first use and writes downscaled pixels from inside a hook: no message-sized
copies, no blocking. The L1 fallback is `CaptureScreen()` from
`lumina_server`.

*Risks.* Capturing under the desktop write lock adds hook latency; capture
is budgeted at ≤ 1 ms per event and skipped if over. Privacy: an exclusion
list of signatures that are never snapshotted (password managers, by rule).

### 5.5 UI

A dedicated `lumina_switcher` window is created hidden at login. It is never
created on demand, which keeps open latency low. It is presented like
Twitcher (which uses `B_MODAL_WINDOW_LOOK`, see `TSwitcherWindow::DoKey`),
and it does not activate the target window until you commit. It is drawn with `be_control_look`. The Classic-style
layout is a single row of 48 px icons (Twitcher's `kTeamIconSize`); Modern
and Power User add a thumbnail row and a search field.

*Tests.* Host: MRU ordering, grouping, search ranking. Haiku: hold/tap
timing, kill switcher mid-hold → next Ctrl+Tab gives Twitcher, Deskbar hidden
→ switcher still works, key repeat storms (Twitcher's `fSkipUntil` issue).

## 6. C — Workspaces

| Feature | Status | Mechanism / level | Notes | Defaults (Cl / Mo / PU / Mi) |
|---|---|---|---|---|
| Count & grid layout | **exists** (stock + `PrivateApi::Set/GetWorkspacesLayout`) | L1 | — | stock in all |
| Names | planned | L1, profile data (`workspaces.names` *(proposed)*); Haiku has none (F21) | Shown in panel applet, switcher, overview, OSD | off / on / on / off |
| Switch OSD (name + index, ≤ 600 ms) | planned | L1 panel-owned window, `B_AVOID_FOCUS` | Hidden instantly on next input | off / on / on / off |
| Previews | **partial** (applet draws schematic; switch-time images cached) | L1 schematic (F15 model) + cached screen images (`fWorkspaceImages`) | Previews are the workspace as last seen | — |
| Per-workspace arrangements | planned | Saved layouts = list of (rule match → preset/slot) per workspace; applied on demand | Uses §4 presets | off / off / on / off |
| Drag-and-drop between workspaces | planned | Applet/overview → engine `workspace` command | app_server's own `WorkspacesView` already supports this; Lumina mirrors it in its applet | on wherever the applet is shown |
| Multi-workspace windows | planned | Workspace **bitmask** (F10); extend engine `workspace` op to accept a mask | Per-workspace positions are native (`window_anchor`) | menu only |
| Animations | **exists** (proxy slide/zoom, `fx.proxies.workspace`) | L1 proxies ([vfx.md](vfx.md)) | Must never delay the switch itself | none / slide / none / none |
| Keyboard navigation | **exists** in stock (F3, F16) | L1 | Lumina adds no default bindings; optional "move window to workspace N" via engine | stock |
| Rule-based assignment | **exists** (`workspace` action) | rules | Extend to masks and names (`"workspace": "Research"`) | — |
| Activity groups (optional presets) | planned | Profile presets that set names + rules + per-workspace wallpaper (native Backgrounds attributes) | §6.1 | off in all; offered in setup |

*Workspace change detection.* There is no listener hook (F12). The panel's
`BWindow::WorkspaceActivated()` (all-workspace window) notifies
`lumina_server`. The engine additionally compares
`Desktop::CurrentWorkspace()` in its next hook. The second is a safety net,
not the primary source.

### 6.1 Activity groups

An activity group is a *named workspace with rules*. It adds no new concept to
app_server. Presets ship as data, are disabled by default and can be edited
like any profile fragment:

| Preset | Example rules (illustrative) |
|---|---|
| Development | Terminal, Pe/Koder, Debugger → this workspace; Terminal `right-half` |
| Communications | Mail, Vision/Quassel → here; notifications not muted |
| Research | WebPositive, Tracker query windows → here |
| General | everything unmatched |

Applying a preset changes workspace count only if the user confirms. It goes
through try-mode with revert ([03](03-architecture.md) §5.6).

## 7. D — Overview mode

*What it is.* A full-screen, Lumina-owned `BWindow` that shows the current
workspace's windows as a non-overlapping grid of cached snapshots, with a
workspace strip and a search/launch field. It is native, light and keyboard
first. It is **not** a compositor effect: real windows do not move.

| Aspect | Design | Level |
|---|---|---|
| Previews | Same snapshot cache as §5.4; schematic fallback; age badges | L1/L2 |
| Opening | Hot key (none by default), optional hot corner via input filter, panel applet | L1 / L2 filter |
| Organisation by DnD | Drag a preview onto a workspace cell → `workspace`; onto another preview → "stack with" (S&T, §4.3); onto a screen half → placement preset | L2 commands |
| Keyboard | Arrows move selection, Enter activates, Ctrl+1…9 workspaces, Delete = close (`B_QUIT_REQUESTED` to the window), typing = search | L1 |
| Search & launch | One field: windows first, then applications (`BQuery` on `BEOS:APP_SIG`, recent apps via `BRoster::GetRecentApps`) — shared with the launcher (§8) | L1 |
| Transition | Optional proxy zoom from a capture of the screen (vfx.md technique C); off ⇒ instant | L1 |

*Performance.* It opens with whatever snapshots exist and takes no new
captures while opening, except for the screen capture used by the optional
transition, which is masked by it.

*Defaults:* off / available (no hot key) / on (hot key) / off.

## 8. E — Productivity surfaces

| Feature | Status | Built-in vs applet/plugin | Mechanism / level | Notes |
|---|---|---|---|---|
| **Notifications** (appearance, position) | planned | system (stock) | `notification_server` + Notifications preflet settings (F17) | Lumina writes the preflet's settings; no replacement |
| **Notification history** | planned (optional) | applet + optional component | `BNotification` has no history API (F17). Options: (1) history of Lumina's own notifications only; (2) an **opt-in** compatible receiver that registers the notification-server signature, records, and re-displays. (2) replaces a system server and is risky: off by default, separate package, automatic fallback to stock | L1 / replacement |
| Do-not-disturb | planned | applet | Lumina's own notifications only, unless (2) above | — |
| **Quick settings** (volume, brightness, network, DND, mode) | planned | applet (`quicksettings`) | Brightness `BScreen::Get/SetBrightness` (F19); volume via Media Kit mixer (to add to `OSInterface`, status.md #6); network via `BNetworkRoster` (unverified here) | — |
| **Clipboard history** | planned | applet + server module | `BClipboard::StartWatching` → `B_CLIPBOARD_CHANGED` (F18); stores text and small images; in-memory by default; excluded signatures; capped (`clipboard.history.max`, default 25) | Off by default everywhere (privacy) |
| Global shortcuts | **partial** (emergency chord) | built-in | Launch shortcuts: input filter; WM bindings: engine `KeyPressed`. Import from Shortcuts preflet (`shortcut_catcher`, F16) | Collision check §8.1 |
| App shortcuts | planned | rules | Rule action `keys` binds a chord when a matching window is focused (engine knows focus) | — |
| Window search | planned | built-in (switcher/overview/launcher share it) | Registry + refreshed titles | — |
| **Launcher** (searchable) | **partial** (Deskbar-menu launcher exists) | applet | Search over `BQuery` app index + recent apps + Deskbar menu folder | Classic keeps Deskbar menu |
| Desktop widgets | planned | applets as replicants | Applets are archivable `BView`s → Tracker desktop `BShelf` (F20) | [03](03-architecture.md) §2.5 |
| Monitoring applets (CPU, memory, disk, network) | planned | applets | `get_system_info` / `get_cpu_info` (public kernel API) | Pulse ≥ 1 s; zero when hidden |
| **Battery** | planned (backend exists) | applet | `OSInterface::Battery()` reads `/dev/power/acpi_battery` like PowerStatus (F19) | Hidden when no battery |
| **Brightness** | planned (backend exists) | applet / quick settings | `OSInterface::Set/Brightness` | Hidden when unsupported |
| **Volume** | planned | applet | Media Kit (backend missing) | — |
| Configurable / multiple panels | **exists** | built-in | `panels[]` schema | — |
| Auto-hide panels | partial (schema key `panels.*.autoHide`) | built-in | Panel edge-trigger via input filter or 1 px sentinel window | Reserve-space interplay |
| Deskbar tray replicants in Lumina panels | planned | applet (`tray`) | Host a `BShelf` (F20) | [03](03-architecture.md) §2.4 |

**Built-in vs plugin rule.** Anything that needs engine access or must work
when the panel is not running (switcher, overview, key routing, restore
journal) is **built-in** to `lumina_server`/engine. Anything that presents
information is an **applet** (`instantiate_lumina_applet`) and can be
dropped onto the desktop as a replicant.

### 8.1 Key-binding collision policy (§1 principle 3)

Reserved, never bound by Lumina defaults: Ctrl+Tab and Ctrl+` (switcher,
only via `switcher.mode`), Cmd+F1…F12 and Cmd+` (F3), Cmd+Ctrl+arrows ±Shift
(F16), Option-alone, Option+Tab and Option+arrows (S&T, F7), and
Shift+Cmd+Ctrl+Esc (F3). Proposed Power User tiling defaults use
**Cmd+Option+arrows/numpad**. S&T only activates on Option *alone* (F7), so
they don't collide with it. The schema validator rejects bindings that collide
with the reserved list or with the user's Shortcuts preflet map.

## 9. Per-feature summary

| Feature | Status | Level | Main risk | Test | Cl | Mo | PU | Mi |
|---|---|---|---|---|---|---|---|---|
| Snap (edges/corners) | exists | L2 | S&T/magnetic interplay | drag matrix | off | on | on | on |
| Custom zones/layouts | planned | L2 | size limits | host geometry | – | – | on | – |
| Keyboard tiling | planned | L2 | key collisions | collision suite | – | – | on | – |
| Rules (fix gaps §3) | partial | L2 | timing vs app positioning | rule suite | on | on | on | on |
| S&T stacking from Lumina | planned | L2 (internals) | hrev churn | stack/snap/restore | – | on | on | – |
| On-top / pinned | planned | L2 | stale client feel | revert-on-exit | menu | menu | menu | menu |
| Remembered geometry | planned | L1+L2 | fighting apps | StyledEdit test | – | on | on | – |
| Restart restoration | partial | L2 | token reuse | kill -9 drill | on | on | on | on |
| Multi-monitor | blocked | L3 | upstream | — | – | – | – | – |
| Lumina switcher | planned | L2 (L1 filter fallback) | fallback correctness | crash-mid-hold | stock | on | on | stock |
| Thumbnails | planned | L2/L1 | lock-time cost, privacy | budget counters | – | cached | cached+visible | – |
| Workspace names/OSD | planned | L1 | — | UI | – | on | on | – |
| Workspace DnD | planned | L2 | — | applet DnD | where shown | ← | ← | ← |
| Multi-workspace masks | planned | L2 | — | mask round-trip | menu | menu | menu | menu |
| Activity presets | planned | L1 data | workspace-count change | try-mode | – | – | offered | – |
| Overview | planned | L1/L2 | memory | open-time | – | avail. | on | – |
| Notification history | planned | L1 / replacement | replacing a server | fallback drill | – | – | own only | – |
| Clipboard history | planned | L1 | privacy | exclusion list | – | – | – | – |
| Quick settings / battery / brightness / volume | planned | L1 | driver variance | VM + laptop | – | on | on | – |
| Searchable launcher | partial | L1 | index cost | query timing | Deskbar | on | on | Deskbar |

("–" = available but off; "menu" = available as a window-menu or command
action, nothing automatic.)

## 10. Implementation sequence

Sizing follows [05](05-roadmap.md): **S** ≤ 2 weeks · **M** 2–6 weeks · **L**
6–12 weeks. Prerequisite for everything: the first real-hardware run
(status.md, acceptance S1–S8). Nothing below starts on assumptions that run
has not confirmed.

| Phase | Scope | Depends on | Size | Exit criteria |
|---|---|---|---|---|
| **MT0 — Verify** | Run S1–S8; measure hook latency, `ReadBitmap` cost and whether the target is double-buffered (F14) on VM and real hardware | first Haiku run | S | Numbers recorded in vfx.md; go/no-go for engine capture |
| **MT1 — Fix & extend rules** | Gaps §3 (look/feel, four-edge reservations, placement in engine); actions `layer`, `pinned`, `floating`, `snap`; session-state journal + revert on exit | MT0 | M | Rule suite passes; `lumina mode stock` restores every feel/mask |
| **MT2 — Key router** | Engine `KeyPressed` consumption with heart-beat gating; collision validator; Shortcuts preflet import | MT0 | S | Kill client → stock behaviour on next key |
| **MT3 — Switcher** | `lumina_switcher` (icons, MRU, groups, workspace sections, search); tap/hold semantics; Deskbar-hidden case | MT2 | M | Targets §11 met; Twitcher still selectable |
| **MT4 — Snapshot cache** | Event-driven cached snapshots; area transfer; budgets; exclusion list; switcher thumbnails | MT0, MT3 | M | ≤ 1 ms per capture at P95; cache bounded |
| **MT5 — Layouts & tiling** | Custom zones, preview outline, named grids, keyboard tiling, per-workspace arrangements | MT1, MT2 | L | Host geometry suite; drag matrix |
| **MT6 — S&T integration** | Group-aware snapping/animation, "stack with", archive/restore across restart | MT1 | M | Stack drill passes on the pinned hrev |
| **MT7 — Workspaces** | Names, OSD, applet DnD, masks, rule assignment by name, activity presets | MT1 | M | DnD and masks round-trip |
| **MT8 — Overview** | Overview window, DnD organisation, search & launch, optional transition | MT4, MT6, MT7 | M | Targets §11 met |
| **MT9 — Productivity applets** | Quick settings, battery/brightness/volume (add Media Kit to `OSInterface`), monitors, searchable launcher, tray, clipboard history | MT0 (independent of MT1–8) | M–L | Each applet works as a replicant |
| **MT10 — Notification history** (optional) | Own-notification history; opt-in compatible receiver as a separate package | MT9 | M | Fallback drill restores stock server |
| **Deferred** | Multi-monitor, live thumbnails of obscured windows | upstream | — | Tracked as upstream proposals |

Critical path: **MT0 → MT2 → MT3 → MT4 → MT8**. MT9 can run in parallel from
MT0 onward.

## 11. Performance targets

All are measured on the reference real-hardware machine and the VM, P95
over 200 runs, with counters exposed through `lumina engine` stats and
`lumina diagnostics`.

| Metric | Target |
|---|---|
| Idle cost | **0 wake-ups/s** from engine, switcher and overview with nothing happening (`EngineClock` asleep; no `B_PULSE_NEEDED` in hidden surfaces); panel applets ≤ 1 pulse/s each |
| `KeyPressed` hook (any key) | < 20 µs; never blocks (zero-timeout sends) |
| Ctrl+Tab tap (quick switch, no UI) | activation < 30 ms after Ctrl release |
| Switcher open (after hold threshold) | first complete frame **< 50 ms**, including title refresh for ≤ 50 windows |
| Switcher search | filter update < 5 ms per keystroke for 200 windows |
| Snapshot capture | ≤ 1 ms per capture in-hook (downscaled); skipped if over budget |
| Snapshot cache memory | ≤ `switcher.cacheMB` (default 24 MB); freed on window removal |
| Overview open | < 100 ms with 20 windows from cache; no new captures |
| Snap zone hover → preview outline | ≤ 1 frame (16.7 ms at 60 Hz) |
| Workspace switch | no added latency vs stock when animations are off; OSD never delays the switch |
| Rule evaluation | < 50 µs per window for 100 rules (host) |
| Clipboard watcher | < 1 ms per `B_CLIPBOARD_CHANGED`; memory ≤ 2 MB at default cap |
| Classic personality | no measurable difference from stock in input latency or idle CPU; RSS within the [05](05-roadmap.md) budget (≤ 15 MB) |

A feature that misses its target at P95 on the reference hardware is
disabled in that personality by default until the target is met.
