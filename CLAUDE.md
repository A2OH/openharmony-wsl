# Claude Code Agent Instructions

## Project Overview

This repo (`A2OH/openharmony-wsl`) contains build scripts, kernel patches, and tools for running OpenHarmony on WSL2 with QEMU ARM32 emulation, including VNC display support.

Related repos:
- `A2OH/westlake` — Android shim layer, Dalvik port, test apps
- `A2OH/dalvik-universal` — KitKat Dalvik VM 64-bit port source

## Key Skills

### Building OHOS
```bash
# Source: /home/dspfac/openharmony
export SOURCE_ROOT_DIR=/home/dspfac/openharmony
export PATH=${SOURCE_ROOT_DIR}/prebuilts/python/linux-x86/current/bin:${SOURCE_ROOT_DIR}/prebuilts/build-tools/linux-x86/bin:/home/dspfac/miniconda3/bin:$PATH
python3 ${SOURCE_ROOT_DIR}/build/hb/main.py build --product-name qemu-arm-linux-min --no-prebuilt-sdk
```

### Building the Kernel
```bash
OH=/home/dspfac/openharmony
KERNEL=$OH/kernel/linux/linux-5.10
CLANG=$OH/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang
GCC_CROSS=$OH/prebuilts/gcc/linux-x86/arm/gcc-linaro-7.5.0-arm-linux-gnueabi/bin/arm-linux-gnueabi-
export PATH=$OH/prebuilts/clang/ohos/linux-x86_64/llvm/bin:/tmp:$PATH
# Need fake ccache: ln -sf /usr/bin/env /tmp/ccache

cd $KERNEL
make ARCH=arm CC="$CLANG" CROSS_COMPILE="$GCC_CROSS" ohos_defconfig
# Disable GCC plugins:
sed -i 's/CONFIG_GCC_PLUGINS=y/# CONFIG_GCC_PLUGINS is not set/' .config
make ARCH=arm CC="$CLANG" CROSS_COMPILE="$GCC_CROSS" -j$(nproc) zImage
cp arch/arm/boot/zImage $OH/out/qemu-arm-linux/packages/phone/images/zImage-dtb
```

### Building QEMU from Source (with virtio-gpu)
```bash
cd /tmp/qemu-8.2.2/build
../configure --target-list=arm-softmmu --enable-vnc --disable-sdl --disable-gtk --disable-opengl --disable-virglrenderer --disable-spice --disable-docs --disable-guest-agent --disable-tools --disable-capstone
ninja qemu-system-arm
```

### Booting QEMU with VNC
```bash
QEMU=/tmp/qemu-8.2.2/build/qemu-system-arm
OH=/home/dspfac/openharmony
IMAGES=$OH/out/qemu-arm-linux/packages/phone/images

# Headless (serial console)
$OH/tools/qemu-extracted/usr/bin/qemu-system-arm -M virt -cpu cortex-a7 -smp 4 -m 1024 -nographic ...

# With VNC display (source-built QEMU + virtio-gpu)
$QEMU -M virt -cpu cortex-a7 -smp 1 -m 1024 -display vnc=:0 -device virtio-gpu-device ...
# Connect: vncviewer localhost:5900
```

### Building Dalvik VM (ARM32)
```bash
cd /home/dspfac/android-to-openharmony-migration/dalvik-port
make TARGET=ohos-arm32 -j$(nproc)
# Output: build-ohos-arm32/dalvikvm (7.2MB static ARM32)
```

### Building ArkUI Headless (ARM32)
```bash
# Requires patched libc (libc_static_fixed.a)
cmake -DCMAKE_TOOLCHAIN_FILE=/tmp/arm32-ohos-toolchain.cmake -S $OH/arkui_test_standalone -B /tmp/arkui-arm32-build
make -C /tmp/arkui-arm32-build -j$(nproc) button_test_ng
```

### Running APKs on QEMU
```bash
# On QEMU serial console:
cd /data/a2oh
ANDROID_DATA=/data/a2oh ANDROID_ROOT=/data/a2oh \
  ./dalvikvm -Xverify:none -Xdexopt:none \
  -Xbootclasspath:/data/a2oh/core.jar:/data/a2oh/app.dex \
  -classpath /data/a2oh/app.dex ClassName
```

### Injecting Files to QEMU Images
```bash
# Via debugfs (offline)
debugfs -w -R "write /path/to/local a2oh/filename" userdata.img

# Via base64 over serial (online, small files only)
base64 file | while read line; do echo "echo '$line' >> /data/file.b64"; done
base64 -d /data/file.b64 > /data/file
```

## Important Paths

| Path | Description |
|------|-------------|
| `/home/dspfac/openharmony` | OHOS source tree |
| `/home/dspfac/android-to-openharmony-migration` | Westlake repo (dalvik-port, shim, test-apps) |
| `/home/dspfac/dalvik-kitkat` | Dalvik VM source |
| `/home/dspfac/openharmony-wsl` | This repo |
| `/tmp/qemu-8.2.2/build/qemu-system-arm` | Source-built QEMU with virtio-gpu |
| `ohos-sysroot-arm32/usr/lib/libc_static_fixed.a` | Patched musl libc (dynlink.o fix) |

## Key Patches Applied

1. **Kernel: virtio-gpu ARM32 fix** (`patches/07-virtio-gpu-arm32-fix.diff`)
   - NULL check in `virtio_gpu_release`
   - Skip `VIRTIO_F_VERSION_1` check

2. **musl libc: dynlink.o TLS fix** (in `libc_static_fixed.a`)
   - Rename `__init_tls` and `__libc_start_init` in dynlink.o
   - Fixes static ARM32 binaries that link ArkUI

3. **ArkUI: overlay_manager lazy init**
   - `RefPtr<Curve>` globals converted to function-local statics
   - Prevents crash in ARM32 static constructors

4. **Dalvik: Inflater heap fix**
   - Store original malloc'd pointer in `z_stream.opaque`
   - Fixes SIGILL when loading compressed APK entries

## Common Issues

- **OHOS static binary crashes as PID 1**: Use GCC cross-compiler instead of OHOS Clang (preinit_stubs issue)
- **virtio-gpu kernel oops**: Apply kernel patches, use `-smp 1`
- **No /dev/fb0**: Need `CONFIG_DRM_VIRTIO_GPU=y` + `CONFIG_DRM_FBDEV_EMULATION=y` in kernel
- **dalvikvm "Non-absolute bootclasspath"**: Use full paths `/data/a2oh/core.jar`
- **DEX class not found**: Must be on both `-Xbootclasspath` and `-classpath`
- **Lambdas in shim code**: Use `dx` (not `d8`) with `--no-optimize` for DEX 035
