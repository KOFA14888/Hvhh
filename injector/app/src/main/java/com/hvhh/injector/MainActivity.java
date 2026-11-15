package com.hvhh.injector;

import android.app.Activity;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MainActivity extends Activity {
    /** The application the library is written for. */
    private static final String TARGET_PACKAGE = "com.axlebolt.standoff2";
    /** Private root-owned directory so files from other apps are never reused. */
    private static final String WORK_DIR = "/data/local/tmp/hvhh-injector";

    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private TextView status;
    private Button inject;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        int pad = (int) (16 * getResources().getDisplayMetrics().density);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(pad, pad, pad, pad);
        TextView title = new TextView(this);
        title.setText("Hvhh Injector (root)");
        title.setTextSize(22);
        layout.addView(title);
        TextView help = new TextView(this);
        help.setText("Target: " + TARGET_PACKAGE + "\n"
            + "ARM64 only. Start the target app first. Root access is required.\n"
            + "Use only on a device and an application you are authorized to modify.");
        layout.addView(help);
        inject = new Button(this);
        inject.setText("Inject into " + TARGET_PACKAGE);
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
        try (InputStream in = getAssets().open(name);
             FileOutputStream out = new FileOutputStream(output)) {
            byte[] buf = new byte[16384];
            int n;
            while ((n = in.read(buf)) != -1) out.write(buf, 0, n);
        }
    }

    private static String quote(String value) { return "'" + value.replace("'", "'\\''") + "'"; }

    /** Runs a shell command as root and returns its combined output. */
    private String root(String command) throws Exception {
        Process process = new ProcessBuilder("su", "-c", command).redirectErrorStream(true).start();
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        try (InputStream in = process.getInputStream()) {
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) != -1) {
                if (output.size() < 65536) output.write(buf, 0, Math.min(n, 65536 - output.size()));
            }
        }
        int exit = process.waitFor();
        return "exit=" + exit + "\n" + new String(output.toByteArray(), StandardCharsets.UTF_8);
    }

    private void startInjection() {
        inject.setEnabled(false);
        status.setText("Requesting root…");
        worker.execute(() -> {
            StringBuilder report = new StringBuilder();
            try {
                report.append(root("id"));
                String pids = root("pidof " + quote(TARGET_PACKAGE)).trim();
                report.append("pids=").append(pids).append('\n');
                if (!pids.contains("exit=0") || pids.length() < 8) {
                    report.append("\nThe target app is not running. Start it, then press the button again.");
                    publish(report.toString());
                    return;
                }

                copyAsset("injector");
                copyAsset("libastra.so");
                String base = getFilesDir().getAbsolutePath();
                String cmd = "umask 077; mkdir -p " + quote(WORK_DIR) + " && chmod 700 " + quote(WORK_DIR)
                    + " && cp " + quote(base + "/injector") + " " + quote(WORK_DIR + "/injector")
                    + " && cp " + quote(base + "/libastra.so") + " " + quote(WORK_DIR + "/libastra.so")
                    + " && chmod 700 " + quote(WORK_DIR + "/injector")
                    + " && chmod 644 " + quote(WORK_DIR + "/libastra.so")
                    + " && " + quote(WORK_DIR + "/injector") + " --package " + quote(TARGET_PACKAGE)
                    + " --libs " + quote(WORK_DIR + "/libastra.so") + " --memfd";
                report.append(root(cmd));
            } catch (Exception e) {
                report.append("Injection failed: ").append(e.getMessage());
            }
            publish(report.toString());
        });
    }

    private void publish(String message) {
        runOnUiThread(() -> {
            status.setText(message);
            inject.setEnabled(true);
        });
    }

    @Override protected void onDestroy() { super.onDestroy(); worker.shutdown(); }
}
