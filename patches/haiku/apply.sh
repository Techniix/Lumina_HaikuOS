#!/bin/sh
# Applies (or checks / reverts) Lumina's optional Haiku patch set to a Haiku
# source tree. Level 3 only: nothing in Lumina requires these patches; the
# Window Engine detects the patched app_server at run time.
#
#   patches/haiku/apply.sh <haiku-source-dir> [--check | --revert]
#
# Before applying, verifies that the overlay compose core embedded in the
# patch is byte-identical to Lumina's unit-tested copy
# (src/core/lumina/fx/OverlayCompose.{h,cpp}).
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
tree=$1
mode=${2:---apply}
[ -n "$tree" ] && [ -d "$tree/src/servers/app" ] || {
	echo "usage: $0 <haiku-source-dir> [--check|--revert]" >&2; exit 2; }

patches=$(ls "$here"/[0-9][0-9][0-9][0-9]-*.patch)

# The embedded compose core must match the tested one.
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for f in OverlayCompose.h OverlayCompose.cpp; do
	# Extract the added file from the patch (lines after its header).
	awk -v file="src/servers/app/drawing/$f" '
		$0 ~ "^diff --git a/" file " " { on = 1; next }
		on && /^diff --git / { on = 0 }
		on && /^\+/ && !/^\+\+\+ / { print substr($0, 2) }
	' $patches > "$tmp/$f"
	if ! cmp -s "$tmp/$f" "$repo/src/core/lumina/fx/$f"; then
		echo "error: $f in the patch differs from src/core/lumina/fx/$f;" >&2
		echo "       regenerate the patch (see patches/haiku/README.md)." >&2
		exit 1
	fi
done

cd "$tree"
case "$mode" in
	--check)
		for p in $patches; do git apply --check "$p"; echo "ok: $(basename "$p")"; done ;;
	--revert)
		for p in $(echo $patches | tr ' ' '\n' | sort -r); do
			git apply -R "$p"; echo "reverted: $(basename "$p")"; done ;;
	--apply)
		for p in $patches; do git apply "$p"; echo "applied: $(basename "$p")"; done
		echo "Build app_server with: jam -q app_server   (see README.md)" ;;
	*)
		echo "unknown mode $mode" >&2; exit 2 ;;
esac
