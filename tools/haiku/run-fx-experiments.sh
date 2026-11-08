#!/bin/sh
# Runs the visual-effects prototypes and collects their JSON reports.
# Leave the machine alone while it runs (~2 minutes): windows will appear,
# move, minimize and the workspace will switch; experiment F asks for one
# click. Run inside Haiku.
#
# E (lifecycle) registers its own proxy pool: it quits lumina_server first
# if it is running and does not restart it. F reports "not available" on a
# stock app_server (expected) and only does real work with the Level 3
# patch.
set -e
cd "$(dirname "$0")/../.."
B=build/experiments
for p in fx-alpha-panel fx-capture-glass fx-window-anim fx-snapshot-proxy \
		fx-lifecycle-engine fx-overlay-compositor; do
	if [ "$p" = fx-lifecycle-engine ]; then
		hey -s application/x-vnd.Lumina-Server quit >/dev/null 2>&1 || true
		sleep 1
	fi
	echo "=== $p"
	"$B/$p" || echo "($p exited with $?)"
	sleep 1
done
OUT="$HOME/Desktop/lumina-fx-reports-$(date +%Y%m%d-%H%M%S).zip"
cd /boot/home/config/var/log/Lumina && zip -q -r "$OUT" fx-reports
echo "Reports: $OUT  (attach this to an issue / send it back)"
