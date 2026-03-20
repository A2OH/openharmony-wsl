import android.app.Activity;
import android.app.MiniServer;
import android.app.MiniActivityManager;
import android.content.Intent;
import android.view.View;
import android.view.ViewGroup;
import android.view.MotionEvent;
import android.graphics.Canvas;
import android.graphics.Bitmap;
import java.io.*;

/**
 * Persistent Android app runner with event loop.
 *
 * 1. Launches Activity, measure/layout/draw
 * 2. Flushes pixels to canvas_pixels.raw
 * 3. Enters event loop: reads touch_input.txt, dispatches MotionEvent,
 *    re-renders on invalidate
 * 4. Outputs RECT geometry to viewdump.txt (fallback rendering)
 */
public class AppRunner {

    static final String TOUCH_INPUT  = "/data/a2oh/touch_input.txt";
    static final String CANVAS_OUT   = "/data/a2oh/canvas_pixels.raw";
    static final String VIEWDUMP_OUT = "/data/a2oh/viewdump.txt";
    static final String FRAME_READY  = "/data/a2oh/frame_ready";
    static final int SCREEN_W = 1280, SCREEN_H = 800;

    static View rootView;
    static Bitmap frameBitmap;
    static Canvas frameCanvas;
    static int frameCount = 0;

    static void out(String s) {
        try {
            byte[] b = new byte[s.length() + 1];
            for (int i = 0; i < s.length(); i++)
                b[i] = (byte)(s.charAt(i) & 0x7F);
            b[s.length()] = (byte)'\n';
            FileOutputStream fos = new FileOutputStream(VIEWDUMP_OUT, true);
            fos.write(b);
            fos.close();
        } catch (Exception e) {}
    }

    public static void main(String[] args) {
        try { new FileOutputStream(VIEWDUMP_OUT).close(); } catch (Exception e) {}

        String appPkg = "com.example.mockdonalds";
        String activityClass = "com.example.mockdonalds.MenuActivity";
        if (args.length >= 2) { appPkg = args[0]; activityClass = args[1]; }
        else if (args.length >= 1) {
            activityClass = args[0];
            int dot = activityClass.lastIndexOf('.');
            if (dot > 0) appPkg = activityClass.substring(0, dot);
        }

        out("=== AppRunner ===");
        out("PACKAGE: " + appPkg);
        out("ACTIVITY: " + activityClass);

        /* Launch Activity */
        Activity activity;
        try {
            MiniServer.init(appPkg);
            MiniActivityManager am = MiniServer.get().getActivityManager();
            Intent intent = new Intent();
            intent.setClassName(appPkg, activityClass);
            am.startActivity(null, intent, 0);
            activity = am.getResumedActivity();
            if (activity == null) { out("ERROR: no activity"); return; }
        } catch (Throwable t) {
            out("ERROR_INIT: " + t.getClass().getName() + " " + t.getMessage());
            return;
        }

        try {
            rootView = activity.getWindow().getDecorView();
            if (rootView == null) { out("ERROR: no root view"); return; }
        } catch (Throwable t) {
            out("ERROR_ROOT: " + t.getClass().getName());
            return;
        }

        /* Initial measure + layout + draw */
        renderFrame("initial");

        /* Dump RECT geometry (fallback for init binary) */
        dumpRects();

        out("EVENT_LOOP_START");

        /* Event loop: poll for touch input file */
        long lastTouchMod = 0;
        while (true) {
            try {
                File touchFile = new File(TOUCH_INPUT);
                if (touchFile.exists() && touchFile.lastModified() > lastTouchMod) {
                    lastTouchMod = touchFile.lastModified();
                    processTouchFile(touchFile);
                    renderFrame("touch");
                }
                Thread.sleep(50); /* 20Hz poll */
            } catch (Throwable t) {
                /* Keep running */
                try { Thread.sleep(200); } catch (Exception e) {}
            }
        }
    }

    static void renderFrame(String reason) {
        try {
            int wSpec = View.MeasureSpec.makeMeasureSpec(SCREEN_W, View.MeasureSpec.EXACTLY);
            int hSpec = View.MeasureSpec.makeMeasureSpec(SCREEN_H, View.MeasureSpec.EXACTLY);
            rootView.measure(wSpec, hSpec);
            rootView.layout(0, 0, SCREEN_W, SCREEN_H);

            /* Canvas draw */
            if (frameBitmap == null)
                frameBitmap = Bitmap.createBitmap(SCREEN_W, SCREEN_H, Bitmap.Config.ARGB_8888);
            if (frameCanvas == null)
                frameCanvas = new Canvas(frameBitmap);

            rootView.draw(frameCanvas);

            /* Flush pixels */
            com.ohos.shim.bridge.OHBridge.canvasDestroy(frameCanvas.getNativeHandle());
            /* Recreate canvas for next frame (destroy freed the old one) */
            frameCanvas = new Canvas(frameBitmap);

            frameCount++;

            /* Signal frame ready */
            try {
                FileOutputStream fos = new FileOutputStream(FRAME_READY);
                String info = "FRAME " + frameCount + " " + reason + "\n";
                fos.write(info.getBytes());
                fos.close();
            } catch (Exception e) {}

        } catch (Throwable t) {
            out("RENDER_ERROR: " + t.getClass().getName() + " " + t.getMessage());
        }
    }

    static void processTouchFile(File f) {
        try {
            BufferedReader br = new BufferedReader(new FileReader(f));
            String line;
            while ((line = br.readLine()) != null) {
                line = line.trim();
                if (line.startsWith("DOWN ") || line.startsWith("UP ") || line.startsWith("MOVE ")) {
                    String[] parts = line.split(" ");
                    if (parts.length >= 3) {
                        int action;
                        if ("DOWN".equals(parts[0])) action = MotionEvent.ACTION_DOWN;
                        else if ("UP".equals(parts[0])) action = MotionEvent.ACTION_UP;
                        else action = MotionEvent.ACTION_MOVE;

                        float x = Float.parseFloat(parts[1]);
                        float y = Float.parseFloat(parts[2]);

                        dispatchTouch(action, x, y);
                    }
                }
            }
            br.close();
            /* Clear the file after processing */
            new FileOutputStream(f).close();
        } catch (Throwable t) {
            out("TOUCH_ERROR: " + t.getClass().getName());
        }
    }

    static void dispatchTouch(int action, float x, float y) {
        try {
            MotionEvent ev = MotionEvent.obtain(action, x, y, System.currentTimeMillis());
            rootView.dispatchTouchEvent(ev);
            out("TOUCH: action=" + action + " x=" + (int)x + " y=" + (int)y);
        } catch (Throwable t) {
            out("DISPATCH_ERROR: " + t.getClass().getName() + " " + t.getMessage());
        }
    }

    static void dumpRects() {
        try {
            out("SCREEN " + SCREEN_W + " " + SCREEN_H);
            dumpView(rootView, 0, 0);
            out("=== RECTS_DONE ===");
        } catch (Throwable t) {
            out("DUMP_ERROR: " + t.getClass().getName());
        }
    }

    static void dumpView(View v, int ox, int oy) {
        int x = ox + v.getLeft(), y = oy + v.getTop();
        int w = v.getWidth(), h = v.getHeight();
        if (w <= 0 || h <= 0) return;

        String type = v.getClass().getSimpleName();
        int bg = 0;
        if (v instanceof android.widget.Button) bg = 0xFF4CAF50;
        else if (v instanceof android.widget.TextView) bg = 0xFF212121;
        else if (v instanceof android.widget.ListView) bg = 0xFF303030;
        else if (v instanceof android.widget.LinearLayout) bg = 0xFF1A1A2E;

        String text = "";
        if (v instanceof android.widget.TextView) {
            try {
                CharSequence t = ((android.widget.TextView)v).getText();
                if (t != null) text = t.toString().replace("\n", " ");
            } catch (Throwable t) {}
        }

        out("RECT " + x + " " + y + " " + w + " " + h + " " +
            Integer.toHexString(bg) + " " + type + " " + text);

        if (v instanceof ViewGroup) {
            ViewGroup vg = (ViewGroup) v;
            for (int i = 0; i < vg.getChildCount(); i++) {
                try { dumpView(vg.getChildAt(i), x, y); }
                catch (Throwable t) {}
            }
        }
    }
}
