# 在 WSL2 上运行 OpenHarmony（QEMU 模拟）

[![Build](https://img.shields.io/badge/build-98%25_targets-brightgreen)]()
[![Platform](https://img.shields.io/badge/platform-WSL2_Ubuntu_24.04-blue)]()
[![Architecture](https://img.shields.io/badge/arch-ARM32_(qemu--arm)-orange)]()
[![License](https://img.shields.io/badge/license-Apache_2.0-blue)]()

无需真实硬件即可构建、启动和开发 OpenHarmony。本仓库提供构建脚本、源码补丁（diff
格式）以及详细的分步说明，用于在 WSL2 上从源码编译 OpenHarmony 并在 QEMU ARM32
模拟器中运行，支持完整的系统服务。

```
+---------------------------------------------------+
|  Windows 11 宿主机                                 |
|  +---------------------------------------------+ |
|  |  WSL2 (Ubuntu 24.04)                        | |
|  |  +---------------------------------------+  | |
|  |  |  QEMU ARM32 (qemu-system-arm)         |  | |
|  |  |  +--------------------------------+   |  | |
|  |  |  |  OpenHarmony 内核 (5.10.184)    |   |  | |
|  |  |  |  +--------------------------+   |   |  | |
|  |  |  |  | init -> samgr            |   |   |  | |
|  |  |  |  | -> foundation            |   |   |  | |
|  |  |  |  | SA 180: ability_manager  |   |   |  | |
|  |  |  |  | SA 401: bundle_manager   |   |   |  | |
|  |  |  |  | SA 501: app_manager      |   |   |  | |
|  |  |  |  | + 12 系统服务             |   |   |  | |
|  |  |  |  +--------------------------+   |   |  | |
|  |  |  +--------------------------------+   |  | |
|  |  +---------------------------------------+  | |
|  +---------------------------------------------+ |
+---------------------------------------------------+
```

## 主要成果

| 指标 | 数值 |
|------|------|
| 编译目标数 | 4,757 / 4,851 (98%) |
| 系统库 | 466 个共享库 |
| 运行中的服务 | 12+ (samgr, foundation, hilogd, accesstoken 等) |
| 系统能力 | SA 180, 401, 501 (ability, bundle, app manager) |
| 源码补丁 | ~809 个文件，涉及 157 个项目（以 diff 格式提供）|
| 启动稳定性 | 稳定，无崩溃，可正常关机 |

## 快速开始

```bash
# 1. 克隆本仓库
git clone https://github.com/A2OH/openharmony-wsl.git
cd openharmony-wsl

# 2. 下载并设置 OHOS 源码（完整步骤见 docs/BUILD_CN.md）
mkdir -p ~/openharmony && cd ~/openharmony
repo init -u https://gitee.com/openharmony/manifest.git -b master --no-repo-verify
repo sync -c -j8 --no-tags
bash build/prebuilts_download.sh

# 3. 应用补丁
cd ~/openharmony-wsl
OHOS_ROOT=~/openharmony ./scripts/apply_patches.sh

# 4. 构建
cd ~/openharmony
python3 build/hb/main.py build --product-name qemu-arm-linux-min --no-prebuilt-sdk

# 5. 准备镜像
cd ~/openharmony-wsl
OHOS_ROOT=~/openharmony ./scripts/prepare_images.sh

# 6. 启动
OHOS_ROOT=~/openharmony ./scripts/qemu_boot.sh 0
```

## 仓库结构

```
openharmony-wsl/
+-- README.md                       <- English 版本
+-- README_CN.md                    <- 本文件（中文）
+-- LICENSE                         <- Apache 2.0
+-- scripts/
|   +-- prepare_images.sh           <- 从构建输出生成 ext4 镜像
|   +-- qemu_boot.sh                <- 启动 QEMU 无显示模式（串口控制台）
|   +-- qemu_boot_vnc.sh            <- 启动 QEMU VNC 显示模式
|   +-- inject_files.sh             <- 通过 debugfs 向 ext4 镜像注入文件
|   +-- apply_patches.sh            <- 将所有补丁应用到 OHOS 源码树
+-- patches/
|   +-- README.md                   <- 补丁说明及手动应用指南
|   +-- 01-gn-defines-clobbering.diff   <- 修复 defines= 覆盖问题（639 个文件）
|   +-- 02-build-system-fixes.diff      <- ninja -k 0，加载器警告降级
|   +-- 03-ruby-ostruct-compat.diff     <- Ruby 3.3+ require 'ostruct'
|   +-- 04-cpp-header-fixes.diff        <- 缺少 #include，SUPPORT_GRAPHICS 保护
|   +-- 05-kernel-config.diff           <- 启用 DRM_BOCHS 支持 VNC
|   +-- 06-gn-component-guards.diff     <- 条件编译修复
+-- docs/
    +-- BUILD.md                    <- 完整构建说明（英文）
    +-- BUILD_CN.md                 <- 完整构建说明（中文）
    +-- QEMU.md                     <- QEMU 设置和选项（英文）
    +-- QEMU_CN.md                  <- QEMU 设置和选项（中文）
    +-- TROUBLESHOOTING.md          <- 常见问题和修复（英文）
    +-- TROUBLESHOOTING_CN.md       <- 常见问题和修复（中文）
```

## 方法：使用 Diff，不直接修改源码

本仓库**不包含**修改后的 OHOS 源代码文件。所有更改都以 **diff 文件** 的形式提供，
您可以将其应用到自己的 OHOS 源码检出中。这种方法的优势：

- 仓库体积小（约 25K 行 diff，而非 80GB 源码树）
- 适用于任何 OHOS 分支/版本（diff 可以适配）
- 不重新分发 OHOS 源代码
- 清楚地展示了每处修改及其原因

## 文档

| 文档 | 语言 | 描述 |
|------|------|------|
| [docs/BUILD.md](docs/BUILD.md) | 英文 | 从源码构建的分步指南 |
| [docs/BUILD_CN.md](docs/BUILD_CN.md) | 中文 | 从源码构建的详细步骤 |
| [docs/QEMU.md](docs/QEMU.md) | 英文 | QEMU 选项、启动模式、文件注入 |
| [docs/QEMU_CN.md](docs/QEMU_CN.md) | 中文 | QEMU 选项、启动模式、文件注入 |
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | 英文 | 常见问题和解决方案 |
| [docs/TROUBLESHOOTING_CN.md](docs/TROUBLESHOOTING_CN.md) | 中文 | 常见问题和解决方案 |
| [patches/README.md](patches/README.md) | 英文 | 补丁说明及应用指南 |

## 前提条件

| 要求 | 最低配置 | 推荐配置 |
|------|---------|---------|
| 操作系统 | Windows 10 21H2+ (WSL2) | Windows 11 |
| WSL2 发行版 | Ubuntu 22.04 | Ubuntu 24.04 |
| 内存 | 16 GB | 32 GB |
| 磁盘空间 | 150 GB 可用 | 250 GB 可用 |
| CPU 核心数 | 4 | 8+ |

```bash
sudo apt update && sudo apt install -y \
  build-essential git git-lfs python3 python3-pip \
  curl wget unzip zip scons ccache \
  libncurses5-dev libssl-dev bc flex bison \
  ruby gperf device-tree-compiler \
  default-jdk e2fsprogs qemu-system-arm
```

## 启动后可以做什么

系统运行后：

- **安装 HAP 包：** `bm install -p /path/to/app.hap`
- **启动 Ability：** `aa start -a MainAbility -b com.example.app`
- **测试 IPC/Binder：** 通过 samgr 在系统能力间通信
- **部署原生二进制文件：** 通过 debugfs 注入，在 ARM32 OHOS 上运行
- **开发 OH 系统服务：** 无需硬件即可测试
- **交叉编译和测试：** 在 x86_64 上构建 ARM32 二进制文件，立即测试

## 可用的系统库

已确认存在的主要库：

| 库 | 用途 |
|----|------|
| `libhilog_ndk.z.so` | 日志框架 |
| `libnative_rdb_ndk.z.so` | 关系型数据库 |
| `libnative_preferences.z.so` | 键值对首选项 |
| `libace_napi.z.so` | ArkUI N-API 绑定 |
| `libabilityms.z.so` | Ability 生命周期管理 |
| `libbms.z.so` | HAP 安装/管理 |
| `libappms.z.so` | 应用进程管理 |

## 已知限制

| 限制 | 影响 | 说明 |
|------|------|------|
| 仅支持 ARM32 | 中等 | ARM64 QEMU 目标已计划 |
| 无 GPU 加速 | 中等 | 仅软件渲染；OH_Drawing 不可用 |
| x86_64 宿主机无 KVM | 中等 | TCG 模拟较慢但可用 |
| 首次启动约 60 秒 | 低 | 后续启动更快 |

## 贡献

欢迎贡献。重点需求领域：

1. **ARM64 QEMU 目标** -- 扩展到 64 位 ARM 构建
2. **GPU 直通** -- 在 QEMU 中启用 OH_Drawing
3. **自动化 CI** -- 无显示启动 + GitHub Actions 冒烟测试
4. **上游补丁** -- 将修复提交到 OpenHarmony 主线

## 许可证

采用 Apache 许可证 2.0 版。详见 [LICENSE](LICENSE)。

OpenHarmony 是开放原子开源基金会的项目。本仓库是独立的社区工作，与开放原子开源
基金会无官方关联或背书。
