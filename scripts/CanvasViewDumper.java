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

        /* Measure and layout */
        try {
            int wSpec = View.MeasureSpec.makeMeasureSpec(screenW, View.MeasureSpec.EXACTLY);
            int hSpec = View.MeasureSpec.makeMeasureSpec(screenH, View.MeasureSpec.EXACTLY);
            out("MEASURE_START");
            root.measure(wSpec, hSpec);
            out("MEASURE_DONE");
            root.layout(0, 0, screenW, screenH);
            out("LAYOUT_DONE");
        } catch (Throwable t) {
            out("ERROR_MEASURE: " + t.getClass().getName() + " " + t.getMessage());
        }

        /* Canvas draw — produces real pixels via software renderer */
        try {
            out("CANVAS_DRAW_START");
            Bitmap bitmap = Bitmap.createBitmap(screenW, screenH, Bitmap.Config.ARGB_8888);
            Canvas canvas = new Canvas(bitmap);
            root.draw(canvas);
            out("CANVAS_DRAW_DONE");
            /* Explicitly destroy canvas to flush pixels to /data/a2oh/canvas_pixels.raw */
            com.ohos.shim.bridge.OHBridge.canvasDestroy(canvas.getNativeHandle());
            out("CANVAS_FLUSHED");
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
        else if (v instanceof LinearLayout) bgColor = 0xFF1A1A2E;

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
