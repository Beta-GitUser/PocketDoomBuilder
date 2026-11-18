package com.pocketdoombuilder;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.net.Uri;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.Nullable;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * Thin Activity shim around the native PocketDoomBuilder shell.
 *
 * Everything that needs Android framework APIs lives here and nowhere else:
 *   - the GLSurfaceView that hosts the EGL context,
 *   - the Storage Access Framework pickers (open / save),
 *   - reading and writing document bytes,
 *   - the diagnostics overlay (see Diagnostics).
 *
 * If the native shell fails to start - a missing library, an EGL problem, an
 * exception from JNI - the activity falls back to a readable panel showing the
 * log, with buttons to copy it or write it to Documents. A phone-only user has
 * no other way to report what went wrong.
 *
 * Input is forwarded to native code as batched touch points so the native
 * side can distinguish taps, drags and pinches.
 */
public class MainActivity extends Activity implements GLSurfaceView.Renderer {

    private static final int REQUEST_OPEN_WAD = 1001;
    private static final int REQUEST_SAVE_WAD = 1002;
    private static final int REQUEST_STORAGE = 1003;

    private GLSurfaceView surfaceView;
    private FrameLayout root;
    private LinearLayout overlay;
    private TextView overlayLog;
    private TextView overlayTitle;

    // Display cutout / gesture bar insets, in pixels. "Respect safe area" keeps
    // the interface out of these; it is off by default so the layout looks the
    // same on devices without a cutout.
    private int insetTop;
    private int insetBottom;
    private int insetLeft;
    private int insetRight;
    private static final String PREF_SAFE_AREA = "safe_area";

    private volatile boolean nativeCreated;
    private volatile boolean nativeCreateFailed;
    private volatile int framesRendered;

    private final Handler handler = new Handler(Looper.getMainLooper());

    // Remembered so "Save" can write back to the document it came from.
    @Nullable private Uri currentWadUri;

    static {
        // Loading the library can itself fail (wrong ABI, packaging problem);
        // record why instead of dying before Diagnostics is even set up.
        try {
            System.loadLibrary("pocketdoombuilder");
        } catch (Throwable t) {
            System.setProperty("pdb.loadError", String.valueOf(t));
        }
    }

    // --- native interface ---------------------------------------------------

    private native void nativeCreate(android.content.res.AssetManager assets,
                                     int width, int height);
    private native void nativeResize(int width, int height);
    private native void nativeRender();
    private native void nativeTouch(int action, int pointerCount, float[] xs, float[] ys);
    private native void nativeKey(int keycode, int action, int unicode);
    private native void nativeOpenWad(byte[] data, String name, String uri);
    private native byte[] nativeSaveWad();
    private native void nativePause();
    private native void nativeDestroy();
    private native void nativeSetFilesDir(String path);
    private native void nativeSetFrameInsets(int top, int bottom, int left, int right);
    private native void nativeSetUiState(boolean safeArea, int top, int bottom, int left,
                                         int right, String logText);

    // --- Activity lifecycle -------------------------------------------------

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        Diagnostics.init(this);
        Diagnostics.log("app", "onCreate, sdk=" + Build.VERSION.SDK_INT);
        String loadError = System.getProperty("pdb.loadError");
        final boolean libraryMissing = loadError != null;
        if (libraryMissing) {
            Diagnostics.log("fatal", "System.loadLibrary failed: " + loadError);
        }
        String previousCrash = Diagnostics.previousCrashReport();
        if (!previousCrash.isEmpty()) {
            Diagnostics.log("crash", "previous run crashed:\n" + previousCrash);
            Diagnostics.clearCrashReport();
        }

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        hideSystemUi();
        requestLegacyStoragePermission();

        try {
            nativeSetFilesDir(getFilesDir().getAbsolutePath());
        } catch (Throwable t) {
            Diagnostics.log("app", "nativeSetFilesDir failed: " + t);
        }

        root = new FrameLayout(this);

        surfaceView = new GLSurfaceView(this);
        surfaceView.setEGLContextClientVersion(3);
        // No explicit setEGLConfigChooser: the library default is the most
        // widely supported configuration. Asking for 8/8/8/8 makes some
        // devices fail to pick a config, which shows up as a black screen.
        surfaceView.setRenderer(this);
        surfaceView.setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
        surfaceView.setOnTouchListener(this::handleTouch);
        root.addView(surfaceView, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        // No floating buttons: the toolbar inside the ImGui UI owns Help,
        // Settings and Diagnostics. The native UI is told about the insets
        // instead, so it can lay itself (and those menus) out inside the safe area.
        installInsetsListener();

        setContentView(root);

        if (!previousCrash.isEmpty()) {
            // Last run died in native code. Show its report *before* the GL
            // thread gets a chance to die the same way, otherwise the panel
            // would flash and vanish and the report would be unreadable.
            Diagnostics.log("crash", "holding the renderer back until the report is read");
            surfaceView.onPause();
            showOverlay("The previous run crashed - tap Continue to start anyway");
            return;
        }

        if (libraryMissing) {
            // No native library, no frames: show why straight away instead of
            // a black screen.
            showOverlay("The native library could not be loaded");
        } else {
            // If the shell has not drawn anything within a few seconds the
            // screen is simply black and nothing else would say why.
            handler.postDelayed(this::checkStartup, 4000);
        }
    }

    private int dp(int value) {
        return (int) TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, value, getResources().getDisplayMetrics());
    }

    private void requestLegacyStoragePermission() {
        // Only pre-10 needs it, and only to write the diagnostics log into the
        // public Documents folder. Everything else is best effort.
        if (Build.VERSION.SDK_INT > 28) {
            return;
        }
        if (checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)
                != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE},
                    REQUEST_STORAGE);
        }
    }

    /**
     * Read the display-cutout and gesture-bar insets and move the on-screen
     * buttons (and the overlay padding) out of them when "Respect safe area" is
     * on. The native UI does not have to know anything about this.
     */
    private void installInsetsListener() {
        View decor = getWindow().getDecorView();
        decor.setOnApplyWindowInsetsListener((view, insets) -> {
            int top = 0, bottom = 0, left = 0, right = 0;
            if (Build.VERSION.SDK_INT >= 28) {
                android.view.DisplayCutout cutout = insets.getDisplayCutout();
                if (cutout != null) {
                    top = Math.max(top, cutout.getSafeInsetTop());
                    bottom = Math.max(bottom, cutout.getSafeInsetBottom());
                    left = Math.max(left, cutout.getSafeInsetLeft());
                    right = Math.max(right, cutout.getSafeInsetRight());
                }
            }
            // Gesture/navigation bar and status bar, when the system reports them.
            if (Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets bars = insets.getInsets(
                        WindowInsets.Type.systemBars()
                                | WindowInsets.Type.displayCutout());
                top = Math.max(top, bars.top);
                bottom = Math.max(bottom, bars.bottom);
                left = Math.max(left, bars.left);
                right = Math.max(right, bars.right);
            } else {
                top = Math.max(top, insets.getSystemWindowInsetTop());
                bottom = Math.max(bottom, insets.getSystemWindowInsetBottom());
                left = Math.max(left, insets.getSystemWindowInsetLeft());
                right = Math.max(right, insets.getSystemWindowInsetRight());
            }
            if (top != insetTop || bottom != insetBottom || left != insetLeft
                    || right != insetRight) {
                insetTop = top;
                insetBottom = bottom;
                insetLeft = left;
                insetRight = right;
                applySafeArea();
            }
            return view.onApplyWindowInsets(insets);
        });
        decor.requestApplyInsets();
    }

    /** Currently selected: keep the interface clear of notches and gesture bars. */
    private boolean safeAreaEnabled() {
        return getPreferences(MODE_PRIVATE).getBoolean(PREF_SAFE_AREA, false);
    }

    private void setSafeAreaEnabled(boolean enabled) {
        getPreferences(MODE_PRIVATE).edit().putBoolean(PREF_SAFE_AREA, enabled).apply();
        Diagnostics.log("safe area", enabled ? "enabled" : "disabled");
        applySafeArea();
    }


    private void applySafeArea() {
        final boolean enabled = safeAreaEnabled();
        final int top = enabled ? insetTop : 0;
        // The native diagnostics window shows the same setting and the insets.
        pushUiState();
        // Keep the ImGui shell (menu bar, toolbar, status bar) inside the same
        // insets, so Help and the other menus are not under the notch.
        try {
            nativeSetFrameInsets(top, enabled ? insetBottom : 0,
                    enabled ? insetLeft : 0, enabled ? insetRight : 0);
        } catch (Throwable t) {
            Diagnostics.log("safe area", "nativeSetFrameInsets failed: " + t);
        }
        final int bottom = enabled ? insetBottom : 0;
        final int left = enabled ? insetLeft : 0;
        final int right = enabled ? insetRight : 0;
        if (overlay != null) {
            overlay.setPadding(dp(12) + left, dp(12) + top, dp(12) + right, dp(12) + bottom);
        }
    }

    private void hideSystemUi() {
        View decor = getWindow().getDecorView();
        decor.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    @Override
    protected void onPause() {
        super.onPause();
        Diagnostics.log("app", "onPause");
        if (surfaceView != null) {
            surfaceView.onPause();
        }
        try {
            nativePause();
        } catch (Throwable t) {
            Diagnostics.log("app", "nativePause failed: " + t);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        Diagnostics.log("app", "onResume");
        hideSystemUi();
        if (surfaceView != null) {
            surfaceView.onResume();
        }
    }

    @Override
    protected void onDestroy() {
        Diagnostics.log("app", "onDestroy");
        try {
            nativeDestroy();
        } catch (Throwable t) {
            Diagnostics.log("app", "nativeDestroy failed: " + t);
        }
        super.onDestroy();
    }

    // --- GLSurfaceView.Renderer --------------------------------------------

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        // The ImGui backend here is the OpenGL ES 3 one; if the device hands
        // back an ES 2.0 context nothing can be drawn, so record exactly what
        // was granted.
        try {
            String version = GLES20.glGetString(GLES20.GL_VERSION);
            String renderer = GLES20.glGetString(GLES20.GL_RENDERER);
            String vendor = GLES20.glGetString(GLES20.GL_VENDOR);
            Diagnostics.log("gl", "surface created: version=" + version
                    + " renderer=" + renderer + " vendor=" + vendor);
            if (version == null || !version.startsWith("OpenGL ES 3")) {
                Diagnostics.log("fatal", "the context is not OpenGL ES 3 (" + version
                        + "); the renderer requires ES 3.0");
                handler.post(() -> showOverlay("This device did not provide OpenGL ES 3"));
            }
        } catch (Throwable t) {
            Diagnostics.log("gl", "could not query the GL strings: " + t);
        }
        try {
            nativeCreate(getAssets(), surfaceView.getWidth(), surfaceView.getHeight());
            nativeCreated = true;
        } catch (Throwable t) {
            nativeCreateFailed = true;
            Diagnostics.log("fatal", "nativeCreate threw: " + t);
            handler.post(() -> showOverlay("The editor failed to start"));
        }
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        Diagnostics.log("gl", "surface " + width + "x" + height);
        try {
            nativeResize(width, height);
        } catch (Throwable t) {
            Diagnostics.log("fatal", "nativeResize threw: " + t);
        }
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        try {
            nativeRender();
            framesRendered++;
            if (framesRendered == 1) {
                Diagnostics.log("gl", "first frame drawn");
            }
        } catch (Throwable t) {
            Diagnostics.log("fatal", "nativeRender threw: " + t);
            handler.post(() -> showOverlay("The editor stopped drawing"));
        }
    }

    private void checkStartup() {
        if (framesRendered != 0) {
            return;
        }
        Diagnostics.log("fatal", "no frames drawn after 4s "
                + "(nativeCreated=" + nativeCreated
                + ", createFailed=" + nativeCreateFailed + ")");
        // nativeCreated stays false when nativeCreate never returned, i.e. the
        // GL thread is stuck inside native init. Its stack says exactly where.
        if (!nativeCreated && !nativeCreateFailed) {
            Diagnostics.log("fatal", "the GL thread is stuck in native initialisation;\n"
                    + "--- its native stack ---\n" + Diagnostics.glThreadStack());
        }
        showOverlay("Nothing was drawn");
    }

    // --- Diagnostics overlay ------------------------------------------------

    /**
     * Startup diagnostics overlay: the fallback used when the app fails before
     * the UI can draw (missing library, no ES3 context, a crash report, or a
     * hang). Once the editor is up, the log is reachable from the Help menu and
     * from the toolbar instead.
     */
    private void showOverlay(@Nullable String headline) {
        if (overlay == null) {
            overlay = new LinearLayout(this);
            overlay.setOrientation(LinearLayout.VERTICAL);
            overlay.setBackgroundColor(Color.parseColor("#F0181818"));

            TextView title = new TextView(this);
            title.setTextColor(Color.WHITE);
            title.setTextSize(18);
            title.setText(headline == null ? "Diagnostics" : headline);
            overlay.addView(title);

            TextView hint = new TextView(this);
            hint.setTextColor(Color.parseColor("#FFB0B0B0"));
            hint.setTextSize(12);
            hint.setPadding(0, dp(4), 0, dp(6));
            hint.setText("Copy the log and paste it into the chat, or save it to "
                    + "Documents/PocketDoomBuilder and attach the file.");
            overlay.addView(hint);

            overlayLog = new TextView(this);
            overlayLog.setTextColor(Color.parseColor("#FFD0D0D0"));
            overlayLog.setTextSize(10);
            overlayLog.setTypeface(android.graphics.Typeface.MONOSPACE);
            ScrollView scroll = new ScrollView(this);
            scroll.addView(overlayLog);
            LinearLayout.LayoutParams scrollParams = new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f);
            overlay.addView(scroll, scrollParams);

            LinearLayout buttons = new LinearLayout(this);
            buttons.setOrientation(LinearLayout.HORIZONTAL);
            buttons.addView(overlayButton("Copy log", this::copyLog));
            buttons.addView(overlayButton("Save to Documents", this::saveLog));
            buttons.addView(overlayButton("Close", () -> overlay.setVisibility(View.GONE)));
            buttons.addView(overlayButton("Continue", () -> {
                overlay.setVisibility(View.GONE);
                surfaceView.onResume();
            }));
            overlay.addView(buttons);

            FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
            root.addView(overlay, params);
        }
        overlay.setVisibility(View.VISIBLE);
        // The overlay is created lazily, so the padding is applied once here and
        // again whenever the safe-area setting changes.
        applySafeArea();
        refreshOverlayLog();
    }

    /** Fill the log view from the buffer and scroll to the newest line. */
    private void refreshOverlayLog() {
        if (overlayLog == null) {
            return;
        }
        String log = Diagnostics.text();
        if (log.isEmpty()) {
            log = "(no log lines yet; the native library probably never loaded: "
                    + System.getProperty("pdb.loadError") + ")";
        }
        overlayLog.setText(log);
        overlayLog.post(() -> {
            View parent = (View) overlayLog.getParent();
            if (parent instanceof ScrollView) {
                ((ScrollView) parent).fullScroll(View.FOCUS_DOWN);
            }
        });
    }

    private Button overlayButton(String label, Runnable action) {
        Button button = new Button(this);
        button.setText(label);
        button.setTextSize(12);
        button.setAllCaps(false);
        button.setOnClickListener(v -> action.run());
        return button;
    }

    private void copyLog() {
        boolean ok = Diagnostics.copyToClipboard(this);
        toast(ok ? "Log copied" : "Could not copy the log");
    }

    private void saveLog() {
        String location = Diagnostics.saveToDocuments(Diagnostics.fullReport());
        if (location == null) {
            toast("Could not write to Documents");
            Diagnostics.log("app", "save to Documents failed");
            return;
        }
        Diagnostics.log("app", "log saved to " + location);
        toast("Saved to " + location);
    }

    // --- Input --------------------------------------------------------------

    private boolean handleTouch(View view, MotionEvent event) {
        if (overlay != null && overlay.getVisibility() == View.VISIBLE) {
            return false; // let the overlay's own widgets handle it
        }
        final int pointerCount = event.getPointerCount();
        float[] xs = new float[pointerCount];
        float[] ys = new float[pointerCount];
        for (int i = 0; i < pointerCount; i++) {
            xs[i] = event.getX(i);
            ys[i] = event.getY(i);
        }

        final int action;
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                action = 0;
                break;
            case MotionEvent.ACTION_MOVE:
                action = 1;
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL:
                action = 2;
                break;
            default:
                action = 1;
                break;
        }
        try {
            nativeTouch(action, pointerCount, xs, ys);
        } catch (Throwable t) {
            Diagnostics.log("fatal", "nativeTouch threw: " + t);
        }
        return true;
    }

    @Override
    public boolean onKeyDown(int keyCode, android.view.KeyEvent event) {
        try {
            nativeKey(keyCode, 1, event.getUnicodeChar());
        } catch (Throwable t) {
            Diagnostics.log("fatal", "nativeKey threw: " + t);
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, android.view.KeyEvent event) {
        try {
            nativeKey(keyCode, 0, 0);
        } catch (Throwable t) {
            Diagnostics.log("fatal", "nativeKey threw: " + t);
        }
        return super.onKeyUp(keyCode, event);
    }

    /** Called from native code to show the system file picker. */
    @SuppressWarnings("unused")
    public void launchOpenPicker() {
        Diagnostics.log("io", "open picker");
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(intent, REQUEST_OPEN_WAD);
        });
    }

    /** Called from native code to write the current WAD back out. */
    @SuppressWarnings("unused")
    public void launchSavePicker(boolean saveAs) {
        Diagnostics.log("io", "save picker saveAs=" + saveAs);
        runOnUiThread(() -> {
            if (!saveAs && currentWadUri != null) {
                writeToUri(currentWadUri);
                return;
            }
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("application/octet-stream");
            intent.putExtra(Intent.EXTRA_TITLE, "map.wad");
            startActivityForResult(intent, REQUEST_SAVE_WAD);
        });
    }

    /** Called from the Help menu so a manual or About link opens a browser. */
    @SuppressWarnings("unused")
    public void openUrl(String url) {
        Diagnostics.log("help", "open url " + url);
        runOnUiThread(() -> {
            try {
                Intent intent = new Intent(Intent.ACTION_VIEW, android.net.Uri.parse(url));
                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                startActivity(intent);
            } catch (Throwable t) {
                Diagnostics.log("help", "no activity can open " + url + ": " + t);
            }
        });
    }

    /**
     * Called from Help > Diagnostics and Log (or the toolbar) to refresh the
     * native window's copy of the log.
     */
    @SuppressWarnings("unused")
    public void showDiagnosticsPanel() {
        pushUiState();
    }

    /** Called when the native window's "Copy log" button is pressed. */
    @SuppressWarnings("unused")
    public void copyLogToClipboard() {
        boolean ok = Diagnostics.copyToClipboard(this);
        toast(ok ? "Log copied" : "Could not copy the log");
    }

    /** Called when the native window's "Save to Documents" button is pressed. */
    @SuppressWarnings("unused")
    public void saveLogToDocuments() {
        String saved = Diagnostics.saveToDocuments(Diagnostics.fullReport());
        toast(saved != null ? "Saved to " + saved : "Could not save the log");
    }

    /** Called when the safe-area checkbox in the native Settings window is toggled. */
    @SuppressWarnings("unused")
    public void setSafeArea(boolean enabled) {
        setSafeAreaEnabled(enabled);
    }

    /** Push the settings and the current log into the native UI. */
    private void pushUiState() {
        try {
            nativeSetUiState(safeAreaEnabled(), insetTop, insetBottom, insetLeft, insetRight,
                    Diagnostics.fullReport());
        } catch (Throwable t) {
            Diagnostics.log("app", "nativeSetUiState failed: " + t);
        }
    }

    /**
     * Called from Help > Program Configuration Folder. The desktop opens
     * Explorer on its settings directory; on Android the equivalent is the app's
     * private files dir, which no file manager can browse, so the log is written
     * to Documents/PocketDoomBuilder (which the user can open) and the path is
     * reported in the log instead.
     */
    @SuppressWarnings("unused")
    public void showConfigFolder() {
        String files = getFilesDir().getAbsolutePath();
        String saved = Diagnostics.saveToDocuments(Diagnostics.fullReport());
        Diagnostics.log("help", "program configuration folder: " + files);
        Diagnostics.log("help", "log written to " + saved);
        runOnUiThread(() -> showOverlay(null));
    }

    /**
     * Called from Help > About to save the whole reference manual as one HTML
     * file in Documents/PocketDoomBuilder, so it can be read in a browser
     * without the app (the desktop ships a .chm, which Android cannot open).
     */
    @SuppressWarnings("unused")
    public void exportManual(String html) {
        String saved = Diagnostics.saveToDocumentsNamed(html, "ReferenceManual.html");
        Diagnostics.log("help", "manual exported to " + saved);
        runOnUiThread(() -> showOverlay(null));
    }

    /** Called from native code so native log lines land in the same log. */
    @SuppressWarnings("unused")
    public void appendNativeLog(String line) {
        Diagnostics.log("native", line);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            Diagnostics.log("io", "picker cancelled (request " + requestCode + ")");
            return;
        }
        Uri uri = data.getData();
        if (requestCode == REQUEST_OPEN_WAD) {
            readFromUri(uri);
        } else if (requestCode == REQUEST_SAVE_WAD) {
            currentWadUri = uri;
            writeToUri(uri);
        }
    }

    // --- Document IO --------------------------------------------------------

    private void readFromUri(Uri uri) {
        try (InputStream in = getContentResolver().openInputStream(uri)) {
            if (in == null) {
                toast("Could not open the selected file");
                return;
            }
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
            currentWadUri = uri;
            Diagnostics.log("io", "opened " + displayName(uri) + " (" + out.size() + " bytes)");
            nativeOpenWad(out.toByteArray(), displayName(uri), uri.toString());
        } catch (Throwable e) {
            Diagnostics.log("fatal", "read failed: " + e);
            toast("Read failed: " + e.getMessage());
        }
    }

    private void writeToUri(Uri uri) {
        byte[] bytes;
        try {
            bytes = nativeSaveWad();
        } catch (Throwable t) {
            Diagnostics.log("fatal", "nativeSaveWad threw: " + t);
            toast("Save failed: " + t);
            return;
        }
        if (bytes == null) {
            toast("Nothing to save");
            return;
        }
        try (OutputStream out = getContentResolver().openOutputStream(uri, "wt")) {
            if (out == null) {
                toast("Could not write to the selected file");
                return;
            }
            out.write(bytes);
            out.flush();
            Diagnostics.log("io", "saved " + displayName(uri) + " (" + bytes.length + " bytes)");
            toast("Saved " + displayName(uri));
        } catch (Throwable e) {
            Diagnostics.log("fatal", "save failed: " + e);
            toast("Save failed: " + e.getMessage());
        }
    }

    private String displayName(Uri uri) {
        String name = uri.getLastPathSegment();
        if (name == null || name.isEmpty()) {
            name = "map.wad";
        }
        int slash = name.lastIndexOf('/');
        if (slash >= 0) {
            name = name.substring(slash + 1);
        }
        return name;
    }

    private void toast(String message) {
        runOnUiThread(() -> Toast.makeText(this, message, Toast.LENGTH_SHORT).show());
    }
}
