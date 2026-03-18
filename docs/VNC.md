# VNC Display Output on OHOS QEMU

## Overview

This guide explains how to get **real pixel output** from OpenHarmony running on QEMU ARM32, viewable via VNC on Windows/Mac/Linux.

The pipeline:
```
ARM32 binary → /dev/fb0 (fbdev) → virtio-gpu DRM → QEMU VNC → Your VNC viewer
```

## Prerequisites

- OpenHarmony source tree built with `qemu-arm-linux-min` product
- WSL2 on Windows (or native Linux)
- VNC viewer (TightVNC on Windows, or Screen Sharing on macOS)

## Step 1: Build QEMU from Source

The Ubuntu QEMU package (`qemu-system-arm`) does **not** include `virtio-gpu-device` for ARM. You must build from source:

```bash
# Download
cd /tmp
wget https://download.qemu.org/qemu-8.2.2.tar.xz
tar xf qemu-8.2.2.tar.xz

# Install dependencies
# On Ubuntu/WSL2, glib is needed:
conda install -y glib pkg-config  # or: sudo apt install libglib2.0-dev

# Configure (ARM only, minimal, VNC enabled)
cd qemu-8.2.2
mkdir build && cd build
export PKG_CONFIG_PATH=$CONDA_PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH

../configure \
  --target-list=arm-softmmu \
  --disable-werror \
  --enable-vnc \
  --disable-sdl --disable-gtk \
  --disable-opengl --disable-virglrenderer \
  --disable-spice --disable-docs \
  --disable-guest-agent --disable-tools \
  --disable-capstone

# Build
ninja qemu-system-arm
# Result: build/qemu-system-arm (~25MB)
```

Verify virtio-gpu support:
```bash
./build/qemu-system-arm -M virt -cpu cortex-a7 -device help 2>&1 | grep virtio-gpu
# Should show: name "virtio-gpu-device", bus virtio-bus
```

## Step 2: Patch the OHOS Kernel

The OHOS 5.10 kernel's `virtio-gpu` driver has two bugs that cause crashes on ARM32 QEMU:

### Patch 1: NULL check in virtio_gpu_release

**File:** `kernel/linux/linux-5.10/drivers/gpu/drm/virtio/virtgpu_kms.c`

The `virtio_gpu_release` function dereferences `dev->dev_private` without checking for NULL. When `virtio_gpu_init` fails early (before allocating vgdev), this causes a NULL pointer crash.

```diff
 void virtio_gpu_release(struct drm_device *dev)
 {
 	struct virtio_gpu_device *vgdev = dev->dev_private;

+	if (!vgdev)
+		return;
+
 	virtio_gpu_modeset_fini(vgdev);
 	virtio_gpu_free_vbufs(vgdev);
```

### Patch 2: Skip VIRTIO_F_VERSION_1 check

**File:** `kernel/linux/linux-5.10/drivers/gpu/drm/virtio/virtgpu_kms.c`

QEMU's `virtio-gpu-device` on the `virtio-mmio` bus uses legacy transport by default (`force-legacy=true`), which doesn't negotiate `VIRTIO_F_VERSION_1`. The kernel driver refuses to initialize without this feature, but it works fine on ARM32 without it.

```diff
 	if (!virtio_has_feature(dev_to_virtio(dev->dev), VIRTIO_F_VERSION_1)) {
-		return -ENODEV;
+		DRM_WARN("virtio-gpu: VIRTIO_F_VERSION_1 not supported, continuing anyway\n");
 	}
```

> **Why not use `force-legacy=false`?** Because `-global virtio-mmio.force-legacy=false` affects ALL virtio-mmio devices including virtio-blk, and the OHOS kernel's virtio-blk driver doesn't support non-legacy mode.

### Rebuild the kernel

```bash
OH=/path/to/openharmony
KERNEL=$OH/kernel/linux/linux-5.10
CLANG=$OH/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang
GCC_CROSS=$OH/prebuilts/gcc/linux-x86/arm/gcc-linaro-7.5.0-arm-linux-gnueabi/bin/arm-linux-gnueabi-

# Apply patches above, then:
cd $KERNEL
export PATH=$OH/prebuilts/clang/ohos/linux-x86_64/llvm/bin:$PATH

# Create a fake ccache (OHOS build expects it)
ln -sf /usr/bin/env /tmp/ccache
export PATH=/tmp:$PATH

# Configure
cp $OH/kernel/linux/config/linux-5.10/arch/arm/configs/qemu-arm-linux_standard_defconfig \
   arch/arm/configs/ohos_defconfig
make ARCH=arm CC="$CLANG" CROSS_COMPILE="$GCC_CROSS" ohos_defconfig

# Disable GCC plugins (not needed, causes build errors)
sed -i 's/CONFIG_GCC_PLUGINS=y/# CONFIG_GCC_PLUGINS is not set/' .config
sed -i 's/CONFIG_GCC_PLUGIN_ARM_SSP_PER_TASK=y/# CONFIG_GCC_PLUGIN_ARM_SSP_PER_TASK is not set/' .config

# Verify DRM_VIRTIO_GPU is enabled
grep CONFIG_DRM_VIRTIO_GPU .config
# Should show: CONFIG_DRM_VIRTIO_GPU=y

# Build
make ARCH=arm CC="$CLANG" CROSS_COMPILE="$GCC_CROSS" -j$(nproc) zImage

# Install
cp arch/arm/boot/zImage $OH/out/qemu-arm-linux/packages/phone/images/zImage-dtb
```

## Step 3: Build the Framebuffer Test Binary

This ARM32 binary runs as PID 1 (init), mounts devtmpfs, opens `/dev/fb0`, and draws a calculator UI to the framebuffer.

**Important:** Must be compiled with **GCC** (not OHOS Clang), because OHOS musl's `preinit_stubs` causes a crash when running as PID 1.

```bash
OH=/path/to/openharmony
GCC=$OH/prebuilts/gcc/linux-x86/arm/gcc-linaro-7.5.0-arm-linux-gnueabi/bin/arm-linux-gnueabi-gcc

$GCC -static -o fb_init fb_init.c
```

**Source: `fb_init.c`**

```c
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <linux/fb.h>

static void msg(const char *s) {
    int fd = open("/dev/tty0", O_WRONLY);
    if (fd >= 0) { write(fd, s, strlen(s)); close(fd); }
}

static void fill(uint32_t *fb, int s, int x, int y, int w, int h, uint32_t c) {
    for (int r = y; r < y + h; r++)
        for (int col = x; col < x + w; col++)
            fb[r * s + col] = c;
}

int main() {
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    sleep(3);

    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) { msg("no fb0\n"); while(1) sleep(3600); }

    struct fb_var_screeninfo vi = {};
    struct fb_fix_screeninfo fi = {};
    ioctl(fd, FBIOGET_VSCREENINFO, &vi);
    ioctl(fd, FBIOGET_FSCREENINFO, &fi);

    int w = vi.xres, h = vi.yres;
    int sz = fi.line_length * h;
    uint32_t *px = mmap(NULL, sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (px == MAP_FAILED) { msg("mmap fail\n"); while(1) sleep(3600); }

    int s = fi.line_length / 4;

    // Background
    for (int i = 0; i < h * s; i++) px[i] = 0xFF303030;

    // Blue title bar
    fill(px, s, 0, 0, w, 48, 0xFF2196F3);
    // Dark display area
    fill(px, s, 10, 58, w - 20, 60, 0xFF1E1E1E);
    // "0" text (white block)
    fill(px, s, w - 60, 75, 30, 28, 0xFFFFFFFF);

    // 4x4 button grid
    int bw = (w - 50) / 4, bh = 60;
    uint32_t colors[] = {
        0xFF616161, 0xFF616161, 0xFF616161, 0xFFFF9800,  // 7 8 9 /
        0xFF616161, 0xFF616161, 0xFF616161, 0xFFFF9800,  // 4 5 6 *
        0xFF616161, 0xFF616161, 0xFF616161, 0xFFFF9800,  // 1 2 3 -
        0xFFF44336, 0xFF616161, 0xFF4CAF50, 0xFFFF9800,  // C 0 = +
    };
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            int bx = 10 + c * (bw + 10), by = 130 + r * (bh + 10);
            fill(px, s, bx, by, bw, bh, colors[r * 4 + c]);
            // White dot for label
            fill(px, s, bx + bw/2 - 5, by + bh/2 - 5, 10, 10, 0xFFFFFFFF);
        }

    msg("CALCULATOR ON VNC!\n");
    munmap(px, sz);
    close(fd);
    while(1) sleep(3600);
}
```

### Create the ramdisk

```bash
mkdir -p ramdisk/{dev,proc,sys}
cp fb_init ramdisk/init
chmod 755 ramdisk/init

cd ramdisk
find . | cpio -o -H newc | gzip > ../ramdisk_vnc.img
```

## Step 4: Boot with VNC

```bash
QEMU=/path/to/qemu-8.2.2/build/qemu-system-arm
OH=/path/to/openharmony
IMAGES=$OH/out/qemu-arm-linux/packages/phone/images

$QEMU \
  -M virt -cpu cortex-a7 -smp 1 -m 1024 \
  -display vnc=:0 \
  -device virtio-gpu-device \
  -kernel $IMAGES/zImage-dtb \
  -initrd ramdisk_vnc.img \
  -append "console=ttyAMA0 root=/dev/ram0 rw quiet loglevel=0"
```

> **Note:** Use `-smp 1` (single core). Multi-core causes kernel SMP IPI issues with virtio-gpu.

> **Note:** `console=ttyAMA0` (not `tty0`) keeps kernel messages off the framebuffer.

> **Note:** `quiet loglevel=0` suppresses remaining kernel output on the display.

## Step 5: Connect VNC

### Windows (TightVNC)
```
vncviewer localhost:5900
```

### macOS (built-in)
```
open vnc://localhost:5900
```

### Browser (noVNC)
```bash
pip install websockify
git clone https://github.com/novnc/noVNC.git
websockify --web=noVNC 6080 localhost:5900
# Open: http://localhost:6080/vnc.html
```

### From LAN (Windows host exposing to network)
In PowerShell as Admin:
```powershell
netsh interface portproxy add v4tov4 listenport=5900 listenaddress=0.0.0.0 connectport=5900 connectaddress=<WSL2_IP>
netsh advfirewall firewall add rule name="VNC" dir=in action=allow protocol=tcp localport=5900
```
Then connect from any device: `vnc://<windows_ip>:5900`

## Full Boot with OHOS System + VNC

To run the full OHOS system (with Dalvik, APKs, ArkUI) AND have VNC display:

```bash
$QEMU \
  -M virt -cpu cortex-a7 -smp 1 -m 1024 \
  -display vnc=:0 \
  -device virtio-gpu-device \
  -drive if=none,file=$IMAGES/userdata.img,format=raw,id=ud -device virtio-blk-device,drive=ud \
  -drive if=none,file=$IMAGES/vendor.img,format=raw,id=vd -device virtio-blk-device,drive=vd \
  -drive if=none,file=$IMAGES/system.img,format=raw,id=sd -device virtio-blk-device,drive=sd \
  -drive if=none,file=$IMAGES/updater.img,format=raw,id=up -device virtio-blk-device,drive=up \
  -kernel $IMAGES/zImage-dtb \
  -initrd $IMAGES/ramdisk.img \
  -append "console=ttyAMA0,115200 quiet init=/bin/init hardware=qemu.arm.linux \
    default_boot_device=a003e00.virtio_mmio root=/dev/ram0 rw \
    ohos.required_mount.system=/dev/block/vdb@/usr@ext4@ro,barrier=1@wait,required \
    ohos.required_mount.vendor=/dev/block/vdc@/vendor@ext4@ro,barrier=1@wait,required \
    ohos.required_mount.data=/dev/block/vdd@/data@ext4@nosuid,nodev,noatime,barrier=1@wait,required"
```

Apps can then write to `/dev/fb0` to render UI visible on VNC.

## What You'll See

- **Calculator test:** Blue title bar, dark display, 4x4 colored button grid (gray numbers, orange operators, red C, green =)
- **Resolution:** 1280x800 (virtio-gpu default)
- **Color depth:** 32-bit BGRA

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| Black screen | init binary crashed (OHOS musl) | Compile with GCC, not OHOS Clang |
| `create_dumb fail` | virtio-gpu VERSION_1 not negotiated | Use `/dev/fb0` instead of DRM dumb buffers |
| `mmap fail` on DRM | Same VERSION_1 issue | Use fbdev path (`/dev/fb0`) |
| `no fb0` | DRM fbdev emulation didn't activate | Ensure `CONFIG_DRM_FBDEV_EMULATION=y` in kernel |
| Kernel oops at `virtio_gpu_modeset_fini` | NULL pointer in cleanup | Apply Patch 1 (NULL check) |
| `VIRTIO_F_VERSION_1 not supported` | QEMU legacy transport | Apply Patch 2 (skip check) |
| Kernel text overlays on calculator | Console on tty0 | Use `console=ttyAMA0 quiet loglevel=0` |
| QEMU crashes with 4+ virtio drives | Too many MMIO devices | Use `-smp 1`, reduce drives if needed |
| PCI address conflict | bochs-display on ARM virt | Don't use bochs-display; use virtio-gpu-device |
| VNC not accessible from Windows | WSL2 port forwarding | `netsh interface portproxy add v4tov4 ...` |
