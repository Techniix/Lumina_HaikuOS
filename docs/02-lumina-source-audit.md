# 02 — Lumina Source Audit

**Audited tree:** `lumina-desktop/lumina` @ `f10fadc` (master, last commit 2024‑01‑01, "Merge pull request #832 from lumina-desktop/build-docs").
Release line: 1.6.x. Toolkit: **Qt 5.11+** via qmake (`src-qt5/`). There is no Qt 6 tree; Qt 6/CMake work exists only on side branches (`cmake-agrozin`, `cmakify-all-the-things`).

All numbers below come from `grep`/`wc` over the checked-out tree; they are meant to size the work, not as exact LOC accounting.

## 1. Component inventory

| Component | Lines (C++/H) | X11/XCB files | Fluxbox refs | Role | Verdict for Haiku |
|---|---:|---:|---:|---|---|
| `core/libLumina` | 14,455 | 8 | 2 | Shared utils, OS abstraction (`LOS`), XDG, X11 (`LXCB`), RandR, themes, icon cache | **Split**: keep pure-Qt utils; replace `LuminaX11`, `LuminaRandR-X11`, `LuminaXDG`, `XDGMime`, `LuminaOS-*` |
| `core/lumina-desktop` | 15,823 | 29 | 9 | The 1.x desktop: session, panels, desktop plugins, screensaver | **Re-implement natively**, reuse plugin taxonomy & config semantics |
| `core/lumina-desktop-unified` | 8,692 | 9 | 0 | Unfinished 2.0 "single binary" desktop incl. its own X11 WM (`NativeWindowSystem.cpp`, 1,152 lines) | **Do not port**; mine ideas (QML root window, unified settings) |
| `core/lumina-session` | 462 | 1 | 2 | `xinit` wrapper, starts Fluxbox, compton/picom, numlockx | **Replace** with launch_daemon jobs |
| `core/lumina-theme-engine` | 2,611 | 1 | 0 | Qt platform-theme plugin + QSS engine (fork of qt5ct) | **Do not port**; Haiku has `libqhaikustyle` + system UI colors |
| `core/lumina-open` | 802 | 1 | 0 | XDG-based "open with" | **Replace** with `BRoster::Launch` / `BMimeType` |
| `core/lumina-checkpass` | (C) | 0 | 0 | PAM password check for lock screen | **Replace**: Haiku has no PAM; use `getpwnam`/shadow via Haiku's `login`-equivalent or skip lock screen in v1 |
| `core/lumina-info`, `lumina-pingcursor` | ~200 | 1 | 0 | About box; X cursor ping | Info: trivial; pingcursor: drop |
| `core-utils/lumina-config` | 5,699 | 1 | **77** | Settings GUI; deeply Fluxbox-aware (themes, keys, WM options) | **Rewrite** as Lumina Settings (see architecture) |
| `core-utils/lumina-xconfig` | 896 | 0 | 2 | xrandr GUI | **Drop** — Haiku Screen preflet |
| `core-utils/lumina-search` | 639 | 0 | 0 | File/app search | **Drop/replace** with BFS queries (`BQuery`) |
| `desktop-utils/*` (fm, textedit, archiver, mediaplayer, photo, screenshot, fileinfo, text-viewer) | ~13,600 | 2 | 0 | Stand-alone Qt apps | **Optional**; they build on Qt but duplicate Tracker, StyledEdit, Expander, MediaPlayer, ShowImage, Screenshot. Not in scope for v1 |
| `desktop-utils/lumina-sudo` | 382 | 0 | 0 | `sudo -n` GUI wrapper | **Drop** — Haiku is single-user, no privilege escalation |
| `experimental/*` | ~1,900 | 0 | 0 | Terminal, screencast | **Drop** |
| `src-cpp/framework-OSInterface*` | 2,476 | 1 | 0 | Newer Q_PROPERTY-based OS interface for 2.0 | **Reuse the interface shape** (it maps cleanly to QML) |

## 2. The OS abstraction layer (`LOS`)

`core/libLumina/LuminaOS.h` defines a static class `LOS`, implemented once per OS in `LuminaOS-<OS>.cpp` (FreeBSD, OpenBSD, NetBSD, DragonFly, Linux, Debian, Gentoo, NixOS, Slackware, Void, kFreeBSD, template). `OS-detect.pri` picks the file by qmake host spec; unknown hosts fall to `OS="Unknown"`.

Almost every implementation is **shell-outs parsed as text**. Top external commands invoked across the tree (`getCmdOutput`/`runCmd`/`startDetached`/`execute`):

```
acpi 18 · shutdown 15 · mixer 13 · dbus-send 13 · df 11 · lumina-open 10 · sysctl 9 · md 9
apm 8 · mount 7 · xscreensaver-command 5 · mixerctl 5 · xdg-open 4 · top 4 · pkill 4
numlockx 4 · amixer 4 · sudo/qsudo 6 · pgrep 3 · pactl 3 · iostat 3 · xrandr 2 · xinput 2
pciconf 2 · intel_backlight 2 · ifconfig 2 · zzz · zfs · xinit · xdg-user-dirs-update · pc-updatemanager · pc-mixer
```

### `LOS` → Haiku native mapping

| `LOS` function | BSD/Linux implementation | Haiku native replacement |
|---|---|---|
| `OSName`, `AppPrefix`, `SysPrefix`, `LuminaShare` | hard-coded `/usr/local` etc. | `find_directory()` / `BPathFinder` (`B_SYSTEM_DATA_DIRECTORY`, `B_USER_SETTINGS_DIRECTORY`, `B_FIND_PATH_DATA_DIRECTORY`) |
| `ControlPanelShortcut`, `AppStoreShortcut` | `.desktop` paths (AppCafe) | `/boot/system/preferences/…`, HaikuDepot signature `application/x-vnd.Haiku-HaikuDepot` |
| `ExternalDevicePaths` | parse `mount` | `BVolumeRoster` + `B_WATCH_MOUNT` node monitoring |
| `ScreenBrightness` / `setScreenBrightness` | `intel_backlight`, `xbacklight`, sysctl | `BScreen::GetBrightness()` / `SetBrightness()` |
| `audioVolume` / `setAudioVolume` / mixer | `mixer`, `mixerctl`, `amixer`, `pactl` | Media Kit: `BMediaRoster::GetAudioMixer()` → `BParameterWeb` → `BContinuousParameter` (what Deskbar's volume replicant does) |
| `hasMixerUtility` / `startMixerUtility` | `pc-mixer` | Launch `application/x-vnd.Haiku-Media` (Media preflet) |
| `userHasShutdownAccess` | `operator` group check | Always true (single user) |
| `systemShutdown` / `systemRestart` | `shutdown -p/-r` | `BRoster::_ShutDown()` (private, used by Deskbar) or `/bin/shutdown [-r]` |
| `systemCanSuspend` / `systemSuspend` | `sysctl hw.acpi`, `acpiconf`, `zzz` | Haiku has no supported user-facing suspend yet → report `false` |
| `hasBattery`, `batteryCharge`, `batteryIsCharging`, `batterySecondsLeft` | `sysctlbyname("hw.acpi.battery.*")`, `apm`, `acpi` | `/dev/power/acpi_battery/*` ioctls (as used by the PowerStatus app) |
| `Checksums` | `md5` | `BFile` + OpenSSL from HaikuPorts, or drop |
| `FileSystemCapacity` | `df` | `fs_stat_dev()` / `BVolume::Capacity()`/`FreeBytes()` |
| `CPUTemperatures` | `sysctl dev.cpu.*.temperature` | No general API → report empty |
| `CPUUsagePercent` | `sysctl kern.cp_times`, `top` | `get_cpu_info()` active-time deltas (what ActivityMonitor uses) |
| `MemoryUsagePercent` | `sysctl vm.stats.*` | `get_system_info()` (`used_pages`/`max_pages`) |
| `DiskUsage` | `iostat` | No equivalent → report unsupported |
| `RSSFeeds` | static list | Static list |

`src-cpp/framework-OSInterface.h` (the 2.0 interface) exposes the same concepts as `Q_PROPERTY`s with `*Supported()` capability checks (battery, volume, network, updates, power, brightness, cpu, memory, disk). **That capability-flag design is the right one for Haiku**, because several subsystems (suspend, temperatures, disk I/O) simply don't exist there.

## 3. X11 dependencies

* `QT += … x11extras …` in both `lumina-desktop.pro` and `lumina-desktop-unified/lumina-desktop.pro` — Qt X11 Extras does not exist on Haiku (or in Qt 6).
* `libLumina/LuminaX11.{h,cpp}` — 2,689 lines; class `LXCB` wraps ICCCM + EWMH: workspaces (`_NET_CURRENT_DESKTOP`, `_NET_NUMBER_OF_DESKTOPS`), client lists, active window, window states/types/actions, struts (`ReserveLocation`), frame extents, tray embedding (`EmbedWindow`/`UnembedWindow`/`TrayImage` – XEMBED), DPMS, damage, root painting, plus a partial WM (`WM_CreateWindow`, `setupEventsForFrame`).
* `libLumina/LuminaRandR-X11.cpp` — 395 lines of RandR.
* `lumina-desktop/LXcbEventFilter.cpp` — native XCB event filter feeding window-list, tray and workspace changes into `LSession`.
* 29 files in `lumina-desktop` touch X11; the heaviest consumers are `LSession`, `LPanel` (struts), `LWinInfo`, and the panel plugins **taskmanager**, **systemtray**, **desktopswitcher**, **showdesktop**, **systemdashboard**.
* `lumina-desktop-unified/src-events/` — 2,299 lines of X11 WM + key translation (`NativeKeyToQt.cpp`).

### X11 concept → Haiku native mapping

| X11/EWMH concept | Haiku equivalent |
|---|---|
| `_NET_CLIENT_LIST`, window titles/icons/pids | `get_token_list()` + `get_window_info()` (private `WindowInfo.h`, what Deskbar uses) + `BRoster` app list watching |
| `_NET_ACTIVE_WINDOW`, raise/minimize | `do_window_action(token, B_BRING_TO_FRONT / B_MINIMIZE_WINDOW)`, `do_bring_to_front_team()`, `do_minimize_team()` |
| Move/resize other clients | **No client API.** Only possible inside app_server (see architecture: Lumina Window Engine add-on) |
| `_NET_NUMBER_OF_DESKTOPS` / `_NET_CURRENT_DESKTOP` | `count_workspaces()`, `set_workspace_count()`, `current_workspace()`, `activate_workspace()` (public, `InterfaceDefs.h`); per-window `BWindow::SetWorkspaces()` |
| Struts / `_NET_WORKAREA` | No concept. Deskbar is special-cased by app_server for maximize. Panels must request work-area via the Window Engine add-on |
| XEMBED system tray | Deskbar replicants (`BDeskbar::AddItem`). Qt's `QSystemTrayIcon` on Haiku already maps to a Deskbar replicant (`qhaikusystemtrayicon.cpp`) |
| Root window painting / wallpaper | Tracker desktop + Backgrounds preflet (per-workspace background attributes on the Desktop folder) |
| RandR | `BScreen` / Screen preflet |
| DPMS / xscreensaver | Haiku `screen_saver` input_server filter + ScreenSaver preflet |
| `xinit`/session | `launch_daemon` user targets (`data/launch/user`) |
| Global hotkeys (XGrabKey) | input_server filter add-ons (`BInputServerFilter`), or `DesktopListener::KeyPressed` inside app_server |

## 4. Fluxbox integration

* `lumina-desktop/WMProcess.cpp`: starts `fluxbox -rc ~/.config/lumina-desktop/fluxbox-init -no-slit -no-toolbar`, restarts it on crash, sends **`SIGUSR2`** to reload config.
* `lumina-session/session.cpp`: `setupFluxboxFiles()` generates `fluxbox-init`, `fluxbox-keys`, `fluxbox-overlay` from templates in `lumina-desktop/fluxboxconf/`; also manages compton/picom (`pkill compton`, `pkill picom`).
* `lumina-config`: 77 references — window-theme picker, focus model, placement, keyboard shortcuts are all **Fluxbox `init`/`keys` file editors**.

Everything Fluxbox provides — decorations, focus policy, placement, keybindings, workspaces — Haiku's app_server already provides natively. Fluxbox has no role on Haiku. The *settings* it exposed (focus model, placement, keys, themes) remain useful and are re-homed in the Lumina Window Engine.

## 5. Every BSD/Unix-specific assumption found

1. **X11/XCB/EWMH/ICCCM/XEMBED/RandR/DPMS** (§3).
2. **External WM process** (Fluxbox) controlled by signals and generated rc files (§4).
3. **Compositor process** (compton/picom) killed via `pkill`.
4. **`xinit`-based session start**, `.desktop` session files in `/usr/share/xsessions` (`L_SESSDIR`).
5. **XDG Base Directory** (`XDG_CONFIG_HOME`, `setEnvironmentVars()`), **XDG desktop entries** (`XDGDesktop`, `systemApplicationDirs`), **XDG MIME** (`XDGMime`, `mimeapps`, `globs2`), **XDG icon themes**, **XDG autostart** — 73 files reference XDG/`.desktop`/freedesktop paths.
6. **D-Bus**: `dbus-send` shell-outs (13) and `QDBus` in the theme engine, message center, session and several distro `LuminaOS-*` files.
7. **PAM** (`lumina-checkpass/main.c`, `security/pam_appl.h`).
8. **Privilege model**: `sudo -n`, `qsudo`, `operator` group, "open as root" in lumina-fm.
9. **FHS / ports prefixes**: `/usr/local`, `/usr/share`, `/etc`, `L_ETCDIR`, `LIBPREFIX/qt5`.
10. **sysctl/ACPI** (`sys/sysctl.h`, `sysctlbyname("hw.acpi.battery.*")`, `acpiconf`, `apm`, `zzz`).
11. **OSS/sndio/ALSA/Pulse mixers** (`mixer`, `mixerctl`, `amixer`, `pactl`).
12. **Text-parsed Unix tools**: `df`, `mount`, `top`, `iostat`, `pciconf`, `ifconfig`, `pgrep`, `pkill`, `id`, `md5`.
13. **ZFS / boot environments** (`zfs`, lumina-fm snapshot browser).
14. **TrueOS/PC-BSD tools**: `pc-updatemanager`, `pc-sysconfig`, `pc-mixer`, AppCafe.
15. **POSIX signals to peer processes** (`::kill(pid, SIGUSR2)`).
16. **`numlockx`**, `xinput`, `xrandr`, `xscreensaver-command`.
17. **QSettings INI files in `~/.config/lumina-desktop/`** — portable, but the path convention is non-Haiku.

## 6. What is genuinely reusable

* **Plugin taxonomy and settings vocabulary.** Panel plugins (`userbutton, desktopbar, spacer, desktopswitcher, battery, clock, systemdashboard, systemstart, taskmanager, systemtray, homebutton, appmenu, applauncher, jsonmenu, line, alarm, audioplayer, quickcontainer`) and desktop plugins (`calendar, applauncher, desktopview, notepad, audioplayer, rssreader, systemmonitor, messagecenter, quickcontainer`) are a proven feature set. Their *config keys* (e.g. `panel<N>/location`, `pluginlist`, `hidepanel`, `customColor`) are a good starting schema.
* **"Appearance profiles" concept** — plain-text, live-reloaded, user-creatable. This is exactly what the "personalities" requirement asks for.
* **JSON menu plugin and menu-scripts** — dynamic menus generated by user scripts. Ports almost verbatim.
* **Pure-Qt plugins** with zero OS calls: `alarm, applauncher, audioplayer, clock, jsonmenu, line, spacer, quickcontainer` (panel) and `audioplayer, calendar, notepad, messagecenter` (desktop). These are useful *as QML widget references*, but the native shell will reimplement the panel ones in the Interface Kit.
* **`framework-OSInterface` capability pattern** for the Qt/QML side.
* **License**: BSD-3-Clause — compatible with Haiku's MIT code and with linking LGPL Qt/KF6.

**Bottom line:** perhaps 15–25 % of Lumina's code is reusable as code; most of the value is in its *design* (modularity, plain-text profiles, plugin catalog). The desktop core must be re-implemented against Haiku's kits.
