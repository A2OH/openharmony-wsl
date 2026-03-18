# QEMU Setup and Options

## Overview

OpenHarmony runs on QEMU ARM32 (`qemu-system-arm`) emulating a virt machine
with Cortex-A7 CPU. The system boots a Linux 5.10.184 kernel with OHOS
userspace (init, samgr, foundation, and system services).

## Architecture

```
Host (WSL2 x86_64)
├── qemu-system-arm
│   ├── Machine: virt
│   ├── CPU: cortex-a7 x4
│   ├── RAM: 1024 MB
│   ├── Drives (virtio-blk):
│   │   ├── vda = updater.img
│   │   ├── vdb = system.img    → mounted at /usr
│   │   ├── vdc = vendor.img    → mounted at /vendor
│   │   └── vdd = userdata.img  → mounted at /data
│   ├── Kernel: zImage-dtb
│   └── Initrd: ramdisk.img
└── Serial: ttyAMA0 (console)
```

## Boot Modes

### Headless (Serial Console)

Best for development and testing. No GUI required.

```bash
OHOS_ROOT=~/openharmony ./scripts/qemu_boot.sh 0
```

Exit with: `Ctrl+A` then `X`

### VNC Display

For testing graphics/UI components. Requires a VNC viewer.

```bash
OHOS_ROOT=~/openharmony ./scripts/qemu_boot_vnc.sh 0
```

Connect from Windows: `vncviewer localhost:5900`

For WSL2, you may need to find the WSL IP:
```bash
hostname -I
# Then connect from Windows: vncviewer <WSL_IP>:5900
```

### With Networking

To enable networking in QEMU (for downloading packages, testing connectivity):

```bash
OHOS_ROOT=~/openharmony qemu-system-arm \
  -M virt -cpu cortex-a7 -smp 4 -m 1024 -nographic \
  -netdev user,id=net0,hostfwd=tcp::5555-:5555 \
  -device virtio-net-device,netdev=net0 \
  -drive if=none,file=$OHOS_ROOT/out/qemu-arm-linux/packages/phone/images/userdata.img,format=raw,id=userdata -device virtio-blk-device,drive=userdata \
  -drive if=none,file=$OHOS_ROOT/out/qemu-arm-linux/packages/phone/images/vendor.img,format=raw,id=vendor -device virtio-blk-device,drive=vendor \
  -drive if=none,file=$OHOS_ROOT/out/qemu-arm-linux/packages/phone/images/system.img,format=raw,id=system -device virtio-blk-device,drive=system \
  -drive if=none,file=$OHOS_ROOT/out/qemu-arm-linux/packages/phone/images/updater.img,format=raw,id=updater -device virtio-blk-device,drive=updater \
  -kernel $OHOS_ROOT/out/qemu-arm-linux/packages/phone/images/zImage-dtb \
  -initrd $OHOS_ROOT/out/qemu-arm-linux/packages/phone/images/ramdisk.img \
  -append "console=ttyAMA0,115200 init=/bin/init hardware=qemu.arm.linux default_boot_device=a003e00.virtio_mmio root=/dev/ram0 rw ohos.required_mount.system=/dev/block/vdb@/usr@ext4@ro,barrier=1@wait,required ohos.required_mount.vendor=/dev/block/vdc@/vendor@ext4@ro,barrier=1@wait,required ohos.required_mount.data=/dev/block/vdd@/data@ext4@nosuid,nodev,noatime,barrier=1@wait,required"
```

Port 5555 on the host is forwarded to 5555 in the guest (hdc default port).

### With GDB Debugging

To debug the kernel or userspace with GDB:

```bash
# Add -s -S to QEMU flags (pauses at boot, waits for GDB on port 1234)
qemu-system-arm ... -s -S

# In another terminal:
gdb-multiarch
(gdb) target remote :1234
(gdb) continue
```

## Drive Order

QEMU virtio-mmio enumerates drives in **reverse** command-line order. The
boot command line specifies:

| CLI Order | Drive ID | Block Device | Mount Point |
|-----------|----------|-------------|-------------|
| 1st | userdata | vdd | /data |
| 2nd | vendor | vdc | /vendor |
| 3rd | system | vdb | /usr |
| 4th | updater | vda | (not mounted) |

The kernel command line maps these via `ohos.required_mount.*` parameters.
Changing the drive order in the QEMU command will break mounting.

## Image Sizes

| Image | Default Size | Contents |
|-------|-------------|----------|
| system.img | 100 MB (25600 x 4K blocks) | System binaries, libraries, configs |
| userdata.img | 100 MB | User data, app data |
| vendor.img | 40 MB (10240 x 4K blocks) | Vendor configs, fstab |
| ramdisk.img | ~5 MB | Initial rootfs with init |
| zImage-dtb | ~8 MB | Linux kernel + device tree |
| updater.img | varies | OTA updater (required by init) |

To increase image sizes, edit `prepare_images.sh` and change the block count
parameter passed to `mke2fs`.

## File Injection

To add or modify files in images without rebuilding:

```bash
# Add a file
./scripts/inject_files.sh system.img ./my_binary /bin/my_binary

# Or use debugfs directly
debugfs -w system.img -R "write local_file /path/in/image"

# List files in an image
debugfs system.img -R "ls /bin" 2>/dev/null

# Extract a file from an image
debugfs system.img -R "dump /bin/init /tmp/init_extracted"
```

## Performance Tips

1. **Use tmpfs for images:** Mount a tmpfs and copy images there for faster I/O:
   ```bash
   sudo mount -t tmpfs -o size=512M tmpfs /tmp/qemu-imgs
   cp out/qemu-arm-linux/packages/phone/images/*.img /tmp/qemu-imgs/
   ```

2. **Increase SMP:** Change `-smp 4` to `-smp 8` if your host has 8+ cores.

3. **Increase RAM:** Change `-m 1024` to `-m 2048` for more memory-intensive
   workloads.

4. **KVM acceleration:** Not available for ARM on x86_64 host. QEMU uses TCG
   (software emulation), which is slower but works everywhere.

## Boot Sequence

1. QEMU loads `zImage-dtb` and `ramdisk.img`
2. Linux kernel boots, mounts ramdisk as rootfs
3. Kernel parses `ohos.required_mount.*` parameters
4. `/bin/init` starts (OpenHarmony init, not systemd)
5. init reads `/etc/init/*.cfg` to start services
6. `samgr` (service manager) starts first
7. `foundation` loads system abilities (SA 180, 401, 501, etc.)
8. `hilogd`, `softbus_server`, `accesstoken_service` start
9. Console becomes available on serial port
10. System is ready (~30-60 seconds after boot)
