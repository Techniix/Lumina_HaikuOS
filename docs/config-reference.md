# Configuration reference

Generated from the built-in schema (`src/core/lumina/ConfigSchemaData.cpp`) by `tools/schema_doc`. Every default reproduces stock Haiku: nothing changes until `session.mode` is `lumina` and a personality enables it.

Paths are dotted; `*` is an array index. Settings marked *try-mode* are applied with an automatic revert unless confirmed.


## Session

| Setting | Type | Default | Description |
|---|---|---|---|
| `session.mode` | enum: `stock` \| `lumina` | `"stock"` | stock = untouched Haiku; lumina = Lumina shell active. *(try-mode)* |
| `session.personality` | string | `"classic"` | Profile (personality) applied in lumina mode. |
| `session.watchdog.maxCrashes` | int (1–20) | `3` | Crashes within the window that trigger fallback to stock. |
| `session.watchdog.windowSeconds` | int (5–3600) | `60` |  |

## Deskbar

| Setting | Type | Default | Description |
|---|---|---|---|
| `deskbar.mode` | enum: `keep` \| `autohide` \| `hidden` | `"keep"` | hidden stops Deskbar via launch_daemon; restored on exit/crash. *(try-mode)* |

## Windows

| Setting | Type | Default | Description |
|---|---|---|---|
| `windows.engine.enabled` | bool | `true` | Select the Lumina decorator add-on (looks identical to Default). |
| `windows.engine.replaceDecorator` | bool | `false` | Allow the engine to replace a non-Default decorator (e.g. Flat). Off: your decorator is kept and engine features are unavailable. *(try-mode)* |
| `windows.decorator.style` | enum: `haiku` | `"haiku"` | Only 'haiku' (pixel-identical to Default) exists so far; parametric styles are on the roadmap. *(try-mode)* |
| `windows.snap.enabled` | bool | `false` |  |
| `windows.snap.edgeThreshold` | int (0–64) | `2` |  |
| `windows.snap.layout` | enum: `halves` \| `quarters` \| `halves+quarters` | `"halves+quarters"` |  |

## Windows/Animation

| Setting | Type | Default | Description |
|---|---|---|---|
| `windows.animation.enabled` | bool | `false` |  |
| `windows.animation.durationMs` | int (0–2000) | `180` |  |
| `windows.animation.curve` | string | `"ease-out-cubic"` | Easing name, cubic-bezier(x1,y1,x2,y2) or spring. |
| `windows.animation.spring.stiffness` | number (10–5000) | `420` |  |
| `windows.animation.spring.damping` | number (0.05–2) | `0.86` | Damping ratio: 1 = critically damped, <1 overshoots. |
| `windows.animation.resizeMode` | enum: `live` \| `throttled` \| `final` | `"throttled"` | How real windows are resized during geometry animations. |
| `windows.animation.maxFps` | int (10–240) | `60` |  |

## Windows/Rules

| Setting | Type | Default | Description |
|---|---|---|---|
| `windows.rules` | array | `[]` |  |

## Effects

| Setting | Type | Default | Description |
|---|---|---|---|
| `fx.quality` | enum: `off` \| `low` \| `medium` \| `high` \| `auto` | `"off"` |  |
| `fx.frameBudgetMs` | number (0.5–33) | `4` | CPU time per frame the effects engine may spend before degrading. |

## Effects/Glass

| Setting | Type | Default | Description |
|---|---|---|---|
| `fx.panelGlass.mode` | enum: `solid` \| `wallpaper` \| `edge` \| `capture` | `"solid"` | Panel background strategy (see docs/vfx.md). |
| `fx.panelGlass.blurRadius` | int (0–64) | `12` |  |
| `fx.panelGlass.tint` | color | `"#dcdcdc"` |  |
| `fx.panelGlass.tintAmount` | number (0–1) | `0.55` |  |
| `fx.panelGlass.saturation` | number (0–3) | `1.2` |  |
| `fx.panelGlass.refreshHz` | number (0–60) | `8` |  |

## Effects/Transitions

| Setting | Type | Default | Description |
|---|---|---|---|
| `fx.proxies.enabled` | bool | `false` |  |
| `fx.proxies.minimize` | bool | `true` |  |
| `fx.proxies.open` | bool | `false` |  |
| `fx.proxies.workspace` | enum: `none` \| `slide` \| `zoom` | `"none"` |  |
| `fx.proxies.durationMs` | int (0–2000) | `220` |  |
| `fx.proxies.restore` | bool | `true` | Restore from the panel with the cached window image. |

## Panels

| Setting | Type | Default | Description |
|---|---|---|---|
| `panels` | array | `[]` |  |
| `panels.*.edge` | enum: `top` \| `bottom` \| `left` \| `right` | — |  |
| `panels.*.screen` | int (0–16) | — |  |
| `panels.*.thickness` | int (16–256) | — |  |
| `panels.*.lengthPercent` | number (10–100) | — |  |
| `panels.*.align` | enum: `start` \| `center` \| `end` | — |  |
| `panels.*.autoHide` | bool | — |  |
| `panels.*.reserveSpace` | bool | — |  |
| `panels.*.applets` | array | — |  |
| `panels.*.style` | enum: `system` \| `glass` \| `custom` | — |  |
| `panels.*.color` | color | — |  |
| `panels.*.spacing` | int (0–32) | — |  |
| `panels.*.clockFormat` | string | — |  |

## Advanced

| Setting | Type | Default | Description |
|---|---|---|---|
| `logging.level` | enum: `trace` \| `debug` \| `info` \| `warning` \| `error` | `"info"` |  |
| `logging.file` | bool | `true` |  |
