#!/bin/bash
# Boot OHOS QEMU with VNC display (virtio-gpu)
# Requires: QEMU built from source with virtio-gpu support
# Requires: Patched kernel (07-virtio-gpu-arm32-fix.diff)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OH=${OHOS_ROOT:-/home/dspfac/openharmony}
QEMU=${QEMU_PATH:-/tmp/qemu-8.2.2/build/qemu-system-arm}
IMAGES=$OH/out/qemu-arm-linux/packages/phone/images
RAMDISK=${RAMDISK:-$IMAGES/ramdisk.img}

if [ ! -f "$QEMU" ]; then
    echo "ERROR: QEMU not found at $QEMU"
    echo "Build from source: see docs/VNC.md"
    exit 1
fi

echo "Starting OHOS QEMU with VNC display..."
echo "VNC: localhost:5900"
echo "Press Ctrl+C to stop"

$QEMU \
  -M virt -cpu cortex-a7 -smp 1 -m 1024 \
  -display vnc=:0 \
  -device virtio-gpu-device \
  -drive if=none,file=$IMAGES/userdata.img,format=raw,id=ud -device virtio-blk-device,drive=ud \
  -drive if=none,file=$IMAGES/vendor.img,format=raw,id=vd -device virtio-blk-device,drive=vd \
  -drive if=none,file=$IMAGES/system.img,format=raw,id=sd -device virtio-blk-device,drive=sd \
  -drive if=none,file=$IMAGES/updater.img,format=raw,id=up -device virtio-blk-device,drive=up \
  -kernel $IMAGES/zImage-dtb \
  -initrd $RAMDISK \
  -append "console=ttyAMA0,115200 quiet loglevel=0 init=/bin/init hardware=qemu.arm.linux default_boot_device=a003e00.virtio_mmio root=/dev/ram0 rw ohos.required_mount.system=/dev/block/vdb@/usr@ext4@ro,barrier=1@wait,required ohos.required_mount.vendor=/dev/block/vdc@/vendor@ext4@ro,barrier=1@wait,required ohos.required_mount.data=/dev/block/vdd@/data@ext4@nosuid,nodev,noatime,barrier=1@wait,required"
