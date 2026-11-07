# 04 — Dependency Graph

## 1. Component dependencies

Solid arrows are hard dependencies; dotted arrows are optional ones.

```mermaid
graph TD
  subgraph Haiku["Haiku (unmodified)"]
    AS[app_server]
    IS[input_server]
    LD[launch_daemon]
    REG[registrar / BRoster]
    TR[Tracker]
    DB[Deskbar]
    NS[notification_server]
    MK[Media Kit]
    BE[libbe: Interface / Storage / App Kits]
    PRIV[private: WindowInfo.h, BLaunchRoster, ServerProtocol]
  end

  subgraph HP["HaikuPorts"]
    QT[qt6_base 6.10.x + libqhaiku QPA]
    QML[qt6_declarative]
    KA[karchive6]
    KW[kwidgetsaddons6]
    KIR[kirigami6]
  end

  LIB[liblumina]
  SRV[lumina_server]
  ENG[Lumina Window Engine<br/>DecorAddOn]
  INP[LuminaInput filter]
  PNL[Lumina Panel + applets]
  MODE[Lumina Mode preflet/replicant/CLI]
  SET[Lumina Settings Center]
  WH[QML Widget Host]

  LIB --> BE
  LIB --> PRIV
  SRV --> LIB
  SRV --> LD
  SRV --> REG
  SRV --> NS
  SRV --> MK
  SRV -->|AS_TALK_TO_DESKTOP_LISTENER| ENG
  ENG --> AS
  INP --> IS
  INP -->|BMessage| SRV
  PNL --> LIB
  PNL -->|subscribe| SRV
  PNL -.->|replicants| TR
  PNL -.->|tray replicants| DB
  MODE --> LIB
  MODE -->|Lmod| SRV
  MODE -.->|Deskbar replicant| DB
  SET -->|Lset| SRV
  SET --> QT
  SET --> QML
  SET -.-> KA
  SET -.-> KW
  SET -.-> KIR
  WH --> QT
  WH --> QML
  WH -->|subscribe| SRV
```

## 2. Build-order / milestone dependencies

```mermaid
graph LR
  P0[P0 Toolchain & repo<br/>Haiku VM + real HW, CI build]
  P1[P1 liblumina core<br/>config, profiles, IPC, OS iface]
  P2[P2 Window Engine PoC<br/>pass-through decorator + listener]
  P3[P3 lumina_server + Mode switching<br/>snapshot/restore, watchdog]
  P4[P4 Lumina Panel + applets]
  P5[P5 Rules, snapping, tiling, work-area]
  P6[P6 Settings Center Qt6/QML]
  P7[P7 Widgets: replicants + QML host]
  P8[P8 Personalities & profile sharing]
  P9[P9 Theme packs, icons, sounds, polish]
  P10[P10 HaikuPorts recipe & 1.0]

  P0 --> P1 --> P2 --> P3
  P1 --> P4
  P3 --> P4
  P2 --> P5
  P3 --> P5
  P3 --> P6
  P4 --> P7
  P6 --> P7
  P4 --> P8
  P5 --> P8
  P6 --> P8
  P8 --> P9 --> P10
```

## 3. External dependency inventory

| Dependency | Needed by | Mandatory? | Status on Haiku |
|---|---|---|---|
| Haiku R1/beta6+ (hrev pinned) | all | yes | ✅ released 2026‑08‑26 |
| `DecorAddOn`/`DesktopListener` internal ABI | Window Engine | yes for WM features | ✅ present, ⚠️ unstable ABI |
| `WindowInfo.h` (private) | liblumina window registry | yes | ✅ used by Deskbar |
| `BLaunchRoster` (private) | mode switching (Deskbar enable/disable) | yes | ✅ used by `launch_roster` CLI |
| `BInputServerFilter` | LuminaInput | optional | ✅ public add-on API |
| `BNotification` | notifications | yes | ✅ public |
| Qt 6 base + QPA | Settings Center, Widget Host | optional | ✅ 6.10.3 |
| qt6_declarative | Settings Center, Widget Host | optional | ✅ |
| KF6 KArchive | profile bundles in Settings | optional | ✅ 6.29.0 |
| KF6 KWidgetsAddons / Kirigami6 | Settings UI | optional | ✅ 6.29.0 |
| KF6 KConfig | KDE-app colour/font sync | optional, later | ✅ 6.29.0 |
| KF6 KIO | — | **not used** | ✅ available if users want KDE apps |
| X11 / Xlibe / Wayland / D-Bus / PAM / systemd | — | **never** | — |

## 4. Upstream proposals (nice to have, never blocking)

| Proposal | Upstream | Unlocks |
|---|---|---|
| Expose `BWindow*` via `nativeResourceForWindow("bwindow")` | `threedeyes/qt6-haikuplugins` | QML windows can pin workspaces/feel directly |
| Stable, versioned desktop-listener add-on API | Haiku | Removes per-hrev rebuild of the engine |
| Public work-area / reserved-strut API | Haiku app_server | Panels without an engine add-on |
| Cursor theme setting | Haiku app_server | Cursor packs |
| Compositing / per-window alpha | Haiku app_server | Real transparency and window animations |
