# Running Android APKs on OpenHarmony QEMU

This guide explains how to run unmodified Android APKs on OpenHarmony ARM32 QEMU,
with visual output rendered to VNC.

## Architecture

```
Android APK (.dex)
    |
    v
Dalvik VM (KitKat, ARM32 static binary)
    |
    v
Java Shim Layer (2,056 android.* classes)
    |
    v
OHBridge JNI (169 methods -> OHOS native APIs)
    |
    v
OpenHarmony ARM32 QEMU (/dev/fb0 -> VNC)
```

## Prerequisites

- OpenHarmony built and booting on QEMU (see [BUILD.md](BUILD.md))
- VNC display working (see [VNC.md](VNC.md))
- GCC ARM cross-compiler (comes with OHOS prebuilts)
- Java 8+ and dx tool (from AOSP prebuilts)

## Step 1: Clone Required Repositories

```bash
# A2OH repos (all public)
git clone https://github.com/A2OH/openharmony-wsl.git ~/openharmony-wsl
git clone https://github.com/A2OH/westlake.git ~/westlake
git clone https://github.com/A2OH/dalvik-universal.git ~/dalvik-universal
```

**Westlake** contains:
- `dalvik-port/` — Dalvik VM build system + ARM32/x86_64 targets
- `shim/java/android/` — 2,056 Android API shim classes (126,625 lines)
- `test-apps/` — Test harness (headless, UI, MockDonalds, real APK)

**dalvik-universal** contains:
- KitKat-era Dalvik VM source code, patched for 64-bit and OHOS

### Optional: AOSP Android 11 Source (for shim development)

Only needed if you want to add new AOSP framework classes to the shim layer:

```bash
mkdir -p ~/aosp && cd ~/aosp
repo init -u https://android.googlesource.com/platform/manifest -b android-11.0.0_r48 --depth=1
repo sync -c -j8 --no-tags frameworks/base
# Only ~5 GB for frameworks/base (vs ~100 GB for full AOSP)
```

## Step 2: Build Dalvik VM for ARM32

```bash
cd ~/westlake/dalvik-port

# Ensure OHOS sysroot and cross-compiler are available
# (These come from the OpenHarmony prebuilts)
export OH=~/openharmony

# Build
make TARGET=ohos-arm32 -j$(nproc)

# Output
ls -la build-ohos-arm32/dalvikvm    # ~7.2 MB static ARM32 binary
ls -la build-ohos-arm32/dexopt      # DEX optimizer
```

**Requirements for Dalvik build:**
- OHOS Clang: `$OH/prebuilts/clang/ohos/linux-x86_64/llvm/bin/clang`
- OHOS sysroot: `dalvik-port/ohos-sysroot-arm32/` (included in westlake)
- Dalvik source: `~/dalvik-universal/` (or set `DALVIK_SRC` env var)
- libffi: included in ohos-sysroot-arm32

## Step 3: Build the Shim DEX

```bash
cd ~/westlake

# Run the test suite (compiles shim + tests, runs on host JVM)
cd test-apps && ./run-local-tests.sh headless

# Build DEX for Dalvik
# Find dx.jar (from AOSP prebuilts or Android SDK)
DX_JAR=~/aosp/prebuilts/sdk/tools/lib/dx.jar
# Or: DX_JAR=~/Android/Sdk/build-tools/*/lib/dx.jar

# Compile shim classes
BUILD=/tmp/shim-build
mkdir -p $BUILD
find shim/java -name "*.java" > /tmp/sources.txt
find test-apps/mock -name "*.java" >> /tmp/sources.txt
echo "test-apps/04-mockdonalds/src/*.java" >> /tmp/sources.txt

javac -d $BUILD --release 8 \
  -sourcepath shim/java:test-apps/mock:test-apps/04-mockdonalds/src \
  $(cat /tmp/sources.txt) 2>&1

# Convert to DEX 035 (KitKat compatible)
java -jar $DX_JAR --dex --no-optimize --output=/tmp/mockdonalds.dex $BUILD
```

## Step 4: Build the VNC Init Binary

The init binary runs as PID 1 on a minimal QEMU ramdisk. It mounts filesystems,
runs Dalvik, parses View tree output, and draws to `/dev/fb0` for VNC display.

**Must be compiled with GCC** (not OHOS Clang) to avoid musl `preinit_stubs` crash.

```bash
OH=~/openharmony
GCC=$OH/prebuilts/gcc/linux-x86/arm/gcc-linaro-7.5.0-arm-linux-gnueabi/bin/arm-linux-gnueabi-gcc

# Compile the init binary
$GCC -static -o /tmp/dalvik_init ~/openharmony-wsl/scripts/dalvik_vnc_init.c
```

## Step 5: Create the Ramdisk

```bash
mkdir -p /tmp/ramdisk/{dev,proc,sys,etc,tmp}

# Init binary
cp /tmp/dalvik_init /tmp/ramdisk/init
chmod 755 /tmp/ramdisk/init

# /etc/passwd is required for Dalvik's System.initSystemProperties
echo "root:x:0:0:root:/data:/bin/sh" > /tmp/ramdisk/etc/passwd
echo "root:x:0:" > /tmp/ramdisk/etc/group

# Create ramdisk image
cd /tmp/ramdisk
find . | cpio -o -H newc | gzip > /tmp/ramdisk_dalvik.img
```

## Step 6: Deploy Files to QEMU Image

```bash
OH=~/openharmony
IMAGES=$OH/out/qemu-arm-linux/packages/phone/images

# Inject files into userdata.img via debugfs
debugfs -w -R "mkdir a2oh" $IMAGES/userdata.img
debugfs -w -R "mkdir a2oh/dalvik-cache" $IMAGES/userdata.img
debugfs -w -R "mkdir a2oh/bin" $IMAGES/userdata.img

# Deploy Dalvik VM + DEX files
debugfs -w -R "write ~/westlake/dalvik-port/build-ohos-arm32/dalvikvm a2oh/dalvikvm" $IMAGES/userdata.img
debugfs -w -R "write ~/westlake/dalvik-port/build-ohos-arm32/dexopt a2oh/dexopt" $IMAGES/userdata.img
debugfs -w -R "write ~/westlake/dalvik-port/core.jar a2oh/core.jar" $IMAGES/userdata.img
debugfs -w -R "write /tmp/mockdonalds.dex a2oh/mockdonalds.dex" $IMAGES/userdata.img

# Verify
debugfs -R "ls -l a2oh" $IMAGES/userdata.img
```

## Step 7: Boot with VNC

Requires source-built QEMU with virtio-gpu support (see [VNC.md](VNC.md)).

```bash
QEMU=/tmp/qemu-8.2.2/build/qemu-system-arm
OH=~/openharmony
IMAGES=$OH/out/qemu-arm-linux/packages/phone/images

$QEMU \
  -M virt -cpu cortex-a7 -smp 1 -m 1024 \
  -display vnc=:0 \
  -device virtio-gpu-device \
  -device virtio-tablet-device \
  -drive if=none,file=$IMAGES/userdata.img,format=raw,id=ud \
  -device virtio-blk-device,drive=ud \
  -kernel $IMAGES/zImage-dtb \
  -initrd /tmp/ramdisk_dalvik.img \
  -append "console=ttyAMA0 root=/dev/ram0 rw quiet loglevel=0" \
  -serial file:/tmp/qemu_serial.log \
  -monitor unix:/tmp/qemu_monitor.sock,server,nowait \
  -daemonize

# Connect VNC (wait ~3 min for Dalvik on ARM emulation)
# Windows: TightVNC -> localhost:5900
# macOS: open vnc://localhost:5900
```

> **Note:** `-device virtio-tablet-device` enables touch/click input from VNC.
> Requires kernel patches for `virtio_mmio.c` and `virtio_input.c` (see [VNC.md](VNC.md)).

## What You'll See

The VNC display shows the Android View tree rendered from Dalvik:

- Red title bar: "Android on OHOS" + "Android on OHOS ARM32 QEMU"
- Menu items with TTF fonts: Big Mock Burger ($5.99), Quarter Mocker ($4.99), etc.
- Green "View Cart" button
- Numbered red circles for each item
- Footer: "30 views | 8 items | TTF fonts | Touch enabled"
- Click any item: highlights briefly, status bar shows "Tapped: <item name>"

## Rendering Paths

There are two rendering paths:

### Path 1: RECT geometry (default)
`ViewDumper.java` runs `view.measure()` + `view.layout()`, outputs RECT commands.
The init binary parses RECTs and draws with stb_truetype TTF fonts to `/dev/fb0`.

### Path 2: Canvas pixel rendering (software OH_Drawing)
`CanvasViewDumper.java` calls `view.draw(canvas)` which invokes the real Android
Canvas API. The Dalvik VM's OHBridge JNI routes Canvas calls to a software
renderer (`software_canvas.h` + `stb_truetype.h`) that produces ARGB8888 pixels.
The init binary blits the raw pixel buffer directly to `/dev/fb0`.

Canvas operations implemented: drawRect, drawCircle, drawLine, drawText,
drawRoundRect, drawOval, drawBitmap, drawPath, drawColor, save/restore,
translate, scale, clipRect. 70 JNI methods total.

## ViewDumper Output Format

The `ViewDumper.java` class creates an Activity, runs `measure()` + `layout()`,
then outputs geometry as RECT commands:

```
=== ViewDumper ===
ACTIVITY: com.example.mockdonalds.MenuActivity
ROOT: android.widget.FrameLayout
SCREEN 1280 800
RECT 0 0 1280 800 0 FrameLayout
RECT 0 0 1280 800 ff1a1a2e LinearLayout
RECT 0 0 1280 65 ff212121 TextView MockDonalds Menu
RECT 0 65 1280 21 ff212121 TextView Cart: 0 items
RECT 0 86 1280 344 ff303030 ListView
RECT 0 86 1280 43 ff1a1a2e LinearLayout
RECT 0 86 288 43 ff212121 TextView Big Mock Burger
RECT 288 108 64 21 ff212121 TextView   $5.99
...
RECT 0 430 1280 21 ff4caf50 Button View Cart
=== END ===
```

Format: `RECT x y width height color type text`

The init binary parses these RECT lines and draws colored rectangles to `/dev/fb0`,
which appears on VNC.

## Key Constraints

- **DEX format 035**: Use `dx` (not `d8`) for KitKat Dalvik compatibility
- **No lambdas in shim code**: KitKat Dalvik has no `invokedynamic` support
- **No String.format / Pattern.compile**: KitKat natives missing these methods
- **GCC for PID 1 binaries**: OHOS musl `preinit_stubs` crashes when running as init
- **`-smp 1`**: Multi-core causes kernel SMP IPI issues with virtio-gpu
- **`/etc/passwd` in ramdisk**: Required for `System.initSystemProperties` via `getpwuid`

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `ExceptionInInitializerError` in Dalvik | `getpwuid` returns null | Add `/etc/passwd` to ramdisk |
| NPE in `AbsListView.setSelector` | `Context.getDrawable` returns null | Shim must return `ColorDrawable(0)` |
| DEX class not found | Missing from bootclasspath | Add to both `-Xbootclasspath` and `-classpath` |
| Empty viewdump.txt | stderr floods output file | Redirect dalvikvm stderr to `/dev/null` |
| Black VNC screen | Init crashed (musl issue) | Compile init with GCC, not OHOS Clang |
| No `/dev/fb0` | Kernel DRM/fbdev not configured | See [VNC.md](VNC.md) for kernel patches |
| `UnsatisfiedLinkError: bitmapCreate` | JNI symbols stripped from static binary | Use `--whole-archive` when linking libdvm.a + register via `dvmRegisterOHBridge` |
| No touch device (only gpio-keys) | `virtio_input.c` rejects VERSION_1 | Patch kernel: see [VNC.md](VNC.md) patches 3+4 |
| Click does nothing in VNC | virtio-mmio blocks v2 devices | Patch `virtio_mmio.c` VERSION_1 check |
| Canvas pixels black except text | `View.draw()` text color defaults to black | Set background color in Activity or use RECT fallback path |
