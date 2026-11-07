# Development guide

## Source layout

```
src/core/        lumina_core — portable C++17, no Haiku API (config, profiles,
                 rules, logging, animation, effects policy & pixel ops)
src/liblumina/   liblumina.so — Haiku IPC, private-API compat layer
                 (compat/haiku_private), paths, mode control, snapshot,
                 OS interface, visual-effects backends
src/engine/      LuminaEngine — app_server decorator add-on (Window Engine)
src/server/      lumina_server — session, config, modes, watchdog, effects
src/panel/       LuminaPanel — native panels and applets
src/mode/        Lumina Mode — stock/Lumina switch (preflet + Deskbar item)
src/cli/         lumina — command-line control and diagnostics
src/input/       LuminaInput — input_server filter (emergency chord)
experiments/     visual-effects research prototypes (never installed)
data/            personalities, launch_daemon file
tests/           unit tests (run on any host and on Haiku)
tools/           compile-check sysroot, Haiku build/install scripts, VM tools
```

Rules of thumb: anything that can be portable goes to `lumina_core` and gets
a unit test; anything that touches a semi-private Haiku API goes through
`lumina/haiku/PrivateApi.*`; anything experimental stays in `experiments/`.

## Building and testing

### On any host (unit tests)

```sh
cmake -S . -B build-host -G Ninja -DLUMINA_WERROR=ON
cmake --build build-host && (cd build-host && ctest --output-on-failure)
```

### Compile-checking the Haiku code from Linux

```sh
tools/setup-haiku-check-sysroot.sh ~/haiku-check      # once
cmake -S . -B build-check -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/HaikuCompileCheck.toolchain.cmake \
  -DHAIKU_CHECK_ROOT=$HOME/haiku-check -DLUMINA_WERROR=ON
cmake --build build-check
```

This compiles every Haiku target (including the app_server add-on) with
clang's Haiku target against real Haiku headers and libstdc++ configured
for Haiku. It produces objects only — catches API and type errors, proves
nothing about runtime.

### On Haiku

```sh
tools/haiku/build.sh --engine     # build + unit tests (fetches app_server sources)
tools/haiku/install-dev.sh        # into ~/config/non-packaged; Lumina stays OFF
lumina status
lumina mode lumina classic        # enable; `lumina mode stock` to go back
tools/haiku/uninstall-dev.sh      # restore stock and remove
```

See `tools/vm/README.md` for VM setup, remote runs and the acceptance
checklist.

## Debugging

* Logs: `~/config/var/log/Lumina/{server,panel}.log`; warnings and errors
  also go to syslog. `LUMINA_LOG_LEVEL=debug` for more.
* `lumina diag` — the server's recent log, counters and effective config.
* `lumina engine` — engine presence, ABI, statistics.
* `LUMINA_SETTINGS_DIR=/tmp/x lumina …` — sandbox all settings.
* The engine logs to syslog with the prefix `lumina-engine:`.

## Conventions

Haiku coding style (tabs, `fMember`, `_Private()`), MIT license headers,
no exceptions across Haiku API boundaries, no blocking calls in app_server
hooks, no Qt in native components.
