# Building OpenHarmony on WSL2

Complete step-by-step guide to building OpenHarmony from source on Windows
WSL2 and booting it in QEMU ARM32 emulation.

**Time estimate:** 3-5 hours for first build (depends on CPU and network speed).

## Prerequisites

### System Requirements

| Requirement | Minimum | Recommended |
|-------------|---------|-------------|
| OS | Windows 10 21H2+ with WSL2 | Windows 11 |
| WSL2 distro | Ubuntu 22.04 | Ubuntu 24.04 |
| RAM | 16 GB | 32 GB |
| Disk space | 150 GB free | 250 GB free |
| CPU cores | 4 | 8+ |

### Step 0: Verify WSL2

In Windows PowerShell:
```powershell
wsl --version
# Expected: WSL version 2.x.x.x
```

If not installed:
```powershell
wsl --install -d Ubuntu-24.04
```

### Step 1: Install Dependencies (~5 minutes)

```bash
sudo apt update && sudo apt install -y \
  build-essential git git-lfs python3 python3-pip \
  curl wget unzip zip scons ccache \
  libncurses5-dev libssl-dev bc flex bison \
  ruby gperf device-tree-compiler \
  default-jdk e2fsprogs \
  qemu-system-arm

# Verify key tools
git --version          # Expected: 2.x+
python3 --version      # Expected: 3.10+
ruby --version         # Expected: 3.0+
qemu-system-arm --version  # Expected: 7.2+
javac -version         # Expected: 11+
```

**Expected output:** All version checks pass. If ruby is 3.3+, patch 03
(ostruct) will be needed.

### Step 2: Install repo tool (~1 minute)

```bash
mkdir -p ~/bin
curl https://storage.googleapis.com/git-repo-downloads/repo > ~/bin/repo
chmod a+x ~/bin/repo
echo 'export PATH=~/bin:$PATH' >> ~/.bashrc
source ~/.bashrc
repo version
# Expected: repo version v2.x
```

### Step 3: Configure Git

```bash
git config --global user.email "you@example.com"
git config --global user.name "Your Name"
git config --global color.ui false
```

### Step 4: Download OpenHarmony Source (~60-90 minutes)

```bash
mkdir -p ~/openharmony && cd ~/openharmony
repo init -u https://gitee.com/openharmony/manifest.git -b master --no-repo-verify
repo sync -c -j$(nproc) --no-tags
```

**Expected output:** After sync completes, `ls` shows directories like
`base/`, `build/`, `foundation/`, `kernel/`, etc. Total size ~80-120 GB.

**Tip:** If sync fails partway through, just re-run `repo sync -c -j4 --no-tags`.
It resumes where it left off.

### Step 5: Download prebuilts (~15-30 minutes)

```bash
cd ~/openharmony
bash build/prebuilts_download.sh
```

**Expected output:** Downloads toolchains (clang, node, python) into
`prebuilts/` directory. ~10-20 GB additional.

## Building

### Step 6: Apply Patches (~30 seconds)

```bash
# Clone this repository
cd ~
git clone https://github.com/A2OH/openharmony-wsl.git

# Apply all patches
cd ~/openharmony-wsl
OHOS_ROOT=~/openharmony ./scripts/apply_patches.sh
```

If automatic application fails for some patches (due to repo-per-project
structure), apply them manually. See `patches/README.md` for details.

**Alternative: apply the most critical patch manually**

The single most impactful patch is 01 (GN defines clobbering). You can also
make this change with a one-liner:

```bash
cd ~/openharmony
# Fix defines= to defines+= across all .gn files
find . -name "*.gn" -not -path "./out/*" -not -path "./.repo/*" \
  -exec grep -l 'defines = \[' {} \; | while read f; do
    sed -i 's/defines = \[/defines += \[/g' "$f"
done
```

### Step 7: Build (~45-120 minutes)

```bash
cd ~/openharmony
python3 build/hb/main.py build \
  --product-name qemu-arm-linux-min \
  --no-prebuilt-sdk
```

**Expected output:**
```
[OHOS INFO] build success
[OHOS INFO] cost time: ...
```

Build output goes to `out/qemu-arm-linux/`. Key artifacts:
- `packages/phone/images/zImage-dtb` - Kernel
- `packages/phone/images/ramdisk.img` - Initial ramdisk
- `packages/phone/system/` - System root filesystem

**If build fails:** See [TROUBLESHOOTING.md](TROUBLESHOOTING.md). The `-k 0`
flag (from patch 02) ensures ninja keeps going past individual failures.
Check which targets failed:

```bash
# Count successes vs failures
grep -c "FAILED:" out/qemu-arm-linux/build.*.log | tail -1
```

### Step 8: Prepare QEMU Images (~1 minute)

```bash
cd ~/openharmony-wsl
OHOS_ROOT=~/openharmony ./scripts/prepare_images.sh
```

**Expected output:**
```
=== Preparing images for QEMU boot ===
[1/7] Creating mount points...
[2/7] Creating /system symlinks...
[3/7] Configuring init services...
  console: {'disabled': 0, 'start-mode': 'boot', 'ondemand': False}
  hdcd: {'disabled': 0, 'start-mode': 'boot'}
  foundation: {'critical': [0]}
[4/7] Checking foundation.json...
[5/7] Creating system.img (25600 blocks)...
[6/7] Creating userdata.img (25600 blocks)...
[7/7] Creating vendor.img (10240 blocks)...
=== Done! Images ready ===
```

## Booting

### Step 9: Boot QEMU (~30-60 seconds to full boot)

**Headless (serial console):**
```bash
OHOS_ROOT=~/openharmony ./scripts/qemu_boot.sh 0
```

**With VNC display:**
```bash
OHOS_ROOT=~/openharmony ./scripts/qemu_boot_vnc.sh 0
# Then connect: vncviewer localhost:5900
```

**Expected output (serial console):**
```
Booting Linux on physical CPU 0x0
...
[    2.xxx] init: OpenHarmony init started
...
[    5.xxx] samgr: service manager ready
...
[   15.xxx] foundation: system abilities loaded
```

### Step 10: Verify

Once booted, you should see system services running:

```bash
# In the QEMU console:
cat /proc/version
# Expected: Linux version 5.10.184 ...

ls /system/bin/
# Expected: hdcd, samgr, foundation, hilogd, etc.

# Check running services
ps -ef | grep -E "samgr|foundation|hilog"
```

## Product Configuration

The default product config is `qemu-arm-linux-min` which includes these
subsystems:

| Subsystem | Components |
|-----------|------------|
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

The config file is at:
`vendor/ohemu/qemu_arm_linux_min/config.json`

To add more subsystems (e.g., ability framework), edit this file and rebuild.

## Build Artifacts

After a successful build, key files are at:

```
out/qemu-arm-linux/
├── packages/phone/
│   ├── images/
│   │   ├── zImage-dtb          # Linux kernel with device tree
│   │   ├── ramdisk.img         # Initial RAM disk
│   │   ├── system.img          # System partition (ext4)
│   │   ├── vendor.img          # Vendor partition (ext4)
│   │   ├── userdata.img        # User data partition (ext4)
│   │   └── updater.img         # Updater partition (ext4)
│   └── system/                 # Unpacked system root
│       ├── bin/                # System binaries
│       ├── lib/                # Shared libraries (466+)
│       ├── etc/                # Configuration files
│       └── profile/            # Service profiles
├── clang_x64/                  # Host tools built during compilation
└── *.log                       # Build logs
```
