#!/bin/sh
# Restores stock Haiku and removes a development install.
NP=/boot/home/config/non-packaged
[ -x "$NP/bin/lumina" ] && "$NP/bin/lumina" restore-stock
rm -rf "$NP/apps/Lumina" "$NP/bin/lumina" "$NP/preferences/Lumina Mode" \
	"$NP/lib/liblumina.so"* "$NP/add-ons/decorators/LuminaEngine" \
	"$NP/add-ons/input_server/filters/LuminaInput" \
	"$NP/data/Lumina" "$NP/data/user_launch/lumina"
echo "Lumina removed. Your settings remain in ~/config/settings/Lumina (delete"
echo "that folder to forget them)."
