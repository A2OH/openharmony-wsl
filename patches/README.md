# OpenHarmony Source Patches for WSL2 Build

These patches fix compilation issues encountered when building OpenHarmony
(master branch, March 2025) on WSL2 Ubuntu 24.04 with the `qemu-arm-linux-min`
product target.

## Patch Summary

| Patch | Files | Description |
|-------|-------|-------------|
| `01-gn-defines-clobbering.diff` | 639 | Fix GN `defines =` overwriting inherited values (should be `defines +=`) |
| `02-build-system-fixes.diff` | 2 | Add `ninja -k 0` (keep going on errors), downgrade loader exception to warning |
| `03-ruby-ostruct-compat.diff` | 17 | Add `require 'ostruct'` for Ruby 3.3+ compatibility |
| `04-cpp-header-fixes.diff` | 146 | Add missing `#include <cstdint>`, `SUPPORT_GRAPHICS` guards, type fixes |
| `05-kernel-config.diff` | 1 | Enable `CONFIG_DRM_BOCHS=y` for QEMU VNC framebuffer |
| `06-gn-component-guards.diff` | 4 | Fix conditional compilation for optional components |

**Total: ~809 files across 157 projects**

## Applying Patches

### Automatic (recommended)

```bash
OHOS_ROOT=~/openharmony ../scripts/apply_patches.sh
```

### Manual (per-project)

Because OpenHarmony uses `repo` with separate git repositories per project,
patches are organized by project. Each patch file contains project markers:

```
# Project: project_name (project/path)
diff --git a/file.gn b/file.gn
...
```

To apply a specific project's changes:

```bash
cd ~/openharmony/<project_path>
# Extract the relevant section from the patch and apply
git apply /path/to/patch.diff
```

### Dry Run

To preview what would change without modifying files:

```bash
OHOS_ROOT=~/openharmony ../scripts/apply_patches.sh --dry-run
```

## Patch Details

### 01-gn-defines-clobbering.diff

**Root cause:** GN build files throughout OHOS use `defines = [...]` inside
`config()` blocks. When multiple configs are applied to a target, later configs
overwrite earlier ones. The fix changes `defines = [...]` to `defines += [...]`
so defines accumulate correctly.

**Affected subsystems:** build, arkcompiler, foundation, base, drivers, and
~150 other projects.

**Example:**
```gn
# Before (broken - overwrites previous defines):
config("my_config") {
  defines = [ "MY_DEFINE" ]
}

# After (correct - appends to existing defines):
config("my_config") {
  defines += [ "MY_DEFINE" ]
}
```

### 02-build-system-fixes.diff

Two changes to the build harness:

1. **ninja.py**: Add `-k 0` flag so ninja continues building after errors.
   Without this, a single failing target stops the entire build.

2. **load_ohos_build.py**: Downgrade a component mismatch error to a warning.
   The strict check fails on valid configurations when subsystem definitions
   have minor inconsistencies.

### 03-ruby-ostruct-compat.diff

**Root cause:** Ruby 3.3 moved `OpenStruct` out of the default loaded libraries.
Scripts in arkcompiler_runtime_core use `OpenStruct` without `require 'ostruct'`.
Also fixes `File.exists?` (removed in Ruby 3.2) to `File.exist?`.

### 04-cpp-header-fixes.diff

Several categories of C++ fixes:

- **Missing `#include <cstdint>`**: Some files use `uint32_t` etc. without
  including the header. GCC 13+ is stricter about this.
- **SUPPORT_GRAPHICS guards**: Code that references UI/graphics types needs
  `#ifdef SUPPORT_GRAPHICS` guards for headless builds.
- **Type fixes**: Minor type mismatches caught by newer compilers.

### 05-kernel-config.diff

Enables `CONFIG_DRM_BOCHS=y` in the QEMU ARM kernel defconfig. This provides
a framebuffer device in QEMU for VNC display output. Without it, VNC mode
shows a black screen.

### 06-gn-component-guards.diff

Fixes GN files that reference optional components without proper
`defined(global_parts_info.component_name)` guards. When building with a
minimal product config (qemu-arm-linux-min), many components are absent.
