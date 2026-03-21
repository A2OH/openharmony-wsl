#!/bin/bash
# Prepare QEMU images after OpenHarmony build
# Fixes ext4 features, creates vendor.img, adds mount points and symlinks
#
# Prerequisites:
#   - Completed OHOS build: python3 build/hb/main.py build --product-name qemu-arm-linux-min --no-prebuilt-sdk
#
# Usage:
#   OHOS_ROOT=~/openharmony ./prepare_images.sh

set -e

OHOS_ROOT="${OHOS_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
IMAGES_DIR="$OHOS_ROOT/out/qemu-arm-linux/packages/phone/images"
SYS_DIR="$OHOS_ROOT/out/qemu-arm-linux/packages/phone/system"
MKE2FS="$OHOS_ROOT/out/qemu-arm-linux/clang_x64/thirdparty/e2fsprogs/mke2fs"
PYTHON="$OHOS_ROOT/prebuilts/python/linux-x86/current/bin/python3"
VENDOR_DIR="/tmp/vendor_root"

if [ ! -d "$SYS_DIR" ]; then
    echo "ERROR: System directory not found at $SYS_DIR"
    echo "Did you run the build first?"
    exit 1
fi

if [ ! -f "$MKE2FS" ]; then
    echo "ERROR: mke2fs not found at $MKE2FS"
    echo "Build may not have completed successfully."
    exit 1
fi

echo "=== Preparing images for QEMU boot ==="
echo "OHOS_ROOT: $OHOS_ROOT"
echo "Output:    $IMAGES_DIR"

# 1. Create mount point directories
echo "[1/11] Creating mount points..."
mkdir -p "$SYS_DIR"/{dev,mnt,mnt/data,proc,sys,vendor,storage,data,init,tmp,config}

# 2. Create /system symlinks
echo "[2/11] Creating /system symlinks..."
mkdir -p "$SYS_DIR/system"
ln -sfn ../etc "$SYS_DIR/system/etc"
ln -sfn ../bin "$SYS_DIR/system/bin"
ln -sfn ../lib "$SYS_DIR/system/lib"
ln -sfn ../profile "$SYS_DIR/system/profile"

# 3. Enable console + hdcd, make foundation non-critical
echo "[3/11] Configuring init services..."
$PYTHON - "$SYS_DIR" <<'PYEOF'
import json, sys
sys_dir = sys.argv[1]
for fname, svc, mods in [
    (f"{sys_dir}/etc/init/console.cfg", "console", {"disabled":0,"start-mode":"boot","ondemand":False}),
    (f"{sys_dir}/etc/init/hdcd.cfg", "hdcd", {"disabled":0,"start-mode":"boot"}),
    (f"{sys_dir}/etc/init/foundation.cfg", "foundation", {"critical":[0]}),
]:
    with open(fname) as f: cfg = json.load(f)
    for s in cfg.get('services',[]):
        if s['name'] == svc: s.update(mods)
    with open(fname,'w') as f: json.dump(cfg,f,indent=4)
    print(f"  {svc}: {mods}")
PYEOF

# 4. Create empty foundation.json if missing
echo "[4/11] Checking foundation.json..."
if [ ! -f "$SYS_DIR/profile/foundation.json" ]; then
    echo '{"process":{"name":"foundation"},"systemability":[]}' > "$SYS_DIR/profile/foundation.json"
    echo "  Created empty foundation.json"
fi

# 5. Add ueventd rule for /dev/fb0
echo "[5/11] Adding ueventd rule for /dev/fb0..."
UEVENTD_CFG="$SYS_DIR/etc/ueventd.config"
if ! grep -q "^/dev/fb0 " "$UEVENTD_CFG" 2>/dev/null; then
    # Add /dev/fb0 entry (major 29, auto-created by DRM fbdev emulation)
    sed -i '/^\/dev\/graphics\/fb0/a /dev/fb0 0666 0 1003' "$UEVENTD_CFG"
    echo "  Added /dev/fb0 0666 0 1003"
else
    echo "  Already present"
fi

# 6. Deploy lib2d_graphics (Skia) and dependencies
echo "[6/11] Deploying Skia/OH_Drawing libraries..."
BUILD_OUT="$OHOS_ROOT/out/qemu-arm-linux"
PLATFORMSDK="$SYS_DIR/lib/platformsdk"
CHIPSETSDK="$SYS_DIR/lib/chipset-pub-sdk"
mkdir -p "$PLATFORMSDK" "$CHIPSETSDK"
for lib in \
    "$BUILD_OUT/innerkits/ohos-arm/graphic_2d/2d_graphics/lib2d_graphics.z.so" \
    "$BUILD_OUT/thirdparty/skia/libskia_canvaskit.z.so" \
    "$BUILD_OUT/innerkits/ohos-arm/thirdparty/icu/libhmicuuc.z.so" \
    ; do
    if [ -f "$lib" ]; then
        cp "$lib" "$PLATFORMSDK/"
        echo "  Deployed $(basename $lib)"
    fi
done
# libhmicuuc depends on libhmicudt
for lib in "$BUILD_OUT"/innerkits/ohos-arm/thirdparty/icu/libhmicudt*.so; do
    [ -f "$lib" ] && cp "$lib" "$PLATFORMSDK/" && echo "  Deployed $(basename $lib)"
done

# 7. Create dalvikvm init service config
echo "[7/11] Creating dalvikvm init service..."
cat > "$SYS_DIR/etc/init/dalvikvm.cfg" << 'SVCEOF'
{
    "services": [
        {
            "name": "dalvikvm",
            "path": [
                "/system/bin/sh",
                "/system/bin/run_dalvikvm.sh"
            ],
            "uid": "root",
            "gid": ["root", "shell", "graphics"],
            "critical": [0],
            "apl": "normal",
            "sandbox": 0,
            "start-mode": "condition",
            "secon": "u:r:su:s0",
            "disabled": 0
        }
    ],
    "jobs": [
        {
            "name": "post-fs-data",
            "cmds": [
                "mkdir /data/a2oh 0755 root shell",
                "mkdir /data/a2oh/dalvik-cache 0755 root shell",
                "mkdir /data/a2oh/bin 0755 root shell",
                "mknod /dev/fb0 c 29 0"
            ]
        },
        {
            "name": "boot",
            "cmds": [
                "start dalvikvm"
            ]
        }
    ]
}
SVCEOF
echo "  Created dalvikvm.cfg"

# 8. Create run_showcase.sh for the dalvikvm service
echo "[8/11] Creating dalvikvm run script..."
cat > "$SYS_DIR/bin/run_dalvikvm.sh" << 'RUNEOF'
#!/system/bin/sh
# Dalvik VM showcase launcher — started by OHOS init as 'dalvikvm' service
export ANDROID_DATA=/data/a2oh
export ANDROID_ROOT=/data/a2oh
export LD_LIBRARY_PATH=/system/lib/platformsdk:/system/lib/chipset-pub-sdk:/system/lib:$LD_LIBRARY_PATH

# Wait for data partition
while [ ! -d /data/a2oh ]; do sleep 1; done

# Ensure dexopt is available
chmod 755 /data/a2oh/dalvikvm /data/a2oh/dexopt 2>/dev/null
cp /data/a2oh/dexopt /data/a2oh/bin/dexopt 2>/dev/null

# Create /dev/fb0 if not present
if [ ! -c /dev/fb0 ]; then
    mknod /dev/fb0 c 29 0
    chmod 0666 /dev/fb0
fi

# Run Dalvik
cd /data/a2oh
exec ./dalvikvm -Xverify:none -Xdexopt:none \
    -Xbootclasspath:/data/a2oh/core.jar:/data/a2oh/app.dex \
    -classpath /data/a2oh/app.dex \
    CanvasViewDumper com.example.showcase.ShowcaseActivity
RUNEOF
chmod 755 "$SYS_DIR/bin/run_dalvikvm.sh"
echo "  Created run_dalvikvm.sh"

# 9. Create system.img (ext4, no 64bit features for QEMU compat)
echo "[9/11] Creating system.img (25600 blocks)..."
$MKE2FS -t ext4 -b 4096 -I 256 -O ^64bit,^metadata_csum \
    -d "$SYS_DIR" -L system "$IMAGES_DIR/system.img" 25600 2>&1 | tail -1

# 10. Create userdata.img
echo "[10/11] Creating userdata.img (25600 blocks)..."
$MKE2FS -t ext4 -b 4096 -I 256 -O ^64bit,^metadata_csum \
    -L userdata "$IMAGES_DIR/userdata.img" 25600 2>&1 | tail -1

# 7. Create vendor.img
echo "[11/11] Creating vendor.img (10240 blocks)..."
rm -rf "$VENDOR_DIR" && mkdir -p "$VENDOR_DIR/etc/param"
cp "$OHOS_ROOT/device/qemu/arm_virt/linux/chipset/etc/fstab.qemu.arm.linux" "$VENDOR_DIR/etc/"
cp "$OHOS_ROOT/device/qemu/arm_virt/linux/rootfs/init.qemu.arm.linux.cfg" "$VENDOR_DIR/etc/"
cp "$OHOS_ROOT/device/qemu/arm_virt/linux/rootfs/init.qemu.arm.linux.usb.cfg" "$VENDOR_DIR/etc/" 2>/dev/null || true
cp "$OHOS_ROOT/device/qemu/arm_virt/linux/rootfs/default.para" "$VENDOR_DIR/etc/" 2>/dev/null || true
$MKE2FS -t ext4 -b 4096 -I 256 -O ^64bit,^metadata_csum \
    -d "$VENDOR_DIR" -L vendor "$IMAGES_DIR/vendor.img" 10240 2>&1 | tail -1

echo ""
echo "=== Done! Images ready at: $IMAGES_DIR ==="
echo "Boot with: ./scripts/qemu_boot.sh"
