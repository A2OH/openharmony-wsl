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

public class ViewDumper {
    public static void main(String[] args) {
        System.out.println("=== ViewDumper ===");
        
        // Init MiniServer and launch MockDonalds
        MiniServer.init("com.example.mockdonalds");
        MiniActivityManager am = MiniServer.get().getActivityManager();
        
        Intent intent = new Intent();
        intent.setClassName("com.example.mockdonalds", "com.example.mockdonalds.MenuActivity");
        am.startActivity(null, intent, 0);
        
        Activity activity = am.getResumedActivity();
        if (activity == null) {
            System.out.println("ERROR: no activity");
            return;
        }
        
        View root = activity.getWindow().getDecorView();
        if (root == null) {
            System.out.println("ERROR: no root view");
            return;
        }
        
        // Measure and layout
        int screenW = 1280, screenH = 800;
        int wSpec = View.MeasureSpec.makeMeasureSpec(screenW, View.MeasureSpec.EXACTLY);
        int hSpec = View.MeasureSpec.makeMeasureSpec(screenH, View.MeasureSpec.EXACTLY);
        root.measure(wSpec, hSpec);
        root.layout(0, 0, screenW, screenH);
        
        System.out.println("SCREEN " + screenW + " " + screenH);
        
        // Dump view tree as RECT commands
        dumpView(root, 0, 0);
        
        System.out.println("=== END ===");
    }
    
    static void dumpView(View v, int offsetX, int offsetY) {
        int x = offsetX + v.getLeft();
        int y = offsetY + v.getTop();
        int w = v.getWidth();
        int h = v.getHeight();
        
        if (w <= 0 || h <= 0) return;
        
        // Get background color
        int bgColor = 0;
        String type = v.getClass().getSimpleName();
        
        if (v instanceof Button) {
            bgColor = 0xFF4CAF50; // green buttons
            type = "Button";
        } else if (v instanceof TextView) {
            bgColor = 0xFF212121; // dark text bg
            type = "TextView";
        } else if (v instanceof ListView) {
            bgColor = 0xFF303030; // list bg
            type = "ListView";
        } else if (v instanceof LinearLayout) {
            bgColor = 0xFF1A1A2E; // layout bg
            type = "LinearLayout";
        }
        
        // Output: RECT x y w h color type text
        String text = "";
        if (v instanceof TextView) {
            CharSequence t = ((TextView)v).getText();
            if (t != null) text = t.toString().replace("\n", " ");
        }
        
        System.out.println("RECT " + x + " " + y + " " + w + " " + h + " " + 
                           Integer.toHexString(bgColor) + " " + type + " " + text);
        
        // Recurse into children
        if (v instanceof ViewGroup) {
            ViewGroup vg = (ViewGroup) v;
            for (int i = 0; i < vg.getChildCount(); i++) {
                dumpView(vg.getChildAt(i), x, y);
            }
        }
    }
}
