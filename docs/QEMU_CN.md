# QEMU 设置与选项

## 概述

OpenHarmony 运行在 QEMU ARM32（`qemu-system-arm`）上，模拟一个搭载 Cortex-A7 CPU
的 virt 虚拟机。系统启动 Linux 5.10.184 内核，运行 OHOS 用户空间（init、samgr、
foundation 及系统服务）。

## 架构

```
宿主机 (WSL2 x86_64)
├── qemu-system-arm
│   ├── 机器类型: virt
│   ├── CPU: cortex-a7 x4
│   ├── 内存: 1024 MB
│   ├── 磁盘 (virtio-blk):
│   │   ├── vda = updater.img
│   │   ├── vdb = system.img    → 挂载到 /usr
│   │   ├── vdc = vendor.img    → 挂载到 /vendor
│   │   └── vdd = userdata.img  → 挂载到 /data
│   ├── 内核: zImage-dtb
│   └── 初始内存盘: ramdisk.img
└── 串口: ttyAMA0（控制台）
```

## 启动模式

### 无显示模式（串口控制台）

最适合开发和测试，无需图形界面。

```bash
OHOS_ROOT=~/openharmony ./scripts/qemu_boot.sh 0
```

退出方式：`Ctrl+A` 然后 `X`

### VNC 显示模式

用于测试图形/UI 组件，需要 VNC 客户端。

```bash
OHOS_ROOT=~/openharmony ./scripts/qemu_boot_vnc.sh 0
```

从 Windows 连接：`vncviewer localhost:5900`

对于 WSL2，可能需要获取 WSL IP 地址：
```bash
hostname -I
# 然后从 Windows 连接: vncviewer <WSL_IP>:5900
```

### 带网络模式

启用 QEMU 中的网络功能（下载包、测试网络连接等）：

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

宿主机的 5555 端口转发到客户机的 5555 端口（hdc 默认端口）。

### GDB 调试模式

使用 GDB 调试内核或用户空间程序：

```bash
# 在 QEMU 参数中添加 -s -S（启动时暂停，等待 GDB 连接到 1234 端口）
qemu-system-arm ... -s -S

# 在另一个终端中：
gdb-multiarch
(gdb) target remote :1234
(gdb) continue
```

## 磁盘顺序

QEMU virtio-mmio 按照命令行**逆序**枚举磁盘。启动命令行指定：

| 命令行顺序 | 磁盘 ID | 块设备 | 挂载点 |
|-----------|---------|--------|--------|
| 第1个 | userdata | vdd | /data |
| 第2个 | vendor | vdc | /vendor |
| 第3个 | system | vdb | /usr |
| 第4个 | updater | vda | （未挂载）|

内核命令行通过 `ohos.required_mount.*` 参数映射这些设备。
更改 QEMU 命令中的磁盘顺序会导致挂载失败。

## 镜像大小

| 镜像 | 默认大小 | 内容 |
|------|---------|------|
| system.img | 100 MB (25600 x 4K 块) | 系统二进制文件、库、配置 |
| userdata.img | 100 MB | 用户数据、应用数据 |
| vendor.img | 40 MB (10240 x 4K 块) | 厂商配置、fstab |
| ramdisk.img | ~5 MB | 包含 init 的初始根文件系统 |
| zImage-dtb | ~8 MB | Linux 内核 + 设备树 |
| updater.img | 不固定 | OTA 更新器（init 需要）|

如需增大镜像，编辑 `prepare_images.sh` 修改传给 `mke2fs` 的块数参数。

## 文件注入

无需重新构建即可添加或修改镜像中的文件：

```bash
# 添加文件
./scripts/inject_files.sh system.img ./my_binary /bin/my_binary

# 或直接使用 debugfs
debugfs -w system.img -R "write local_file /path/in/image"

# 列出镜像中的文件
debugfs system.img -R "ls /bin" 2>/dev/null

# 从镜像中提取文件
debugfs system.img -R "dump /bin/init /tmp/init_extracted"
```

## 性能优化建议

1. **使用 tmpfs 存放镜像：** 挂载 tmpfs 并将镜像复制到那里以提高 I/O 速度：
   ```bash
   sudo mount -t tmpfs -o size=512M tmpfs /tmp/qemu-imgs
   cp out/qemu-arm-linux/packages/phone/images/*.img /tmp/qemu-imgs/
   ```

2. **增加 SMP 核数：** 如果宿主机有 8+ 个核心，将 `-smp 4` 改为 `-smp 8`。

3. **增加内存：** 将 `-m 1024` 改为 `-m 2048` 以支持更多内存密集型工作负载。

4. **KVM 加速：** 在 x86_64 宿主机上无法用于 ARM 模拟。QEMU 使用 TCG（软件模拟），
   速度较慢但兼容性好。

## 启动序列

1. QEMU 加载 `zImage-dtb` 和 `ramdisk.img`
2. Linux 内核启动，挂载 ramdisk 作为根文件系统
3. 内核解析 `ohos.required_mount.*` 参数
4. `/bin/init` 启动（OpenHarmony init，非 systemd）
5. init 读取 `/etc/init/*.cfg` 启动服务
6. `samgr`（服务管理器）首先启动
7. `foundation` 加载系统能力（SA 180、401、501 等）
8. `hilogd`、`softbus_server`、`accesstoken_service` 启动
9. 串口控制台变为可用
10. 系统就绪（启动后约 30-60 秒）
