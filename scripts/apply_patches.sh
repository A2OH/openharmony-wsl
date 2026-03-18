#!/bin/bash
# Apply all OpenHarmony source patches for WSL2/QEMU build
#
# This script applies patches to the OHOS source tree to fix compilation
# issues encountered when building on WSL2 (Ubuntu 24.04).
#
# Usage:
#   OHOS_ROOT=~/openharmony ./apply_patches.sh [--dry-run]
#
# Patches applied:
#   01 - GN defines= clobbering (639 files) - fixes `defines =` overwriting previous values
#   02 - Build system fixes (ninja -k 0, loader warning downgrade)
#   03 - Ruby require 'ostruct' compatibility (Ruby 3.3+)
#   04 - C++ header fixes (#include <cstdint>, SUPPORT_GRAPHICS guards)
#   05 - Kernel defconfig (enable DRM_BOCHS for VNC display)
#   06 - GN component guard fixes (conditional compilation)
#
# Time estimate: ~30 seconds

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PATCH_DIR="$SCRIPT_DIR/../patches"
OHOS_ROOT="${OHOS_ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
DRY_RUN=""

if [ "$1" = "--dry-run" ]; then
    DRY_RUN="--dry-run"
    echo "=== DRY RUN MODE - no files will be modified ==="
fi

if [ ! -d "$OHOS_ROOT/build" ] || [ ! -d "$OHOS_ROOT/base" ]; then
    echo "ERROR: OHOS source tree not found at $OHOS_ROOT"
    echo "Set OHOS_ROOT to point to your OpenHarmony source directory."
    exit 1
fi

echo "=== Applying patches to: $OHOS_ROOT ==="
echo ""

TOTAL=0
FAILED=0

for patch in "$PATCH_DIR"/*.diff; do
    name=$(basename "$patch")
    lines=$(wc -l < "$patch")
    hunks=$(grep -c '^diff --git' "$patch" 2>/dev/null || echo 0)

    echo "--- Applying: $name ($hunks file hunks, $lines lines) ---"

    # These patches use repo-relative paths and need to be applied from OHOS_ROOT
    # The patches contain project markers that need to be filtered
    # Use --ignore-whitespace and -p1 for standard git diff format
    cd "$OHOS_ROOT"

    # Filter out comment lines (# Project: ...) before applying
    grep -v '^# Project:' "$patch" | grep -v '^--- a/' | grep -v '^+++ b/' > /tmp/_filtered_patch.diff 2>/dev/null || true

    if git apply $DRY_RUN --stat "$patch" 2>/dev/null; then
        git apply $DRY_RUN "$patch" 2>/dev/null && echo "  OK" || {
            echo "  PARTIAL - applying with --reject"
            git apply $DRY_RUN --reject "$patch" 2>/dev/null || true
            FAILED=$((FAILED + 1))
        }
    else
        echo "  NOTE: Patch uses repo-per-project format."
        echo "  Apply manually per project (see patches/README.md for instructions)."
        FAILED=$((FAILED + 1))
    fi

    TOTAL=$((TOTAL + 1))
    echo ""
done

echo "=== Summary: $TOTAL patches processed, $FAILED need manual application ==="
if [ $FAILED -gt 0 ]; then
    echo ""
    echo "For patches that failed automatic application, apply them manually:"
    echo "  cd \$OHOS_ROOT/<project_dir>"
    echo "  git apply <patch_file>"
    echo ""
    echo "See patches/README.md for per-project instructions."
fi
