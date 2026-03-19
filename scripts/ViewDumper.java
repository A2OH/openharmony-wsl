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
import java.io.*;

public class ViewDumper {
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
        // Clear output file
        try { new FileOutputStream("/data/a2oh/viewdump.txt").close(); } catch (Exception e) {}

        out("=== ViewDumper ===");

        Activity activity = null;
        try {
            MiniServer.init("com.example.mockdonalds");
            MiniActivityManager am = MiniServer.get().getActivityManager();

            Intent intent = new Intent();
            intent.setClassName("com.example.mockdonalds", "com.example.mockdonalds.MenuActivity");
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
            out("ERROR_ROOT: " + t.getClass().getName() + " " + t.getMessage());
            return;
        }

        try {
            int screenW = 1280, screenH = 800;
            int wSpec = View.MeasureSpec.makeMeasureSpec(screenW, View.MeasureSpec.EXACTLY);
            int hSpec = View.MeasureSpec.makeMeasureSpec(screenH, View.MeasureSpec.EXACTLY);
            out("MEASURE_START");
            root.measure(wSpec, hSpec);
            out("MEASURE_DONE");
            root.layout(0, 0, screenW, screenH);
            out("LAYOUT_DONE");

            out("SCREEN " + screenW + " " + screenH);
            dumpView(root, 0, 0);
            out("=== END ===");
        } catch (Throwable t) {
            out("ERROR_MEASURE: " + t.getClass().getName() + " " + t.getMessage());
            // Still try to dump whatever we have
            try {
                out("SCREEN 1280 800");
                dumpView(root, 0, 0);
                out("=== END (partial) ===");
            } catch (Throwable t2) {
                out("ERROR_DUMP: " + t2.getClass().getName());
            }
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
                try {
                    dumpView(vg.getChildAt(i), x, y);
                } catch (Throwable t) {
                    out("ERROR_CHILD: " + t.getClass().getName());
                }
            }
        }
    }
}
