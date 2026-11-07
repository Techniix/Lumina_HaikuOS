# 01 — Technical Feasibility Report

*Lumina Desktop, KDE Frameworks and Plasma capabilities on HaikuOS*
Research date: 2026‑11‑07 · Target: Haiku R1/beta6 (released 2026‑08‑26) and nightlies · x86_64 first

## Executive summary

**Feasible, but not as a "port".** Lumina 1.x is an X11 desktop that delegates window management to Fluxbox and talks to the OS by shelling out to BSD/Linux tools. Almost none of that layer survives contact with Haiku, and the requirement to preserve app_server, Deskbar and Tracker rules out recreating it.

What *does* transfer is Lumina's design: a modular, plugin-based shell with plain-text, live-reloading appearance profiles. The recommendation is to build **Lumina for Haiku** as a new native shell. It would take Lumina's plugin catalog, profile model and configuration vocabulary, implement them on Haiku's kits, and keep the user's stock Haiku setup as a mode that is always one switch away.

Key findings:

1. **Haiku already has a supported extension point for window management inside app_server.** Decorator add-ons (`DecorAddOn`) can supply a `Decorator`, a `WindowBehaviour` *and* a list of `DesktopListener`s. A listener receives window add/remove, move, resize, activation, workspace, mouse and key events. This is how Stack & Tile works, and the listeners are swapped when the decorator changes (`Desktop.cpp` ~L2306–2331). **Fluxbox can be replaced by an app_server add-on, with no OS modification.** The catch: this is an internal app_server ABI, so the add-on must be rebuilt for each Haiku revision.
2. **Qt 6 is mature on Haiku.** HaikuPorts ships `qt6_base-6.10.3` with a native QPA (`libqhaiku.so`, from `threedeyes/qt6-haikuplugins`), a Haiku widget style (`libqhaikustyle.so`) and about 40 Qt 6 module recipes, including `qt6-declarative` (QML/Quick).
3. **KDE Frameworks 6 are mostly there.** HaikuPorts has 84 framework recipes at **KF6 6.29.0**, including KConfig, KIO, KArchive, KWidgetsAddons, Kirigami, KCoreAddons, KI18n, KIconThemes, KXmlGui and KNotifications. KIO builds with **no D-Bus requirement** in its recipe. Plasma 6 pieces stop at `kdecoration6`, `breeze6` and `plasma-activities`: **no KWin, no plasma-workspace, no plasmashell.**
4. **XFCE is absent from HaikuPorts.** GTK 3 runs via Xlibe (an Xlib-on-Haiku shim). But xfwm4, xfdesktop and xfce4-panel are EWMH/X11 window-manager programs, and xfconf needs D-Bus. Porting them would mean recreating the Linux desktop stack.
5. **Hard platform limits that shape the design:**
   * No public API to move or resize *other* applications' windows. Window lists and minimize/raise exist only through the semi-private `WindowInfo.h` API that Deskbar uses.
   * **No compositing in app_server.** True per-window translucency and window open/close animations need app_server changes, which are out of scope. Panel "transparency" has to be pseudo-transparency.
   * Qt and native code cannot share a process: Qt creates the `BApplication`. QML widgets therefore live in their own process and cannot be embedded into native panels.
   * Qt's Haiku QPA returns `nullptr` from `nativeResourceForWindow()`, so Qt windows can't currently get at their `BWindow` (for example to call `SetWorkspaces(B_ALL_WORKSPACES)`). This needs a small upstream patch.

### Effort comparison (Haiku-native constraints applied)

| Option | Approach | Reuse | New native code | Estimate | Fit with requirements |
|---|---|---|---|---|---|
| **A. Lumina for Haiku (recommended)** | Native shell + app_server add-on; Qt/QML only for settings & widgets | Design, schema, plugin catalog; some Qt code | ~25–40 kLOC | **L** (≈ 9–14 person-months to 1.0) | ✅ Preserves app_server/Deskbar/Tracker |
| B. Literal Lumina port | Keep Lumina 1.x Qt widgets, replace `LXCB`/`LOS`/Fluxbox | ~40 % of desktop code | ~15–25 kLOC of shims | **L** (≈ 8–12 pm) | ⚠️ Qt-drawn shell; Classic look only emulated; cross-process limits break tray/taskbar plugins |
| C. XFCE | GTK3 via Xlibe; rewrite xfwm4/xfdesktop/panel backends; xfconf over D-Bus | Apps only | Large; panel/desktop are X11-native | **XL** (≈ 18–30 pm) | ❌ Requires D-Bus session bus and X11 semantics |
| D. KDE Plasma 6 | plasmashell + KWin | Frameworks (already ported) | KWin cannot run; Wayland/X11 compositor + D-Bus everywhere | **XXL / not viable** | ❌ Contradicts "do not recreate Linux desktop infrastructure" |

The estimates assume one experienced Haiku/Qt developer. "pm" means person-months.

## Findings by research question

### 1. Lumina architecture, OS abstraction, Qt, Fluxbox, X11

See [`02-lumina-source-audit.md`](02-lumina-source-audit.md). In short:

* ~68 kLOC of Qt 5 C++ in total. The desktop itself (`lumina-desktop`) is 15.8 kLOC, and 29 of its files touch X11.
* The OS layer `LOS` is a static class with one `.cpp` per OS, implemented almost entirely by **parsing shell-command output** (`sysctl`, `mixer`, `df`, `acpi`, `shutdown`…).
* Fluxbox is an external process: started with generated rc files, reloaded with `SIGUSR2`, and its config is edited by `lumina-config` (77 references).
* The unfinished Lumina 2 tree (`lumina-desktop-unified`) puts its own X11 WM in-process. It is **not a better starting point** for Haiku, because its WM is X11-only. Its QML root window and unified settings are worth borrowing as ideas.

### 2. BSD/Unix-specific interfaces

There are 17 categories, listed in audit §5: X11 family, external WM, compositor, xinit session, XDG (desktop entries, MIME, icons, autostart, base dirs), D-Bus, PAM, sudo/operator privilege model, FHS prefixes, sysctl/ACPI, OSS/ALSA/Pulse mixers, text-parsed Unix tools, ZFS, TrueOS tools, inter-process POSIX signals, X input utilities, and `~/.config` paths. Each has a native mapping in audit §2–§3, or is dropped.

### 3. Adapting to app_server, Interface Kit, Storage Kit and the application lifecycle

| Lumina concept | Haiku-native realisation |
|---|---|
| `LSession` (QApplication + X event filter) | `lumina_server`: a `BApplication` launched by `launch_daemon`, with state in `BMessage`s and IPC by `BMessenger` |
| Panels (`LPanel` QWidget + struts) | `LuminaPanel`: native `BWindow`s (`B_NO_BORDER_WINDOW_LOOK`, `B_FLOATING_ALL_WINDOW_FEEL`, `B_ALL_WORKSPACES`), drawn with `BControlLook` so they inherit the system look |
| Panel plugins (`LPPlugin` QWidget) | Native `BView` add-ons with archive/instantiate, the same mechanism as replicants |
| Desktop plugins (`LDPlugin`) | **Tracker desktop replicants** (Tracker's desktop is a `BShelf`), plus QML widgets in a separate host |
| `.desktop` files, XDG menu | App signatures, `BMimeType`, Deskbar's menu folder (`~/config/settings/deskbar/menu`), `BQuery` on `BEOS:APP_SIG` |
| XDG MIME / default apps | `BMimeType::GetPreferredApp()` / FileTypes |
| Icon themes | HVIF icons from `BMimeType::GetIcon()`/`BNodeInfo`, with optional icon-pack overlay (architecture §6) |
| QSettings in `~/.config` | Settings in `~/config/settings/Lumina/`, watched with `BNodeMonitor` for live reload |
| Autostart | `launch_daemon` user jobs (`~/config/settings/launch/` or package `data/launch/user/`) |
| `lumina-open` | `BRoster::Launch(entry_ref)` |
| Notifications (message center) | `BNotification` → `notification_server` (native); appearance via Notifications preflet settings |

### 4. Can Fluxbox be replaced with native Haiku window management?

**Yes, completely.** Fluxbox's jobs split into three groups:

* **Already native:** decorations, focus, stacking, workspaces, Stack & Tile, magnetic borders (`MagneticBorder.cpp`), focus-follows-mouse modes (`set_mouse_mode`), accept-first-click.
* **Configurable natively:** decorator choice (`AS_SET_DECORATOR`), decorator settings (`AS_SET_DECORATOR_SETTINGS`), UI colors (`set_ui_color`/`set_ui_colors`), control look (`AS_SET_CONTROL_LOOK`), workspace count and layout.
* **Missing — supplied by the Lumina Window Engine (a `DecorAddOn`):** rule-based placement, snapping zones, keyboard tiling, per-app decoration overrides, work-area reservation for panels, and window rules (always-on-top, workspace pinning, initial geometry).

Besides the built-in Default decorator, Haiku ships four decorator add-ons (Be, Flat, Mac, Win), so parametric decorator theming is well-trodden ground.

### 5. Qt 6 and QML as the shell foundation

Qt 6 works well on Haiku, but it should **not** be the foundation of the shell itself:

* A Qt-drawn panel can only *imitate* the Haiku look. A native panel gets `BControlLook`, system UI colors and font settings for free, and updates live when the user changes them in Appearance.
* Qt windows are full `BWindow`s, but the QPA exposes no native handle. Workspace pinning, feel and look control are limited to what `QHaikuWindow` maps from `Qt::WindowFlags`. For example, `WindowStaysOnTopHint` maps to `B_FLOATING_ALL_WINDOW_FEEL` and `FramelessWindowHint` maps to `B_NO_BORDER_WINDOW_LOOK`.
* Qt costs memory and start-up time. The "Minimal" and "Classic" personalities should not have to load it.

**Where Qt/QML clearly wins:** the Settings Center (deep, searchable, live-preview UI), the theme/profile editor, and a QML desktop-widget engine for user-authored widgets. Use Qt 6 + QML there, optionally with Kirigami6 (available in HaikuPorts).

### 6. Reusable KDE Frameworks

| Framework | HaikuPorts | Use in this project | Notes |
|---|---|---|---|
| **KConfig** (6.29) | ✅ | Qt side only: optional mirror of Lumina profiles for KDE apps' colour/font settings | Don't make it the canonical store: native components shouldn't link Qt |
| **KArchive** (6.29) | ✅ | Profile/theme bundle import/export in Settings Center | The native side can use the `zip`/`unzip` CLI or libarchive instead |
| **KWidgetsAddons** (6.29) | ✅ | Settings Center widgets (`KPageWidget`, `KColorCombo`, `KFontChooser`, `KMessageWidget`) if the Settings Center is QtWidgets-based | Light dependency, Qt-only |
| **KIO** (6.29) | ✅ (no D-Bus in recipe) | **Not recommended** for the shell | Tracker + Storage Kit are the native file layer. KIO pulls in KService, Solid, KCrash, KBookmarks… It's worth having only for KDE apps users install anyway |
| Kirigami6 | ✅ | QML Settings Center UI | Optional; plain QtQuick.Controls also works |
| KCoreAddons, KI18n, KIconThemes | ✅ | Transitive, if Kirigami/KWidgetsAddons are used | — |
| KGlobalAccel, KWindowSystem, KStatusNotifierItem, KIdleTime | (ported) | **Do not use** | Built on X11/Wayland/D-Bus semantics; use native input filters, `WindowInfo`, Deskbar replicants and the screen_saver filter instead |
| KNewStuff | ✅ | Later, optional: browse shared profiles | Needs a hosted provider file; defer |

### 7. Effort: Lumina vs XFCE vs Plasma

See the table above. The deciding factor is **window management and the session model**:

* Lumina is the only one of the three whose WM is *external and optional* and whose OS layer is *designed to be swapped*.
* XFCE's panel and desktop depend on EWMH and on xfconf over D-Bus.
* Plasma depends on KWin, which in Plasma 6 is a Wayland compositor.

### 8. Native Haiku and Qt applications side by side

* Qt apps already look native: `libqhaikustyle`, a native file dialog helper (`qhaikuplatformdialoghelpers`), and Deskbar replicants for `QSystemTrayIcon`.
* The Lumina panel lists **all** windows (native, Qt, GTK/Xlibe, WINE) because it reads the app_server window list, not toolkit-specific hints.
* Lumina theme changes go through **system** settings (UI colors, fonts, decorator, control look), so native, Qt (via the QPA's `qhaikutheme`) and GTK apps all follow.
* KDE apps already in HaikuPorts (Dolphin, Konsole, Kate, Okular, KDevelop) can be offered as optional components of the "Power User" personality without making them dependencies.

### 9. HaikuPorts foundations

Usable as-is: `qt6-*` (6.10.3), `kde-frameworks/*` (KF6 6.29.0), `extra_cmake_modules`, `kirigami6`, `breeze-icons`, `dbus` (only if a KDE app needs it, never for the shell), `xlibe`, `gtk3`. HaikuPorts also gives us the delivery channel: Lumina ships as `.hpkg` packages from a HaikuPorts recipe, and theme/icon packs ship as separate packages installable through HaikuDepot.

### 10. Roadmap

See [`05-roadmap.md`](05-roadmap.md) and the recommended proof of concept in [`06-proof-of-concept.md`](06-proof-of-concept.md).

## Risks and blockers

| # | Risk / blocker | Severity | Mitigation |
|---|---|---|---|
| R1 | `DecorAddOn`/`DesktopListener` is an **unstable internal ABI**, so the add-on breaks across hrevs | High | Ship in a package with a strict `haiku == <hrev>` requirement; keep it thin (mechanism only, policy lives in `lumina_server`); upstream a stable listener API in the long term |
| R2 | An add-on crash **crashes app_server** | High | Minimal in-server code; no allocation-heavy work on event paths; fuzz/test in VM; install as a *user* add-on so the boot option "Disable user add-ons" bypasses it; one-click revert to the Default decorator |
| R3 | No compositing → no real translucency or window animations | Medium | Pseudo-transparency for panels; animations limited to Lumina's own surfaces; mark compositor work as an optional upstream contribution, not a dependency |
| R4 | Window list / actions use **private** `WindowInfo.h` | Medium | The same API Deskbar uses; stable since BeOS days; wrap it in one module |
| R5 | Qt QPA has no `nativeResourceForWindow` | Low–Med | Small upstream patch to `qt6-haikuplugins` exposing `BWindow*`; meanwhile route workspace/feel requests for QML windows through the Window Engine rules |
| R6 | Replacing Deskbar is not a supported scenario (Deskbar is a `service` in `data/launch/user`) | Medium | Classic/Modern keep Deskbar; personalities that hide it use `BLaunchRoster::SetEnabled/Stop("x-vnd.Be-TSKB")` and restore it on exit or crash |
| R7 | Mixing native panels with QML widgets → two rendering stacks | Low | Qt is optional and loaded only when QML widgets or the Settings Center are in use |
| R8 | Lumina upstream is quiet (last master commit Jan 2024; Qt 6 port unfinished) | Low | We depend on its design, not its code |

## Verdict

Proceed with **Option A**. Validate R1, R2 and R6 first with the proof of concept: Window Engine add-on, native panel, and mode switch. Everything else depends on those three.
