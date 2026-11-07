#!/bin/sh
# Push the working tree to a Haiku VM over SSH and build/test there.
#   haiku-remote.sh user@host:port test|fx|install
set -e
TARGET="${1:?usage: haiku-remote.sh user@host:port test|fx|install}"
ACTION="${2:-test}"
HOSTPART="${TARGET%:*}"
PORT="${TARGET##*:}"
[ "$PORT" = "$TARGET" ] && PORT=22
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
REMOTE=lumina-src
tar -C "$ROOT" --exclude='./build*' --exclude=.git -czf - . | \
	ssh -p "$PORT" "$HOSTPART" "rm -rf $REMOTE && mkdir $REMOTE && tar -C $REMOTE -xzf -"
case "$ACTION" in
	test)    ssh -p "$PORT" "$HOSTPART" "cd $REMOTE && tools/haiku/build.sh --engine" ;;
	fx)      ssh -p "$PORT" "$HOSTPART" "cd $REMOTE && tools/haiku/build.sh --engine && tools/haiku/run-fx-experiments.sh" ;;
	install) ssh -p "$PORT" "$HOSTPART" "cd $REMOTE && tools/haiku/build.sh --engine && tools/haiku/install-dev.sh" ;;
	*) echo "unknown action $ACTION"; exit 1 ;;
esac
