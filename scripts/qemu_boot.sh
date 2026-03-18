#!/bin/bash
# Boot OpenHarmony in QEMU ARM32 (headless serial console)
#
# Usage:
#   OHOS_ROOT=~/openharmony ./qemu_boot.sh [timeout_seconds]
#
# The timeout defaults to 60 seconds. Set to 0 for no timeout.
#
# Prerequisites:
#   - qemu-system-arm installed (apt install qemu-system-arm)
#   - Images prepared via prepare_images.sh
#
# Expected output:
#   - Kernel boot messages on serial console
#   - OpenHarmony init starts services (samgr, foundation, hilogd, etc.)
#   - Shell prompt available after ~30-60 seconds

set -e

TIMEOUT=${1:-60}
OHOS_ROOT="${OHOS_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
IMAGES="$OHOS_ROOT/out/qemu-arm-linux/packages/phone/images"

# Check for QEMU
QEMU="${QEMU_PATH:-$(which qemu-system-arm 2>/dev/null || true)}"
if [ -z "$QEMU" ]; then
    echo "ERROR: qemu-system-arm not found."
    echo "Install with: sudo apt install qemu-system-arm"
    exit 1
fi

# Verify images exist
for img in zImage-dtb ramdisk.img system.img vendor.img userdata.img updater.img; do
    if [ ! -f "$IMAGES/$img" ]; then
        echo "ERROR: $img not found at $IMAGES/"
        echo "Run prepare_images.sh first."
        exit 1
    fi
done

echo "Booting OpenHarmony (headless, timeout=${TIMEOUT}s)..."
echo "Images: $IMAGES"
echo "Press Ctrl+A then X to exit QEMU."
echo ""

# Drive order matters: virtio-mmio enumerates in REVERSE command-line order
# vda=updater, vdb=system, vdc=vendor, vdd=userdata
QEMU_CMD="$QEMU \
  -M virt -cpu cortex-a7 -smp 4 -m 1024 -nographic \
  -drive if=none,file=$IMAGES/userdata.img,format=raw,id=userdata -device virtio-blk-device,drive=userdata \
  -drive if=none,file=$IMAGES/vendor.img,format=raw,id=vendor -device virtio-blk-device,drive=vendor \
  -drive if=none,file=$IMAGES/system.img,format=raw,id=system -device virtio-blk-device,drive=system \
  -drive if=none,file=$IMAGES/updater.img,format=raw,id=updater -device virtio-blk-device,drive=updater \
  -kernel $IMAGES/zImage-dtb -initrd $IMAGES/ramdisk.img \
  -append 'console=ttyAMA0,115200 init=/bin/init hardware=qemu.arm.linux default_boot_device=a003e00.virtio_mmio root=/dev/ram0 rw ohos.required_mount.system=/dev/block/vdb@/usr@ext4@ro,barrier=1@wait,required ohos.required_mount.vendor=/dev/block/vdc@/vendor@ext4@ro,barrier=1@wait,required ohos.required_mount.data=/dev/block/vdd@/data@ext4@nosuid,nodev,noatime,barrier=1@wait,required'"

if [ "$TIMEOUT" -gt 0 ] 2>/dev/null; then
    timeout "$TIMEOUT" $QEMU_CMD
else
    $QEMU_CMD
fi
