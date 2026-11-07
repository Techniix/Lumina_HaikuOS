# Vendored Haiku private headers

These declarations come verbatim (or as a marked subset) from the Haiku
source tree (`headers/private/...`, MIT License, Copyright Haiku, Inc.). They
describe **semi-private but long-stable** libbe/driver interfaces that
Deskbar, Tracker and the preflets use themselves:

| Header | Used for | Source |
|---|---|---|
| `WindowInfo.h` | window list, minimize/raise other apps' windows | `headers/private/interface` |
| `LaunchRoster.h` | stopping/starting Deskbar through launch_daemon | `headers/private/app` |
| `DecoratorPrivate.h` | selecting the Lumina decorator / restoring the stock one | `headers/private/interface` |
| `InterfacePrivateSubset.h` | control look, workspace layout | `headers/private/interface/InterfacePrivate.h` |
| `power_managment.h` | ACPI battery ioctls (as PowerStatus) | `headers/private/device` |

Only `src/liblumina/lumina/haiku/PrivateApi.*` includes them. Everything
else talks to the `lumina::haiku::PrivateApi` wrapper, which is the single
place to adapt if Haiku changes these interfaces. Each wrapper function has
a public-API fallback or reports "unsupported" instead of crashing.
