package com.hvhh.injector;

import android.app.Activity;
import android.os.Bundle;
import android.text.InputType;
import android.view.Gravity;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.regex.Pattern;

public class MainActivity extends Activity {
    private static final Pattern PACKAGE = Pattern.compile("[A-Za-z][A-Za-z0-9_]*(\\.[A-Za-z][A-Za-z0-9_]*)+");
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private EditText packageName;
    private TextView status;
    private Button inject;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        int pad = (int)(16 * getResources().getDisplayMetrics().density);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(pad, pad, pad, pad);
        TextView title = new TextView(this);
        title.setText("Hvhh Injector (root)"); title.setTextSize(22);
        layout.addView(title);
        TextView help = new TextView(this);
        help.setText("ARM64 only. Start the target app first, then enter its exact package name. Root access is required. Use only on a device and app you are authorized to modify.");
        layout.addView(help);
        packageName = new EditText(this);
        packageName.setSingleLine(true);
        packageName.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        packageName.setHint("com.example.game");
        packageName.setText(getPreferences(MODE_PRIVATE).getString("package", ""));
        layout.addView(packageName);
        inject = new Button(this);
        inject.setText("Inject into running app");
        layout.addView(inject);
        status = new TextView(this);
        status.setTextIsSelectable(true);
        status.setText("Ready. No root commands have been run yet.");
        ScrollView scroll = new ScrollView(this);
        scroll.addView(status);
        layout.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        layout.setGravity(Gravity.TOP);
        setContentView(layout);
        inject.setOnClickListener(v -> startInjection());
    }

    private void copyAsset(String name) throws Exception {
        File output = new File(getFilesDir(), name);
        try (InputStream in = getAssets().open(name); FileOutputStream out = new FileOutputStream(output)) {
            byte[] buf = new byte[16384];
            int n;
            while ((n = in.read(buf)) != -1) out.write(buf, 0, n);
        }
    }

    private static String quote(String value) { return "'" + value.replace("'", "'\\''") + "'"; }

    private void startInjection() {
        String target = packageName.getText().toString().trim();
        if (!PACKAGE.matcher(target).matches()) {
            status.setText("Invalid package name. Example: com.example.game");
            return;
        }
        getPreferences(MODE_PRIVATE).edit().putString("package", target).apply();
        inject.setEnabled(false);
        status.setText("Requesting root and injecting…");
        worker.execute(() -> {
            String result;
            try {
                copyAsset("injector"); copyAsset("libastra.so");
                String base = getFilesDir().getAbsolutePath();
                // A private root-owned directory avoids reusing files supplied by other apps.
                String dir = "/data/local/tmp/hvhh-injector";
                String cmd = "umask 077; mkdir -p " + quote(dir) + " && chmod 700 " + quote(dir)
                    + " && cp " + quote(base + "/injector") + " " + quote(dir + "/injector")
                    + " && cp " + quote(base + "/libastra.so") + " " + quote(dir + "/libastra.so")
                    + " && chmod 700 " + quote(dir + "/injector")
                    + " && chmod 644 " + quote(dir + "/libastra.so")
                    + " && " + quote(dir + "/injector") + " --package " + quote(target)
                    + " --libs " + quote(dir + "/libastra.so") + " --memfd";
                Process process = new ProcessBuilder("su", "-c", cmd).redirectErrorStream(true).start();
                ByteArrayOutputStream output = new ByteArrayOutputStream();
                try (InputStream in = process.getInputStream()) {
                    byte[] buf = new byte[4096]; int n;
                    while ((n = in.read(buf)) != -1) {
                        if (output.size() < 65536) output.write(buf, 0, Math.min(n, 65536 - output.size()));
                    }
                }
                int exit = process.waitFor();
                result = "Exit code: " + exit + "\n" + new String(output.toByteArray(), StandardCharsets.UTF_8);
            } catch (Exception e) {
                result = "Injection failed: " + e.getMessage();
            }
            final String message = result;
            runOnUiThread(() -> { status.setText(message); inject.setEnabled(true); });
        });
    }

    @Override protected void onDestroy() { super.onDestroy(); worker.shutdown(); }
}
