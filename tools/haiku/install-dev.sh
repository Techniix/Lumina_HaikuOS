#!/bin/sh
# Install a development build into ~/config/non-packaged (no package needed,
# removable with tools/haiku/uninstall-dev.sh). Run inside Haiku.
set -e
cd "$(dirname "$0")/../.."
cmake --install build
NP=/boot/home/config/non-packaged
for f in "$NP/apps/Lumina/lumina_server" "$NP/apps/Lumina/LuminaPanel" \
		"$NP/bin/lumina" "$NP/preferences/Lumina Mode"; do
	[ -e "$f" ] && mimeset -f "$f"
done
echo "Installed to $NP."
echo "Nothing changed yet: Lumina is OFF until you enable it."
echo "  lumina status                  # check the installation"
echo "  lumina mode lumina classic     # enable (stock appearance)"
echo "  lumina mode stock              # back to stock Haiku"
echo "  Ctrl+Alt+Shift+Backspace       # emergency restore (after input_server restart)"
if [ -e "$NP/add-ons/decorators/LuminaEngine" ]; then
	echo "Window Engine installed; select it via 'lumina mode lumina' (or"
	echo "Appearance → Decorator → LuminaEngine)."
fi
