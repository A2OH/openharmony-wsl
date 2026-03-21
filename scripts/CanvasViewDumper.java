import android.app.Activity;
import android.app.MiniServer;
import android.app.MiniActivityManager;
import android.content.Intent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.graphics.Canvas;
import android.graphics.Bitmap;
import java.io.*;

/**
 * ViewDumper that uses Canvas.draw() for real pixel rendering.
 * The Canvas calls go through OHBridge JNI to the software renderer,
 * which produces pixels in /data/a2oh/canvas_pixels.raw.
 * Also outputs RECT geometry for fallback rendering.
 */
public class CanvasViewDumper {
    static void out(String s) {
        try {
            byte[] b = new byte[s.length() + 1];
            for (int i = 0; i < s.length(); i++)
                b[i] = (byte)(s.charAt(i) & 0x7F);
            b[s.length()] = (byte)'\n';
            FileOutputStream fos = new FileOutputStream("/data/a2oh/viewdump.txt", true);
            fos.write(b);
            fos.close();
        } catch (Exception e) {}
    }

    public static void main(String[] args) {
        try { new FileOutputStream("/data/a2oh/viewdump.txt").close(); } catch (Exception e) {}

        out("=== CanvasViewDumper ===");

        String appPkg = "com.example.mockdonalds";
        String activityClass = "com.example.mockdonalds.MenuActivity";
        if (args.length >= 2) {
            appPkg = args[0];
            activityClass = args[1];
        } else if (args.length >= 1) {
            activityClass = args[0];
            int dot = activityClass.lastIndexOf('.');
            if (dot > 0) {
                int dot2 = activityClass.lastIndexOf('.', dot - 1);
                if (dot2 > 0) appPkg = activityClass.substring(0, dot);
            }
        }

        Activity activity = null;
        try {
            MiniServer.init(appPkg);
            MiniActivityManager am = MiniServer.get().getActivityManager();
            Intent intent = new Intent();
            intent.setClassName(appPkg, activityClass);
            am.startActivity(null, intent, 0);
            activity = am.getResumedActivity();
            if (activity == null) { out("ERROR: no activity"); return; }
            out("ACTIVITY: " + activity.getClass().getName());
        } catch (Throwable t) {
            out("ERROR_INIT: " + t.getClass().getName() + " " + t.getMessage());
            return;
        }

        View root = null;
        try {
            root = activity.getWindow().getDecorView();
            if (root == null) { out("ERROR: no root view"); return; }
            out("ROOT: " + root.getClass().getName());
        } catch (Throwable t) {
            out("ERROR_ROOT: " + t.getClass().getName());
            return;
        }

        int screenW = 1280, screenH = 800;

        /* Apply default theme (widget backgrounds, colors) */
        try {
            android.app.DefaultTheme.applyToViewTree(root);
            out("THEME_APPLIED");
        } catch (Throwable t) {
            out("THEME_ERROR: " + t.getClass().getName());
        }

        /* Measure with unlimited height to get full content */
        try {
            int wSpec = View.MeasureSpec.makeMeasureSpec(screenW, View.MeasureSpec.EXACTLY);
            int hSpec = View.MeasureSpec.makeMeasureSpec(4000, View.MeasureSpec.AT_MOST);
            root.measure(wSpec, hSpec);
            int contentH = root.getMeasuredHeight();
            if (contentH > screenH) screenH = contentH;
            out("CONTENT_HEIGHT " + contentH);
            root.layout(0, 0, screenW, screenH);
            out("MEASURE_START");
            out("MEASURE_DONE");
            out("LAYOUT_DONE");
        } catch (Throwable t) {
            out("ERROR_MEASURE: " + t.getClass().getName() + " " + t.getMessage());
        }

        /* Canvas draw — produces real pixels via software renderer */
        try {
            out("CANVAS_DRAW_START");
            Bitmap bitmap = Bitmap.createBitmap(screenW, screenH, Bitmap.Config.ARGB_8888);
            Canvas canvas = new Canvas(bitmap);
            /* Fill with dark background before View.draw — Views have transparent bg by default */
            canvas.drawColor(0xFFF5F5F5);
            root.draw(canvas);
            out("CANVAS_DRAW_DONE");

            /* Write pixels from Java (C-side open() broken on this platform) */
            long bmpHandle = bitmap.getNativeHandle();
            int bw = com.ohos.shim.bridge.OHBridge.bitmapGetWidth(bmpHandle);
            int bh = com.ohos.shim.bridge.OHBridge.bitmapGetHeight(bmpHandle);
            out("CANVAS_WRITE " + bw + "x" + bh);
            if (bw > 0 && bh > 0) {
                FileOutputStream fos = new FileOutputStream("/data/a2oh/canvas_pixels.raw");
                /* Header */
                byte[] hdr = new byte[8];
                hdr[0]=(byte)(bw&0xFF); hdr[1]=(byte)((bw>>8)&0xFF);
                hdr[2]=(byte)((bw>>16)&0xFF); hdr[3]=(byte)((bw>>24)&0xFF);
                hdr[4]=(byte)(bh&0xFF); hdr[5]=(byte)((bh>>8)&0xFF);
                hdr[6]=(byte)((bh>>16)&0xFF); hdr[7]=(byte)((bh>>24)&0xFF);
                fos.write(hdr);
                /* Write 8 rows at a time to reduce JNI call overhead */
                int BATCH = 8;
                byte[] buf = new byte[bw * 4 * BATCH];
                for (int y = 0; y < bh; y += BATCH) {
                    int rows = (y + BATCH <= bh) ? BATCH : (bh - y);
                    for (int r = 0; r < rows; r++) {
                        for (int x = 0; x < bw; x++) {
                            int px = com.ohos.shim.bridge.OHBridge.bitmapGetPixel(bmpHandle, x, y + r);
                            int off = (r * bw + x) * 4;
                            buf[off]   = (byte)(px & 0xFF);
                            buf[off+1] = (byte)((px>>8) & 0xFF);
                            buf[off+2] = (byte)((px>>16) & 0xFF);
                            buf[off+3] = (byte)((px>>24) & 0xFF);
                        }
                    }
                    fos.write(buf, 0, rows * bw * 4);
                }
                fos.close();
                out("CANVAS_PIXELS_WRITTEN");

                /* Blit to /dev/fb0 if available (full OHOS with DRM) */
                try {
                    java.io.File fb = new java.io.File("/dev/fb0");
                    if (fb.exists()) {
                        int fbW = 1280, fbH = 800; /* VNC framebuffer size */
                        FileOutputStream fbos = new FileOutputStream("/dev/fb0");
                        int scrollY = 0;
                        int maxScroll = (bh > fbH) ? (bh - fbH) : 0;
                        /* Write visible viewport */
                        byte[] fbBuf = new byte[fbW * 4];
                        for (int y = 0; y < fbH; y++) {
                            int sy = y + scrollY;
                            if (sy < bh) {
                                for (int x = 0; x < fbW && x < bw; x++) {
                                    int px = com.ohos.shim.bridge.OHBridge.bitmapGetPixel(bmpHandle, x, sy);
                                    int off = x * 4;
                                    fbBuf[off]   = (byte)(px & 0xFF);
                                    fbBuf[off+1] = (byte)((px>>8) & 0xFF);
                                    fbBuf[off+2] = (byte)((px>>16) & 0xFF);
                                    fbBuf[off+3] = (byte)((px>>24) & 0xFF);
                                }
                            }
                            fbos.write(fbBuf, 0, fbW * 4);
                        }
                        fbos.close();
                        out("FB0_BLIT_DONE " + fbW + "x" + fbH);

                        /* Input scroll loop — read mouse wheel events */
                        java.io.FileInputStream fis = null;
                        for (int i = 0; i < 10; i++) {
                            try {
                                fis = new java.io.FileInputStream("/dev/input/event" + i);
                                break;
                            } catch (Throwable ignore) {}
                        }
                        if (fis != null) {
                            out("INPUT_LOOP_START");
                            byte[] evBuf = new byte[16]; /* struct input_event: sec(4) + usec(4) + type(2) + code(2) + value(4) */
                            while (true) {
                                if (fis.read(evBuf) == 16) {
                                    int evType = (evBuf[8] & 0xFF) | ((evBuf[9] & 0xFF) << 8);
                                    int evCode = (evBuf[10] & 0xFF) | ((evBuf[11] & 0xFF) << 8);
                                    int evValue = (evBuf[12] & 0xFF) | ((evBuf[13] & 0xFF) << 8) |
                                                  ((evBuf[14] & 0xFF) << 16) | ((evBuf[15] & 0xFF) << 24);
                                    /* EV_REL=2, REL_WHEEL=8 */
                                    if (evType == 2 && evCode == 8) {
                                        scrollY -= evValue * 60;
                                        if (scrollY < 0) scrollY = 0;
                                        if (scrollY > maxScroll) scrollY = maxScroll;
                                        /* Re-blit at new scroll position */
                                        java.io.RandomAccessFile raf = new java.io.RandomAccessFile("/dev/fb0", "rw");
                                        for (int fy = 0; fy < fbH; fy++) {
                                            int sy2 = fy + scrollY;
                                            if (sy2 < bh) {
                                                for (int x = 0; x < fbW && x < bw; x++) {
                                                    int px = com.ohos.shim.bridge.OHBridge.bitmapGetPixel(bmpHandle, x, sy2);
                                                    int off = x * 4;
                                                    fbBuf[off]   = (byte)(px & 0xFF);
                                                    fbBuf[off+1] = (byte)((px>>8) & 0xFF);
                                                    fbBuf[off+2] = (byte)((px>>16) & 0xFF);
                                                    fbBuf[off+3] = (byte)((px>>24) & 0xFF);
                                                }
                                            }
                                            raf.write(fbBuf, 0, fbW * 4);
                                        }
                                        raf.close();
                                    }
                                }
                            }
                        }
                    }
                } catch (Throwable t) {
                    out("FB0_ERROR: " + t.getClass().getName() + " " + t.getMessage());
                }
            }
        } catch (Throwable t) {
            out("CANVAS_ERROR: " + t.getClass().getName() + " " + t.getMessage());
        }

        /* Also dump RECT geometry as fallback */
        try {
            out("SCREEN " + screenW + " " + screenH);
            dumpView(root, 0, 0);
            out("=== END ===");
        } catch (Throwable t) {
            out("DUMP_ERROR: " + t.getClass().getName());
        }
    }

    static void dumpView(View v, int offsetX, int offsetY) {
        int x = offsetX + v.getLeft();
        int y = offsetY + v.getTop();
        int w = v.getWidth();
        int h = v.getHeight();
        if (w <= 0 || h <= 0) return;

        int bgColor = 0;
        String type = v.getClass().getSimpleName();

        if (v instanceof Button) bgColor = 0xFF4CAF50;
        else if (v instanceof TextView) bgColor = 0xFF212121;
        else if (v instanceof ListView) bgColor = 0xFF303030;
        else if (v instanceof LinearLayout) bgColor = 0xFFF5F5F5;

        String text = "";
        if (v instanceof TextView) {
            try {
                CharSequence t = ((TextView)v).getText();
                if (t != null) text = t.toString().replace("\n", " ");
            } catch (Throwable t) { text = "?"; }
        }

        out("RECT " + x + " " + y + " " + w + " " + h + " " +
            Integer.toHexString(bgColor) + " " + type + " " + text);

        if (v instanceof ViewGroup) {
            ViewGroup vg = (ViewGroup) v;
            for (int i = 0; i < vg.getChildCount(); i++) {
                try { dumpView(vg.getChildAt(i), x, y); }
                catch (Throwable t) { out("ERROR_CHILD: " + t.getClass().getName()); }
            }
        }
    }
}
