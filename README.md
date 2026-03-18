# OpenHarmony on WSL2 with QEMU

[![Build](https://img.shields.io/badge/build-98%25_targets-brightgreen)]()
[![Platform](https://img.shields.io/badge/platform-WSL2_Ubuntu_24.04-blue)]()
[![Architecture](https://img.shields.io/badge/arch-ARM32_(qemu--arm)-orange)]()
[![License](https://img.shields.io/badge/license-Apache_2.0-blue)]()

Build, boot, and develop for OpenHarmony without real hardware. This repository
provides build scripts, source patches (as diffs), and step-by-step instructions
for compiling OpenHarmony from source on WSL2 and running it under QEMU ARM32
emulation with full system service support.

```
+---------------------------------------------------+
|  Windows 11 Host                                  |
|  +---------------------------------------------+ |
|  |  WSL2 (Ubuntu 24.04)                        | |
|  |  +---------------------------------------+  | |
|  |  |  QEMU ARM32 (qemu-system-arm)         |  | |
|  |  |  +--------------------------------+   |  | |
|  |  |  |  OpenHarmony Kernel (5.10.184)  |   |  | |
|  |  |  |  +--------------------------+   |   |  | |
|  |  |  |  | init -> samgr            |   |   |  | |
|  |  |  |  | -> foundation            |   |   |  | |
|  |  |  |  | SA 180: ability_manager  |   |   |  | |
|  |  |  |  | SA 401: bundle_manager   |   |   |  | |
|  |  |  |  | SA 501: app_manager      |   |   |  | |
|  |  |  |  | + 12 system services     |   |   |  | |
|  |  |  |  +--------------------------+   |   |  | |
|  |  |  +--------------------------------+   |  | |
|  |  +---------------------------------------+  | |
|  +---------------------------------------------+ |
+---------------------------------------------------+
```

## Key Results

| Metric | Value |
|--------|-------|
| Build targets compiled | 4,757 / 4,851 (98%) |
| System libraries | 466 shared libraries |
| Running services | 12+ (samgr, foundation, hilogd, accesstoken, etc.) |
| System abilities | SA 180, 401, 501 (ability, bundle, app manager) |
| Source patches | ~809 files across 157 projects (provided as diffs) |
| Boot stability | Stable, no crashes, clean shutdown |

## Quick Start

```bash
# 1. Clone this repo
git clone https://github.com/A2OH/openharmony-wsl.git
cd openharmony-wsl

# 2. Download and set up OHOS source (see docs/BUILD.md for full steps)
mkdir -p ~/openharmony && cd ~/openharmony
repo init -u https://gitee.com/openharmony/manifest.git -b master --no-repo-verify
repo sync -c -j8 --no-tags
bash build/prebuilts_download.sh

# 3. Apply patches
cd ~/openharmony-wsl
OHOS_ROOT=~/openharmony ./scripts/apply_patches.sh

# 4. Build
cd ~/openharmony
python3 build/hb/main.py build --product-name qemu-arm-linux-min --no-prebuilt-sdk

# 5. Prepare images
cd ~/openharmony-wsl
OHOS_ROOT=~/openharmony ./scripts/prepare_images.sh

# 6. Boot
OHOS_ROOT=~/openharmony ./scripts/qemu_boot.sh 0
```

## Repository Structure

```
openharmony-wsl/
+-- README.md                       <- This file (English)
+-- README_CN.md                    <- Chinese version
+-- LICENSE                         <- Apache 2.0
+-- scripts/
|   +-- prepare_images.sh           <- Generate ext4 images from build output
|   +-- qemu_boot.sh                <- Boot QEMU headless (serial console)
|   +-- qemu_boot_vnc.sh            <- Boot QEMU with VNC display
|   +-- inject_files.sh             <- Inject files into ext4 images via debugfs
|   +-- apply_patches.sh            <- Apply all patches to OHOS source tree
+-- patches/
|   +-- README.md                   <- Patch descriptions and manual apply guide
|   +-- 01-gn-defines-clobbering.diff   <- Fix defines= overwriting (639 files)
|   +-- 02-build-system-fixes.diff      <- ninja -k 0, loader warning downgrade
|   +-- 03-ruby-ostruct-compat.diff     <- Ruby 3.3+ require 'ostruct'
|   +-- 04-cpp-header-fixes.diff        <- Missing #include, SUPPORT_GRAPHICS guards
|   +-- 05-kernel-config.diff           <- Enable DRM_BOCHS for VNC
|   +-- 06-gn-component-guards.diff     <- Conditional compilation fixes
+-- docs/
    +-- BUILD.md                    <- Full build instructions (English)
    +-- BUILD_CN.md                 <- Full build instructions (Chinese)
    +-- QEMU.md                     <- QEMU setup, options, troubleshooting (EN)
    +-- QEMU_CN.md                  <- QEMU setup (Chinese)
    +-- TROUBLESHOOTING.md          <- Common issues and fixes (English)
    +-- TROUBLESHOOTING_CN.md       <- Common issues and fixes (Chinese)
```

## Approach: Diffs, Not Direct Patches

This repository does **not** contain modified OHOS source files. Instead, all
changes are provided as **diff files** that you apply to your own OHOS source
checkout. This approach:

- Keeps the repo small (~25K lines of diffs vs 80GB source tree)
- Works with any OHOS branch/version (diffs can be adapted)
- Does not redistribute OHOS source code
- Makes it clear exactly what changed and why

## Documentation

| Document | Language | Description |
|----------|----------|-------------|
| [docs/BUILD.md](docs/BUILD.md) | EN | Step-by-step build from source |
| [docs/BUILD_CN.md](docs/BUILD_CN.md) | CN | 从源码构建的详细步骤 |
| [docs/QEMU.md](docs/QEMU.md) | EN | QEMU options, boot modes, file injection |
| [docs/QEMU_CN.md](docs/QEMU_CN.md) | CN | QEMU 选项、启动模式、文件注入 |
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | EN | Common issues and solutions |
| [docs/TROUBLESHOOTING_CN.md](docs/TROUBLESHOOTING_CN.md) | CN | 常见问题和解决方案 |
| [patches/README.md](patches/README.md) | EN | Patch descriptions and apply guide |

## Prerequisites

| Requirement | Minimum | Recommended |
|-------------|---------|-------------|
| OS | Windows 10 21H2+ with WSL2 | Windows 11 |
| WSL2 distro | Ubuntu 22.04 | Ubuntu 24.04 |
| RAM | 16 GB | 32 GB |
| Disk space | 150 GB free | 250 GB free |
| CPU cores | 4 | 8+ |

```bash
sudo apt update && sudo apt install -y \
  build-essential git git-lfs python3 python3-pip \
  curl wget unzip zip scons ccache \
  libncurses5-dev libssl-dev bc flex bison \
  ruby gperf device-tree-compiler \
  default-jdk e2fsprogs qemu-system-arm
```

## What You Can Do

With the running OHOS system:

- **Install HAPs:** `bm install -p /path/to/app.hap`
- **Launch abilities:** `aa start -a MainAbility -b com.example.app`
- **Test IPC/binder:** Communicate between system abilities via samgr
- **Deploy native binaries:** Inject via debugfs, run on ARM32 OHOS
- **Develop OH system services:** Test without hardware
- **Cross-compile and test:** Build ARM32 binaries on x86_64, test immediately

## Available System Libraries

Key libraries confirmed present:

| Library | Purpose |
|---------|---------|
| `libhilog_ndk.z.so` | Logging framework |
| `libnative_rdb_ndk.z.so` | Relational database |
| `libnative_preferences.z.so` | Key-value preferences |
| `libace_napi.z.so` | ArkUI N-API bindings |
| `libabilityms.z.so` | Ability lifecycle management |
| `libbms.z.so` | HAP install/manage |
| `libappms.z.so` | App process management |

## Known Limitations

| Limitation | Impact | Notes |
|-----------|--------|-------|
| ARM32 only | Medium | ARM64 QEMU target planned |
| No GPU acceleration | Medium | Software rendering only; OH_Drawing unavailable |
| No KVM on x86_64 host | Medium | TCG emulation is slower but functional |
| First boot ~60s | Low | Subsequent boots faster |

## Contributing

Contributions welcome. Key areas:

1. **ARM64 QEMU target** -- extend build to 64-bit ARM
2. **GPU passthrough** -- enable OH_Drawing in QEMU
3. **Automated CI** -- headless boot + smoke test in GitHub Actions
4. **Upstream patches** -- get fixes accepted into OpenHarmony mainline

## License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for details.

OpenHarmony is a project of the OpenAtom Foundation. This repository is an
independent community effort and is not officially affiliated with or endorsed
by the OpenAtom Foundation.
