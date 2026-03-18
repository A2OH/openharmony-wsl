# 在 WSL2 上构建 OpenHarmony

从源码在 Windows WSL2 上构建 OpenHarmony，并在 QEMU ARM32 模拟器中启动的完整指南。

**预计时间：** 首次构建约 3-5 小时（取决于 CPU 和网络速度）。

## 前提条件

### 系统要求

| 要求 | 最低配置 | 推荐配置 |
|------|---------|---------|
| 操作系统 | Windows 10 21H2+ (WSL2) | Windows 11 |
| WSL2 发行版 | Ubuntu 22.04 | Ubuntu 24.04 |
| 内存 | 16 GB | 32 GB |
| 磁盘空间 | 150 GB 可用 | 250 GB 可用 |
| CPU 核心数 | 4 | 8+ |

### 步骤 0：确认 WSL2

在 Windows PowerShell 中：
```powershell
wsl --version
# 预期输出: WSL version 2.x.x.x
```

如果尚未安装：
```powershell
wsl --install -d Ubuntu-24.04
```

### 步骤 1：安装依赖（约5分钟）

```bash
sudo apt update && sudo apt install -y \
  build-essential git git-lfs python3 python3-pip \
  curl wget unzip zip scons ccache \
  libncurses5-dev libssl-dev bc flex bison \
  ruby gperf device-tree-compiler \
  default-jdk e2fsprogs \
  qemu-system-arm

# 验证关键工具
git --version          # 预期: 2.x+
python3 --version      # 预期: 3.10+
ruby --version         # 预期: 3.0+
qemu-system-arm --version  # 预期: 7.2+
javac -version         # 预期: 11+
```

**预期输出：** 所有版本检查通过。如果 Ruby 版本为 3.3+，则需要补丁 03（ostruct 兼容性）。

### 步骤 2：安装 repo 工具（约1分钟）

```bash
mkdir -p ~/bin
curl https://storage.googleapis.com/git-repo-downloads/repo > ~/bin/repo
chmod a+x ~/bin/repo
echo 'export PATH=~/bin:$PATH' >> ~/.bashrc
source ~/.bashrc
repo version
# 预期: repo version v2.x
```

### 步骤 3：配置 Git

```bash
git config --global user.email "you@example.com"
git config --global user.name "Your Name"
git config --global color.ui false
```

### 步骤 4：下载 OpenHarmony 源码（约60-90分钟）

```bash
mkdir -p ~/openharmony && cd ~/openharmony
repo init -u https://gitee.com/openharmony/manifest.git -b master --no-repo-verify
repo sync -c -j$(nproc) --no-tags
```

**预期输出：** 同步完成后，`ls` 应显示 `base/`、`build/`、`foundation/`、`kernel/` 等目录。
总大小约 80-120 GB。

**提示：** 如果同步中途失败，重新运行 `repo sync -c -j4 --no-tags` 即可从断点继续。

### 步骤 5：下载预编译工具链（约15-30分钟）

```bash
cd ~/openharmony
bash build/prebuilts_download.sh
```

**预期输出：** 将 clang、node、python 等工具链下载到 `prebuilts/` 目录，额外占用约 10-20 GB。

## 构建

### 步骤 6：应用补丁（约30秒）

```bash
# 克隆本仓库
cd ~
git clone https://github.com/A2OH/openharmony-wsl.git

# 应用所有补丁
cd ~/openharmony-wsl
OHOS_ROOT=~/openharmony ./scripts/apply_patches.sh
```

如果部分补丁自动应用失败（由于 repo 多项目结构），需手动应用。
详见 `patches/README.md`。

**替代方案：手动应用最关键的补丁**

最重要的补丁是 01（GN defines 覆盖问题）。可以用一行命令修复：

```bash
cd ~/openharmony
find . -name "*.gn" -not -path "./out/*" -not -path "./.repo/*" \
  -exec grep -l 'defines = \[' {} \; | while read f; do
    sed -i 's/defines = \[/defines += \[/g' "$f"
done
```

### 步骤 7：构建（约45-120分钟）

```bash
cd ~/openharmony
python3 build/hb/main.py build \
  --product-name qemu-arm-linux-min \
  --no-prebuilt-sdk
```

**预期输出：**
```
[OHOS INFO] build success
[OHOS INFO] cost time: ...
```

构建产物输出到 `out/qemu-arm-linux/`。主要文件：
- `packages/phone/images/zImage-dtb` - 内核
- `packages/phone/images/ramdisk.img` - 初始内存盘
- `packages/phone/system/` - 系统根文件系统

**如果构建失败：** 参见 [TROUBLESHOOTING_CN.md](TROUBLESHOOTING_CN.md)。
补丁 02 中的 `-k 0` 标志确保 ninja 在单个目标失败后继续构建。

### 步骤 8：准备 QEMU 镜像（约1分钟）

```bash
cd ~/openharmony-wsl
OHOS_ROOT=~/openharmony ./scripts/prepare_images.sh
```

**预期输出：**
```
=== Preparing images for QEMU boot ===
[1/7] Creating mount points...
[2/7] Creating /system symlinks...
[3/7] Configuring init services...
[4/7] Checking foundation.json...
[5/7] Creating system.img (25600 blocks)...
[6/7] Creating userdata.img (25600 blocks)...
[7/7] Creating vendor.img (10240 blocks)...
=== Done! Images ready ===
```

## 启动

### 步骤 9：启动 QEMU（约30-60秒完成启动）

**无显示模式（串口控制台）：**
```bash
OHOS_ROOT=~/openharmony ./scripts/qemu_boot.sh 0
```

**VNC 显示模式：**
```bash
OHOS_ROOT=~/openharmony ./scripts/qemu_boot_vnc.sh 0
# 然后连接: vncviewer localhost:5900
```

**预期输出（串口控制台）：**
```
Booting Linux on physical CPU 0x0
...
[    2.xxx] init: OpenHarmony init started
...
[    5.xxx] samgr: service manager ready
...
[   15.xxx] foundation: system abilities loaded
```

### 步骤 10：验证

启动后应能看到系统服务运行：

```bash
# 在 QEMU 控制台中：
cat /proc/version
# 预期: Linux version 5.10.184 ...

ls /system/bin/
# 预期: hdcd, samgr, foundation, hilogd 等

# 检查运行中的服务
ps -ef | grep -E "samgr|foundation|hilog"
```

## 产品配置

默认产品配置为 `qemu-arm-linux-min`，包含以下子系统：

| 子系统 | 组件 |
|--------|------|
| build | build_framework |
| startup | init |
| hiviewdfx | hilog, hitrace, faultloggerd, hisysevent, hichecker |
| distributedhardware | device_manager |
| security | device_auth, access_token, huks |
| commonlibrary | c_utils, ylong_runtime |
| communication | ipc, dsoftbus |
| notification | eventhandler |
| systemabilitymgr | samgr, safwk |
| developtools | bytrace, hdc |
| resourceschedule | ffrt, frame_aware_sched |

配置文件位于：`vendor/ohemu/qemu_arm_linux_min/config.json`

如需添加更多子系统（如 ability 框架），编辑此文件并重新构建。

## 构建产物

构建成功后，主要文件位于：

```
out/qemu-arm-linux/
├── packages/phone/
│   ├── images/
│   │   ├── zImage-dtb          # Linux 内核（含设备树）
│   │   ├── ramdisk.img         # 初始内存盘
│   │   ├── system.img          # 系统分区 (ext4)
│   │   ├── vendor.img          # 厂商分区 (ext4)
│   │   ├── userdata.img        # 用户数据分区 (ext4)
│   │   └── updater.img         # 更新分区 (ext4)
│   └── system/                 # 解包的系统根目录
│       ├── bin/                # 系统二进制文件
│       ├── lib/                # 共享库 (466+)
│       ├── etc/                # 配置文件
│       └── profile/            # 服务配置
├── clang_x64/                  # 编译过程中生成的主机工具
└── *.log                       # 构建日志
```
