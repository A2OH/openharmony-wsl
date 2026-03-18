# Troubleshooting

Common issues and solutions when building and running OpenHarmony on WSL2.

## Build Issues

### "defines" variable clobbered (hundreds of errors)

**Symptom:** Massive number of errors like:
```
ERROR: Assignment had no effect
```
or unexpected undefined symbols during compilation.

**Cause:** GN build files use `defines = [...]` which overwrites previously set
defines. Should be `defines += [...]`.

**Fix:** Apply patch 01:
```bash
OHOS_ROOT=~/openharmony ./scripts/apply_patches.sh
```

Or fix manually:
```bash
cd ~/openharmony
find . -name "*.gn" -not -path "./out/*" -not -path "./.repo/*" \
  -exec grep -l 'defines = \[' {} \; | while read f; do
    sed -i 's/defines = \[/defines += \[/g' "$f"
done
```

### Build stops at first error

**Symptom:** Ninja stops after the first compilation failure, even though
most targets would succeed.

**Cause:** Default ninja behavior is to stop on first error.

**Fix:** Apply patch 02 (adds `-k 0` to ninja), or manually edit:
```bash
# In build/hb/services/ninja.py, add '-k', '0' to ninja_cmd
```

### Ruby error: "uninitialized constant OpenStruct"

**Symptom:**
```
NameError: uninitialized constant OpenStruct
```

**Cause:** Ruby 3.3+ removed auto-require of `ostruct`.

**Fix:** Apply patch 03, or manually add `require 'ostruct'` to the failing
Ruby scripts in `arkcompiler/runtime_core/`.

### Missing \#include \<cstdint\>

**Symptom:**
```
error: 'uint32_t' was not declared in this scope
```

**Cause:** GCC 13+ is stricter about implicit includes.

**Fix:** Apply patch 04, or add `#include <cstdint>` to the failing files.

### Component mismatch exception

**Symptom:**
```
Exception: subsystem 'xxx' component 'yyy' not in subsystem_config
```

**Cause:** Strict validation in the loader rejects valid configurations.

**Fix:** Apply patch 02 (downgrades exception to warning), or edit:
```python
# In build/hb/util/loader/load_ohos_build.py line 641:
# Change: raise Exception(message)
# To:     print(f"Warning: {message}")
```

### Disk space exhaustion

**Symptom:** Build fails with "No space left on device".

**Cause:** OpenHarmony source + build output requires 150-200 GB.

**Fix:**
```bash
# Check disk usage
df -h ~

# Clean build artifacts (keeps source)
rm -rf ~/openharmony/out

# Or expand WSL2 disk:
# In PowerShell (admin):
wsl --shutdown
# Then resize the VHD
```

### Out of memory

**Symptom:** Compiler or linker killed by OOM.

**Cause:** WSL2 default memory limit may be too low.

**Fix:** Create/edit `%UserProfile%\.wslconfig` on Windows:
```ini
[wsl2]
memory=24GB
processors=8
swap=8GB
```
Then restart WSL: `wsl --shutdown` in PowerShell.

## QEMU Boot Issues

### "No such file" errors for images

**Symptom:**
```
ERROR: system.img not found
```

**Cause:** Images not generated, or OHOS_ROOT not set correctly.

**Fix:**
```bash
# Make sure images exist
ls ~/openharmony/out/qemu-arm-linux/packages/phone/images/

# If missing, run prepare_images.sh
OHOS_ROOT=~/openharmony ./scripts/prepare_images.sh
```

### QEMU hangs at "Booting Linux"

**Symptom:** Kernel starts but hangs before init.

**Cause:** Usually a drive order issue. Virtio-mmio enumerates in reverse order.

**Fix:** Ensure drive order in the QEMU command matches:
1. userdata (becomes vdd)
2. vendor (becomes vdc)
3. system (becomes vdb)
4. updater (becomes vda)

### "init: mount required partition failed"

**Symptom:**
```
init: MountRequiredPartitions failed
```

**Cause:** The ext4 images use features incompatible with the QEMU kernel,
or the block device mapping is wrong.

**Fix:** Ensure images are created with `^64bit,^metadata_csum`:
```bash
mke2fs -t ext4 -b 4096 -I 256 -O ^64bit,^metadata_csum ...
```

The `prepare_images.sh` script handles this automatically.

### Black screen in VNC mode

**Symptom:** VNC connects but shows only a black screen.

**Cause:** Missing DRM/framebuffer driver in kernel.

**Fix:** Apply patch 05 (enables CONFIG_DRM_BOCHS), or rebuild kernel with:
```
CONFIG_DRM_BOCHS=y
```

### Foundation crashes on boot

**Symptom:**
```
foundation: critical service restart limit reached
```

**Cause:** By default, `foundation` is marked as critical. If it crashes
(e.g., missing dependencies), init will reboot the system.

**Fix:** The `prepare_images.sh` script sets `"critical":[0]` for foundation.
If you regenerated images without this script, run it again.

### Services not starting

**Symptom:** `samgr` is running but system abilities (SA 180, 401, 501) are
not registered.

**Cause:** Missing service profiles or libraries.

**Fix:**
```bash
# Check if foundation.json exists
debugfs system.img -R "stat /profile/foundation.json" 2>/dev/null

# Check if service libraries exist
debugfs system.img -R "ls /lib" 2>/dev/null | grep -E "abilityms|bms|appms"
```

If missing, ensure your product config includes the relevant subsystems
(ability framework, bundle manager, etc.) and rebuild.

## WSL2-Specific Issues

### Slow file I/O

**Symptom:** Build takes much longer than expected.

**Cause:** WSL2 filesystem performance varies. Files on Windows mounts
(`/mnt/c/`) are extremely slow.

**Fix:** Always work within the Linux filesystem (`~/`), never on `/mnt/c/`.

### Port forwarding for VNC

**Symptom:** Cannot connect to VNC from Windows.

**Cause:** WSL2 runs in a VM with its own IP.

**Fix:**
```bash
# Find WSL2 IP
hostname -I

# Or use localhost forwarding (newer WSL2 versions do this automatically)
# In .wslconfig:
[wsl2]
networkingMode=mirrored
```

### System clock issues

**Symptom:** Build errors related to timestamps, or `repo sync` SSL errors.

**Fix:**
```bash
sudo hwclock -s
# Or
sudo ntpdate pool.ntp.org
```
