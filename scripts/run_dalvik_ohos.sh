#!/system/bin/sh
export ANDROID_DATA=/data/a2oh
export ANDROID_ROOT=/data/a2oh
export LD_LIBRARY_PATH=/system/lib/platformsdk:/system/lib/chipset-pub-sdk:/system/lib/chipset-sdk:/system/lib:$LD_LIBRARY_PATH
mkdir -p /data/a2oh/dalvik-cache
cd /data/a2oh
chmod 755 dalvikvm dexopt
./dalvikvm -Xverify:none -Xdexopt:none \
  -Xbootclasspath:/data/a2oh/core.jar:/data/a2oh/app.dex \
  -classpath /data/a2oh/app.dex \
  CanvasViewDumper
echo "=== Dalvik exit: $? ==="
ls -la /data/a2oh/viewdump.txt /data/a2oh/canvas_pixels.raw /data/a2oh/canvas_ops.txt 2>/dev/null
