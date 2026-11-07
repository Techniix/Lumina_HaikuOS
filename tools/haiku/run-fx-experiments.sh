#!/bin/sh
# Runs the four visual-effects prototypes and collects their JSON reports.
# Leave the machine alone while it runs (~1 minute): windows will appear,
# move, minimize and the workspace will switch. Run inside Haiku.
set -e
cd "$(dirname "$0")/../.."
B=build/experiments
for p in fx-alpha-panel fx-capture-glass fx-window-anim fx-snapshot-proxy; do
	echo "=== $p"
	"$B/$p" || echo "($p exited with $?)"
	sleep 1
done
OUT="$HOME/Desktop/lumina-fx-reports-$(date +%Y%m%d-%H%M%S).zip"
cd /boot/home/config/var/log/Lumina && zip -q -r "$OUT" fx-reports
echo "Reports: $OUT  (attach this to an issue / send it back)"
