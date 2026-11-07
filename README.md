# Lumina for Haiku

A Haiku-native, deeply customisable desktop shell that takes its design from the [Lumina Desktop](https://github.com/lumina-desktop/lumina). It extends Haiku's own app_server, Deskbar and Tracker instead of replacing them.

> **Status: research & architecture phase.** No implementation code yet.

## Principles

* **The default is Haiku.** The *Haiku Classic* personality looks and behaves like stock Haiku, plus more capability.
* **Native first.** It uses app_server, the Interface/Storage/App Kits, launch_daemon, `BNotification`, Tracker replicants and Haiku packages. No X11, Wayland, D-Bus or systemd.
* **Qt 6/QML only where it pays off:** the Settings Center and user-authored desktop widgets. Qt is optional at runtime.
* **Personalities are profiles.** Classic, Modern, Power User and Minimal are configurations of one shell. They can be exported and shared.
* **Always reversible.** Stock Haiku is one click, one command, one key chord or one boot option away, and Lumina falls back to it automatically if it crashes repeatedly.

## Documents

| # | Document |
|---|---|
| 01 | [Technical feasibility report](docs/01-feasibility-report.md) |
| 02 | [Lumina source audit](docs/02-lumina-source-audit.md) |
| 03 | [Proposed architecture](docs/03-architecture.md) |
| 04 | [Dependency graph](docs/04-dependency-graph.md) |
| 05 | [Phased roadmap](docs/05-roadmap.md) |
| 06 | [Recommended proof of concept](docs/06-proof-of-concept.md) |

## License

To be decided. Lumina is BSD-3-Clause and Haiku is MIT; both are compatible with a permissive license for this project.
