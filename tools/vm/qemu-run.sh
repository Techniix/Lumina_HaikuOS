#!/bin/sh
# Boots a Haiku disk image in QEMU with SSH forwarded to localhost:2222.
IMG="${1:?usage: qemu-run.sh haiku.img}"
ACCEL=""
[ -e /dev/kvm ] && ACCEL="-enable-kvm -cpu host"
exec qemu-system-x86_64 $ACCEL -m 4096 -smp 2 \
	-drive file="$IMG",format=raw,if=ide \
	-device virtio-net-pci,netdev=n0 \
	-netdev user,id=n0,hostfwd=tcp::2222-:22 \
	-vga std -usb -device usb-tablet
