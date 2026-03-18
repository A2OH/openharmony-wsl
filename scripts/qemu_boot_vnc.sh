#!/bin/bash
# Boot OpenHarmony in QEMU ARM32 with VNC display
#
# Usage:
#   OHOS_ROOT=~/openharmony ./qemu_boot_vnc.sh [timeout_seconds]
#
# After boot, connect with a VNC viewer:
#   vncviewer localhost:5900
#
# Prerequisites:
#   - qemu-system-arm installed (apt install qemu-system-arm)
#   - Images prepared via prepare_images.sh
#   - VNC viewer on your host (TigerVNC, RealVNC, etc.)
#
# For WSL2 users:
#   - Forward port 5900 or use Windows VNC viewer pointing to WSL2 IP
#   - Find WSL2 IP: hostname -I

set -e

TIMEOUT=${1:-120}
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

echo "Starting OpenHarmony QEMU with VNC on :5900..."
echo "Connect with: vncviewer localhost:5900"
echo "Serial console on stdio. Press Ctrl+A then X to exit."
echo ""

QEMU_CMD="$QEMU \
  -M virt -cpu cortex-a7 -smp 4 -m 1024 \
  -vnc :0 -serial stdio \
  -device virtio-gpu-device \
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
