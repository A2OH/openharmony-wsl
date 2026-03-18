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
echo "[1/7] Creating mount points..."
mkdir -p "$SYS_DIR"/{dev,mnt,mnt/data,proc,sys,vendor,storage,data,init,tmp,config}

# 2. Create /system symlinks
echo "[2/7] Creating /system symlinks..."
mkdir -p "$SYS_DIR/system"
ln -sfn ../etc "$SYS_DIR/system/etc"
ln -sfn ../bin "$SYS_DIR/system/bin"
ln -sfn ../lib "$SYS_DIR/system/lib"
ln -sfn ../profile "$SYS_DIR/system/profile"

# 3. Enable console + hdcd, make foundation non-critical
echo "[3/7] Configuring init services..."
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
echo "[4/7] Checking foundation.json..."
if [ ! -f "$SYS_DIR/profile/foundation.json" ]; then
    echo '{"process":{"name":"foundation"},"systemability":[]}' > "$SYS_DIR/profile/foundation.json"
    echo "  Created empty foundation.json"
fi

# 5. Create system.img (ext4, no 64bit features for QEMU compat)
echo "[5/7] Creating system.img (25600 blocks)..."
$MKE2FS -t ext4 -b 4096 -I 256 -O ^64bit,^metadata_csum \
    -d "$SYS_DIR" -L system "$IMAGES_DIR/system.img" 25600 2>&1 | tail -1

# 6. Create userdata.img
echo "[6/7] Creating userdata.img (25600 blocks)..."
$MKE2FS -t ext4 -b 4096 -I 256 -O ^64bit,^metadata_csum \
    -L userdata "$IMAGES_DIR/userdata.img" 25600 2>&1 | tail -1

# 7. Create vendor.img
echo "[7/7] Creating vendor.img (10240 blocks)..."
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
