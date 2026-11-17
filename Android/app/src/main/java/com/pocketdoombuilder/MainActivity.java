package com.pocketdoombuilder;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
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
 *   - reading and writing document bytes.
 *
 * Input is forwarded to native code as batched touch points so the native
 * side can distinguish taps, drags and pinches.
 */
public class MainActivity extends Activity implements GLSurfaceView.Renderer {

    private static final int REQUEST_OPEN_WAD = 1001;
    private static final int REQUEST_SAVE_WAD = 1002;

    private GLSurfaceView surfaceView;

    // Remembered so "Save" can write back to the document it came from.
    @Nullable private Uri currentWadUri;

    static {
        System.loadLibrary("pocketdoombuilder");
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

    // --- Activity lifecycle -------------------------------------------------

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        hideSystemUi();

        surfaceView = new GLSurfaceView(this);
        surfaceView.setEGLContextClientVersion(3);
        surfaceView.setEGLConfigChooser(8, 8, 8, 8, 0, 0);
        surfaceView.setRenderer(this);
        surfaceView.setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
        surfaceView.setOnTouchListener(this::handleTouch);
        setContentView(surfaceView);
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
        if (surfaceView != null) {
            surfaceView.onPause();
        }
        nativePause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemUi();
        if (surfaceView != null) {
            surfaceView.onResume();
        }
    }

    @Override
    protected void onDestroy() {
        nativeDestroy();
        super.onDestroy();
    }

    // --- GLSurfaceView.Renderer --------------------------------------------

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        nativeCreate(getAssets(), surfaceView.getWidth(), surfaceView.getHeight());
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        nativeResize(width, height);
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        // The native side calls back into launchOpenPicker()/launchSavePicker()
        // itself; both post to the UI thread.
        nativeRender();
    }

    // --- Input --------------------------------------------------------------

    private boolean handleTouch(View view, MotionEvent event) {
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
        nativeTouch(action, pointerCount, xs, ys);
        return true;
    }

    @Override
    public boolean onKeyDown(int keyCode, android.view.KeyEvent event) {
        nativeKey(keyCode, 1, event.getUnicodeChar());
        return super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, android.view.KeyEvent event) {
        nativeKey(keyCode, 0, 0);
        return super.onKeyUp(keyCode, event);
    }

    /** Called from native code to show the system file picker. */
    @SuppressWarnings("unused")
    public void launchOpenPicker() {
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

    @Override
    protected void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
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
            nativeOpenWad(out.toByteArray(), displayName(uri), uri.toString());
        } catch (Exception e) {
            toast("Read failed: " + e.getMessage());
        }
    }

    private void writeToUri(Uri uri) {
        byte[] bytes = nativeSaveWad();
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
            toast("Saved " + displayName(uri));
        } catch (Exception e) {
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
