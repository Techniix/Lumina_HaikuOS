# Optional Haiku patch set (Level 3)

Lumina never requires these patches. Levels 1 and 2 run on an unmodified
Haiku. The Window Engine looks for the patched functionality at run time
(`get_image_symbol(…, "lumina_overlay_get_api")`) and reports
`compositor: false` when it is absent.

| Patch | Purpose | State |
|---|---|---|
| `0001-app_server-optional-overlay-compositor.patch` | Overlay windows: real translucency, live backdrop blur, soft shadows, antialiased corners and true opacity fades for Lumina-owned surfaces | Applies cleanly to the Haiku revision in use; every touched translation unit type-checks with clang. **Not yet built with jam or run.** |

Design and source trace: `docs/07-advanced-integration-research.md` §A, and
`docs/09-system-integration-architecture.md`.

## What it changes

About 100 lines in existing app_server files (`Desktop.cpp/.h`, `Window.h`,
`drawing/HWInterface.cpp/.h`, `drawing/Jamfile`), plus new files in
`src/servers/app/drawing/`:

- `OverlayCompositor.{h,cpp}`: the layer list. It keeps a composed buffer
  that is allocated only while at least one overlay exists, and exports the
  C API.
- `OverlayCompose.{h,cpp}`: the pixel core. It is **byte-identical** to
  `src/core/lumina/fx/OverlayCompose.*`, which is unit-tested in this
  repository (`tests/core/TestOverlay.cpp`). `apply.sh` refuses to run if
  the two differ.
- `LuminaOverlayApi.h`: the versioned C interface. An identical copy is in
  `src/engine/`.

With no overlay registered, the only cost is a boolean test per back-to-front
copy and per clipping rebuild.

## Usage

```sh
patches/haiku/apply.sh <haiku-source-dir> --check    # dry run
patches/haiku/apply.sh <haiku-source-dir>            # apply
patches/haiku/apply.sh <haiku-source-dir> --revert   # undo
```

Then build, in a Haiku build environment:

```sh
cd <haiku-source-dir>/generated.x86_64
jam -q app_server            # or: jam -q @nightly-anyboot  (full image)
```

## Installing a patched app_server

**Recommended: a VM with a full image.** Build `@nightly-anyboot` with the
patch, boot it in a virtual machine, take a snapshot, then install Lumina
and run `experiments/fx-overlay-compositor`. Reverting means discarding the
image.

**Candidate for real hardware (unverified).** Place the binary in the
non-packaged hierarchy and override the app_server service definition from a
launch_daemon *settings* directory, so the packaged system stays untouched
and deleting one file reverts it. Whether launch_daemon lets a settings
file replace a packaged system service's `launch` path has **not been
verified** against the launch_daemon source. Do not try this on a machine
you can't recover. Haiku's boot menu (safe mode, previous package states)
does not cover non-packaged files.

## Regenerating the patch

The patches are produced from a Haiku checkout:

```sh
# after editing the files in <haiku-source-dir>
cp src/core/lumina/fx/OverlayCompose.{h,cpp} <tree>/src/servers/app/drawing/
cd <tree> && git add -N src/servers/app/drawing/{OverlayCompose,OverlayCompositor}.* \
	src/servers/app/drawing/LuminaOverlayApi.h
git diff > <lumina>/patches/haiku/0001-app_server-optional-overlay-compositor.patch
```

## Upstream

If the patch is ever proposed upstream, it goes through Haiku's Gerrit code
review after it has been built and tested on hardware, not as a ticket
attachment. Two small, independent upstream fixes found during this work
(both in `src/servers/app/CursorSet.cpp`) would be easier first
contributions:

1. `CursorSet::AddCursor()` writes `"class": "bitmap"`, but
   `CursorSet::FindCursor()` only accepts `"cursor"`.
2. `_CursorWhichToString()` has no case for `B_CURSOR_ID_CREATE_LINK`.
