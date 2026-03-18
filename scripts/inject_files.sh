#!/bin/bash
# Inject files into ext4 QEMU images without rebuilding
#
# Uses debugfs to write files directly into ext4 images.
# Useful for deploying test binaries, config changes, etc.
#
# Usage:
#   ./inject_files.sh <image_path> <local_file> <target_path_in_image>
#
# Examples:
#   # Inject a binary into system.img
#   ./inject_files.sh out/images/system.img ./my_binary /bin/my_binary
#
#   # Inject a config file
#   ./inject_files.sh out/images/system.img ./test.cfg /etc/init/test.cfg
#
# Prerequisites:
#   sudo apt install e2fsprogs

set -e

if [ $# -lt 3 ]; then
    echo "Usage: $0 <image_path> <local_file> <target_path_in_image>"
    echo ""
    echo "Examples:"
    echo "  $0 system.img ./dalvikvm /bin/dalvikvm"
    echo "  $0 system.img ./test.cfg /etc/init/test.cfg"
    exit 1
fi

IMAGE="$1"
LOCAL_FILE="$2"
TARGET_PATH="$3"

if [ ! -f "$IMAGE" ]; then
    echo "ERROR: Image file not found: $IMAGE"
    exit 1
fi

if [ ! -f "$LOCAL_FILE" ]; then
    echo "ERROR: Local file not found: $LOCAL_FILE"
    exit 1
fi

# Check for debugfs
if ! command -v debugfs &> /dev/null; then
    echo "ERROR: debugfs not found. Install with: sudo apt install e2fsprogs"
    exit 1
fi

# Create parent directory in image if needed
TARGET_DIR=$(dirname "$TARGET_PATH")
if [ "$TARGET_DIR" != "/" ]; then
    echo "Ensuring directory exists: $TARGET_DIR"
    debugfs -w "$IMAGE" -R "mkdir $TARGET_DIR" 2>/dev/null || true
fi

# Write file into image
echo "Injecting: $LOCAL_FILE -> $TARGET_PATH (in $IMAGE)"
debugfs -w "$IMAGE" -R "write $LOCAL_FILE $TARGET_PATH"

# Verify
echo "Verifying..."
debugfs "$IMAGE" -R "stat $TARGET_PATH" 2>/dev/null | head -5

echo "Done. File injected successfully."
