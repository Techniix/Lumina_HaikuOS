# Lumina for Haiku

A Haiku-native, deeply customisable desktop shell that takes its design from
the [Lumina Desktop](https://github.com/lumina-desktop/lumina). It extends
Haiku's own app_server, Deskbar and Tracker instead of replacing them.

> **Status: early implementation.** The foundation, Window Engine, server,
> panel, CLI and mode switcher are written and **compile-verified against
> real Haiku headers**; the portable core is unit-tested. **Nothing has run
> on Haiku yet** — see [docs/status.md](docs/status.md) for the exact state.

Lumina integrates at three levels, each optional above the first:

| Level | Runs on | Adds |
|---|---|---|
| 1 Native | stock Haiku, client side | panel, modes, profiles, rules, snapshot glass, Lumina-initiated transitions |
| 2 Enhanced | stock Haiku, Window Engine inside app_server | snapping and real window animation; close/minimize/restore animations **whoever starts them**; verified-background fades; cursor themes |
| 3 Advanced | app_server with the optional patch in [`patches/haiku`](patches/haiku) | true translucency, live blur, soft shadows and real fades for Lumina's own surfaces |

## Principles

* **The default is Haiku.** *Haiku Classic* looks exactly like stock Haiku:
  the engine's decorator *is* the one stock Haiku uses (`SATDecorator`), and
  a different decorator you chose (Flat, Mac, BeOS…) is kept, not replaced.
* **Native first.** It's built on app_server, the Interface/Storage/App Kits,
  launch_daemon, `BNotification` and Haiku packages, with no X11, Wayland,
  D-Bus or systemd. Qt 6/QML is only planned for the optional Settings Center.
* **Personalities are profiles.** Classic, Modern, Power User and Minimal are
  data (`data/personalities`) that you can duplicate, export as a
  `.luminaprofile` file and share.
* **Always reversible.** You can return to stock Haiku from the Lumina Mode
  preflet, the Deskbar tray item, `lumina mode stock`,
  `lumina restore-stock` (works without the server),
  Ctrl+Alt+Shift+Backspace, or a Safe Mode boot. Lumina also falls back to
  stock automatically after repeated crashes, and installing it changes
  nothing until you enable it.

## What's inside

| | |
|---|---|
| **Window Engine** | app_server decorator add-on: window events, move/resize/animate commands, edge/corner snapping, real-window geometry animation (springs, curves), open-transition parking, ABI guard. Thread-free by design ([docs/engine.md](docs/engine.md)) |
| **Visual Effects Engine** | portable policy (profiles, capabilities, governor, automatic fallbacks) plus Haiku backends: frosted panel glass (capture / edge / wallpaper), snapshot-proxy transitions (minimize, restore, open, workspace slide). An honest account of what a non-compositing app_server allows is in [docs/vfx.md](docs/vfx.md) |
| **lumina_server** | layered, schema-validated configuration; stock snapshot and restore; crash watchdog; window registry and rules |
| **Lumina Panel** | native panels drawn with BControlLook: launcher (Deskbar menu), window list, workspaces, clock, show desktop; applet add-on API |
| **Lifecycle transitions** | the engine captures a departing window inside app_server's locked hide path and hands it to a proxy that shrinks, flies to its panel button or crossfades, for closes and minimizes started by any application, Deskbar or the title tab ([docs/07](docs/07-advanced-integration-research.md)) |
| **Cursor themes** | PNG themes converted to app_server's own CursorSet format and applied through its built-in, previously unused loader; `lumina cursors` |
| **Overlay compositor** (optional) | a small app_server patch composing Lumina-owned overlay windows at the back-to-front copy; found at run time, never required |
| **lumina** | CLI for control and diagnostics |

## Quick start (on Haiku)

```sh
git clone https://github.com/Techniix/Lumina_HaikuOS && cd Lumina_HaikuOS
tools/haiku/build.sh --engine && tools/haiku/install-dev.sh
lumina mode lumina modern     # try it
lumina mode stock             # back to stock Haiku
```

## Documents

| | |
|---|---|
| [status.md](docs/status.md) | what is implemented, tested, or still planned; known limitations |
| [development.md](docs/development.md) | building, testing, compile-checking from Linux, debugging |
| [engine.md](docs/engine.md) | Window Engine design and the app_server facts behind it |
| [vfx.md](docs/vfx.md) | Level 1 visual-effects research: evidence and measurements |
| [07](docs/07-advanced-integration-research.md) | advanced integration: source traces and designs for every former "impossible" item |
| [08](docs/08-multitasking-roadmap.md) | window management, switcher, workspaces, overview, productivity |
| [09](docs/09-system-integration-architecture.md) | integration levels, modules, capability detection, fallbacks |
| [patches/haiku](patches/haiku/README.md) | the optional app_server patch set |
| [config-reference.md](docs/config-reference.md) | every setting (generated from the schema) |
| [01](docs/01-feasibility-report.md) · [02](docs/02-lumina-source-audit.md) · [03](docs/03-architecture.md) · [04](docs/04-dependency-graph.md) · [05](docs/05-roadmap.md) · [06](docs/06-proof-of-concept.md) | original research and architecture |
| [tools/vm/README.md](tools/vm/README.md) | VM testing and the acceptance checklist |

## License

MIT. Vendored Haiku private headers in `src/liblumina/compat/haiku_private`
are MIT (Haiku, Inc.).
