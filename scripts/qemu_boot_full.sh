#!/bin/bash
OH=/home/dspfac/openharmony
IMAGES=$OH/out/qemu-arm-linux/packages/phone/images
QEMU=$OH/tools/qemu-extracted/usr/bin/qemu-system-arm
QEMU_LIB=$OH/tools/qemu-extracted/usr/lib/x86_64-linux-gnu
export LD_LIBRARY_PATH=$QEMU_LIB
exec $QEMU -M virt -cpu cortex-a7 -smp 1 -m 1024 -nographic \
  -L $OH/tools/qemu-extracted/usr/share/qemu \
  -drive if=none,file=$IMAGES/userdata.img,format=raw,id=userdata -device virtio-blk-device,drive=userdata \
  -drive if=none,file=$IMAGES/vendor.img,format=raw,id=vendor -device virtio-blk-device,drive=vendor \
  -drive if=none,file=$IMAGES/system.img,format=raw,id=system -device virtio-blk-device,drive=system \
  -drive if=none,file=$IMAGES/updater.img,format=raw,id=updater -device virtio-blk-device,drive=updater \
  -kernel $IMAGES/zImage-dtb -initrd $IMAGES/ramdisk.img \
  -append "console=ttyAMA0,115200 init=/bin/init hardware=qemu.arm.linux default_boot_device=a003e00.virtio_mmio root=/dev/ram0 rw ohos.required_mount.system=/dev/block/vdb@/usr@ext4@ro,barrier=1@wait,required ohos.required_mount.vendor=/dev/block/vdc@/vendor@ext4@ro,barrier=1@wait,required ohos.required_mount.data=/dev/block/vdd@/data@ext4@nosuid,nodev,noatime,barrier=1@wait,required"
