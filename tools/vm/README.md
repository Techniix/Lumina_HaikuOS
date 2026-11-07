# Testing Lumina in a Haiku VM

Lumina has to be verified on Haiku itself. Everything that can be checked
off-Haiku is (unit tests, and compile-checking all Haiku code against real
Haiku headers); everything else needs a real Haiku session.

## One-time VM setup (VirtualBox or QEMU)

1. Get a Haiku nightly or R1/beta6 x86_64 image from haiku-os.org.
2. VirtualBox: 4 GB RAM, 2+ CPUs, VMSVGA graphics, SATA disk (beta6 has
   issues with some virtio disks). QEMU: `tools/vm/qemu-run.sh haiku.img`.
3. Inside Haiku:
   ```sh
   pkgman install -y cmd:cmake cmd:ninja cmd:git haiku_devel
   ssh-keygen -A && /bin/sshd     # optional: for remote runs below
   git clone https://github.com/Techniix/Lumina_HaikuOS
   cd Lumina_HaikuOS && tools/haiku/build.sh --engine
   ```
4. Take a VM snapshot **before** `install-dev.sh` so every test run starts
   from a pristine stock Haiku.

## Remote runs from a development host

With sshd running in the VM and port 2222 forwarded to its port 22:

```sh
tools/vm/haiku-remote.sh user@localhost:2222 test   # build + unit tests
tools/vm/haiku-remote.sh user@localhost:2222 fx     # run the fx prototypes
```

## Manual acceptance checklist (PoC-1)

| # | Check |
|---|---|
| S1 | `lumina mode lumina classic`: windows look identical (compare screenshots before/after) |
| S2 | `lumina windows` matches Deskbar's window list (Terminal, Tracker, WebPositive, a Qt app) |
| S3 | `lumina mode lumina modern`: dragging a window to the left/right/top edge snaps with spring motion |
| S4 | `lumina mode stock` ×20: `lumina snapshot` output identical to the first run |
| S5 | `kill -9` lumina_server 3× within 60 s (relaunching each time): stock restored, notification shown |
| S6 | Safe Mode boot → stock desktop; Ctrl+Alt+Shift+Backspace restores stock |
| S7 | Engine built for another hrev: `lumina engine` reports INACTIVE with a reason, desktop unaffected |
| S8 | `tools/haiku/run-fx-experiments.sh` completes and produces reports |
