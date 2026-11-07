#!/bin/sh
# Build Lumina on Haiku (run inside Haiku).
#
#   tools/haiku/build.sh [--engine] [--release]
#
# --engine   also build the Window Engine; needs a Haiku source tree that
#            matches the running system. It is cloned on first use into
#            ~/develop/haiku at the tag of your hrev (shallow, sparse).
set -e
cd "$(dirname "$0")/../.."
ROOT="$PWD"
BUILD="$ROOT/build"
ENGINE=OFF
TYPE=RelWithDebInfo
for arg in "$@"; do
	case "$arg" in
		--engine) ENGINE=ON ;;
		--release) TYPE=Release ;;
	esac
done

for tool in cmake ninja gcc rc xres mimeset; do
	command -v $tool >/dev/null 2>&1 || {
		echo "missing '$tool': pkgman install cmd:$tool"; exit 1; }
done

EXTRA=""
if [ "$ENGINE" = ON ]; then
	HREV="$(uname -v | cut -d' ' -f1)"
	SRC="$HOME/develop/haiku"
	if [ ! -f "$SRC/src/servers/app/Desktop.h" ]; then
		echo "Fetching Haiku sources for $HREV (headers + app_server only)…"
		mkdir -p "$HOME/develop"
		git clone --filter=blob:none --no-checkout --depth 1 --branch "$HREV" \
			https://github.com/haiku/haiku "$SRC"
		git -C "$SRC" sparse-checkout set --no-cone /headers/ /src/servers/app/
		git -C "$SRC" checkout -q
	fi
	EXTRA="-DLUMINA_BUILD_ENGINE=ON -DLUMINA_HAIKU_SOURCE_DIR=$SRC -DLUMINA_HAIKU_ABI=$HREV"
else
	EXTRA="-DLUMINA_BUILD_ENGINE=OFF"
fi

cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=$TYPE \
	-DCMAKE_INSTALL_PREFIX=/boot/home/config/non-packaged $EXTRA
cmake --build "$BUILD"
(cd "$BUILD" && ctest --output-on-failure)
echo
echo "Built. Install for development with: tools/haiku/install-dev.sh"
