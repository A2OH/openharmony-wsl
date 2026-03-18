# 故障排除

在 WSL2 上构建和运行 OpenHarmony 时的常见问题和解决方案。

## 构建问题

### "defines" 变量被覆盖（数百个错误）

**症状：** 大量类似以下的错误：
```
ERROR: Assignment had no effect
```
或编译期间出现意外的未定义符号。

**原因：** GN 构建文件使用 `defines = [...]` 覆盖了之前设置的宏定义，
应该使用 `defines += [...]`。

**修复：** 应用补丁 01：
```bash
OHOS_ROOT=~/openharmony ./scripts/apply_patches.sh
```

或手动修复：
```bash
cd ~/openharmony
find . -name "*.gn" -not -path "./out/*" -not -path "./.repo/*" \
  -exec grep -l 'defines = \[' {} \; | while read f; do
    sed -i 's/defines = \[/defines += \[/g' "$f"
done
```

### 构建在第一个错误时停止

**症状：** Ninja 在首个编译失败后停止，尽管大多数目标可以成功编译。

**原因：** Ninja 默认行为是遇到第一个错误即停止。

**修复：** 应用补丁 02（添加 `-k 0` 到 ninja），或手动编辑：
```bash
# 在 build/hb/services/ninja.py 中，将 '-k', '0' 添加到 ninja_cmd
```

### Ruby 错误："uninitialized constant OpenStruct"

**症状：**
```
NameError: uninitialized constant OpenStruct
```

**原因：** Ruby 3.3+ 移除了对 `ostruct` 的自动加载。

**修复：** 应用补丁 03，或在 `arkcompiler/runtime_core/` 中失败的 Ruby 脚本中
手动添加 `require 'ostruct'`。

### 缺少 \#include \<cstdint\>

**症状：**
```
error: 'uint32_t' was not declared in this scope
```

**原因：** GCC 13+ 对隐式包含更加严格。

**修复：** 应用补丁 04，或在失败的文件中添加 `#include <cstdint>`。

### 组件不匹配异常

**症状：**
```
Exception: subsystem 'xxx' component 'yyy' not in subsystem_config
```

**原因：** 加载器中的严格验证拒绝了有效的配置。

**修复：** 应用补丁 02（将异常降级为警告），或编辑：
```python
# 在 build/hb/util/loader/load_ohos_build.py 第 641 行：
# 将: raise Exception(message)
# 改为: print(f"Warning: {message}")
```

### 磁盘空间不足

**症状：** 构建失败，提示 "No space left on device"。

**原因：** OpenHarmony 源码 + 构建输出需要 150-200 GB。

**修复：**
```bash
# 检查磁盘使用情况
df -h ~

# 清理构建产物（保留源码）
rm -rf ~/openharmony/out

# 或扩展 WSL2 磁盘：
# 在 PowerShell（管理员）中：
wsl --shutdown
# 然后调整 VHD 大小
```

### 内存不足

**症状：** 编译器或链接器被 OOM 杀死。

**原因：** WSL2 默认内存限制可能过低。

**修复：** 在 Windows 上创建/编辑 `%UserProfile%\.wslconfig`：
```ini
[wsl2]
memory=24GB
processors=8
swap=8GB
```
然后重启 WSL：在 PowerShell 中运行 `wsl --shutdown`。

## QEMU 启动问题

### 镜像文件 "No such file" 错误

**症状：**
```
ERROR: system.img not found
```

**原因：** 镜像未生成，或 OHOS_ROOT 设置不正确。

**修复：**
```bash
# 确认镜像存在
ls ~/openharmony/out/qemu-arm-linux/packages/phone/images/

# 如果缺失，运行 prepare_images.sh
OHOS_ROOT=~/openharmony ./scripts/prepare_images.sh
```

### QEMU 在 "Booting Linux" 处卡住

**症状：** 内核开始启动但在 init 之前卡住。

**原因：** 通常是磁盘顺序问题。Virtio-mmio 以逆序枚举。

**修复：** 确保 QEMU 命令中的磁盘顺序为：
1. userdata（变为 vdd）
2. vendor（变为 vdc）
3. system（变为 vdb）
4. updater（变为 vda）

### "init: mount required partition failed"

**症状：**
```
init: MountRequiredPartitions failed
```

**原因：** ext4 镜像使用了与 QEMU 内核不兼容的功能特性，或块设备映射不正确。

**修复：** 确保镜像使用 `^64bit,^metadata_csum` 创建：
```bash
mke2fs -t ext4 -b 4096 -I 256 -O ^64bit,^metadata_csum ...
```

`prepare_images.sh` 脚本会自动处理。

### VNC 模式下黑屏

**症状：** VNC 连接成功但只显示黑屏。

**原因：** 内核中缺少 DRM/帧缓冲驱动。

**修复：** 应用补丁 05（启用 CONFIG_DRM_BOCHS），或重新编译内核时添加：
```
CONFIG_DRM_BOCHS=y
```

### Foundation 在启动时崩溃

**症状：**
```
foundation: critical service restart limit reached
```

**原因：** 默认情况下 `foundation` 被标记为关键服务。如果它崩溃（例如缺少依赖），
init 会重启系统。

**修复：** `prepare_images.sh` 脚本为 foundation 设置了 `"critical":[0]`。
如果未使用此脚本重新生成镜像，请重新运行。

### 服务未启动

**症状：** `samgr` 在运行，但系统能力（SA 180、401、501）未注册。

**原因：** 缺少服务配置文件或库文件。

**修复：**
```bash
# 检查 foundation.json 是否存在
debugfs system.img -R "stat /profile/foundation.json" 2>/dev/null

# 检查服务库是否存在
debugfs system.img -R "ls /lib" 2>/dev/null | grep -E "abilityms|bms|appms"
```

如果缺失，确保产品配置包含相关子系统（ability 框架、bundle 管理器等）并重新构建。

## WSL2 特定问题

### 文件 I/O 速度慢

**症状：** 构建时间远超预期。

**原因：** WSL2 文件系统性能不一。Windows 挂载点（`/mnt/c/`）上的文件操作极慢。

**修复：** 始终在 Linux 文件系统内（`~/`）工作，不要在 `/mnt/c/` 上操作。

### VNC 端口转发

**症状：** 无法从 Windows 连接到 VNC。

**原因：** WSL2 运行在独立 VM 中，有自己的 IP 地址。

**修复：**
```bash
# 获取 WSL2 IP
hostname -I

# 或使用 localhost 转发（较新的 WSL2 版本自动支持）
# 在 .wslconfig 中：
[wsl2]
networkingMode=mirrored
```

### 系统时钟问题

**症状：** 与时间戳相关的构建错误，或 `repo sync` 出现 SSL 错误。

**修复：**
```bash
sudo hwclock -s
# 或
sudo ntpdate pool.ntp.org
```
