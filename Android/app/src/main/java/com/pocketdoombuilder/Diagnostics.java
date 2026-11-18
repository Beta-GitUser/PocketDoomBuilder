package com.pocketdoombuilder;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * Startup diagnostics: an in-memory log that is also written to a file the
 * user can reach from the phone itself (Documents/PocketDoomBuilder/), plus
 * clipboard support so the log can be pasted into a bug report.
 *
 * Nothing here may throw: it is the last thing that has to keep working when
 * everything else is broken.
 */
final class Diagnostics {

    private static final String TAG = "PocketDoomBuilder";
    private static final Object LOCK = new Object();
    private static final StringBuilder BUFFER = new StringBuilder();
    private static final SimpleDateFormat STAMP =
            new SimpleDateFormat("HH:mm:ss.SSS", Locale.US);
    private static final SimpleDateFormat FILE_STAMP =
            new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US);

    private static Context appContext;
    private static File internalLog;

    private Diagnostics() {
    }

    static void init(Context context) {
        appContext = context.getApplicationContext();
        try {
            internalLog = new File(appContext.getFilesDir(), "pocketdoombuilder.log");
        } catch (Throwable ignored) {
            internalLog = null;
        }
        synchronized (LOCK) {
            BUFFER.setLength(0);
        }
        log("app", "start of run");
        log("app", "device=" + Build.MANUFACTURER + " " + Build.MODEL
                + " android=" + Build.VERSION.RELEASE + " (" + Build.VERSION.SDK_INT + ")"
                + " abi=" + (Build.SUPPORTED_ABIS.length > 0 ? Build.SUPPORTED_ABIS[0] : "?"));
    }

    /** Append one line to the log. Safe from any thread, never throws. */
    static void log(String tag, String message) {
        final String line;
        synchronized (LOCK) {
            line = STAMP.format(new Date()) + " [" + tag + "] " + message;
            BUFFER.append(line).append('\n');
            if (BUFFER.length() > 64 * 1024) {
                BUFFER.delete(0, BUFFER.length() - 64 * 1024);
            }
        }
        Log.i(TAG, "[" + tag + "] " + message);
        if (internalLog != null) {
            try (FileOutputStream out = new FileOutputStream(internalLog, true)) {
                out.write((line + "\n").getBytes(StandardCharsets.UTF_8));
            } catch (Throwable ignored) {
                // Logging must never be the reason something fails.
            }
        }
    }

    static void log(String message) {
        log("app", message);
    }

    /** The whole in-memory log, newest last. */
    static String text() {
        synchronized (LOCK) {
            return BUFFER.toString();
        }
    }

    /** Head of the log, for the on-screen panel (the tail is usually most useful). */
    static String screenText() {
        String all = text();
        if (all.length() <= 4000) {
            return all;
        }
        return "...\n" + all.substring(all.length() - 4000);
    }

    /**
     * What the GL thread is doing right now, discovered by walking that
     * thread's native stack. A hang inside native init leaves no other trace:
     * no crash, no log line after the last stage marker.
     *
     * Debug builds only - release builds strip the symbols this needs - and
     * best effort: any failure just yields a short note.
     */
    static String glThreadStack() {
        StringBuilder out = new StringBuilder();
        try {
            java.util.Map<Thread, StackTraceElement[]> stacks = Thread.getAllStackTraces();
            boolean found = false;
            for (java.util.Map.Entry<Thread, StackTraceElement[]> entry : stacks.entrySet()) {
                Thread thread = entry.getKey();
                StackTraceElement[] stack = entry.getValue();
                boolean looksLikeGl = thread.getName().contains("GLThread");
                for (StackTraceElement frame : stack) {
                    if (frame.getClassName().contains("com.pocketdoombuilder")) {
                        looksLikeGl = true;
                        break;
                    }
                }
                if (!looksLikeGl) {
                    continue;
                }
                found = true;
                out.append(thread.getName()).append(" state=").append(thread.getState()).append('\n');
                int limit = Math.min(stack.length, 24);
                for (int i = 0; i < limit; i++) {
                    out.append("  at ").append(stack[i]).append('\n');
                }
            }
            if (!found) {
                out.append("(no GL thread found)\n");
            }
        } catch (Throwable t) {
            out.append("(could not read thread stacks: ").append(t).append(")\n");
        }
        return out.toString();
    }

    /** Where the persistent copy of the log lives inside the app sandbox. */
    static String internalLogPath() {
        return internalLog != null ? internalLog.getAbsolutePath() : "(unavailable)";
    }

    /** Text of a native crash report left behind by the previous run, or "". */
    static String previousCrashReport() {
        try {
            File crash = new File(appContext.getFilesDir(), "last_crash.txt");
            if (!crash.exists()) {
                return "";
            }
            byte[] bytes = new byte[(int) Math.min(crash.length(), 32 * 1024)];
            try (java.io.FileInputStream in = new java.io.FileInputStream(crash)) {
                int read = in.read(bytes);
                return read > 0 ? new String(bytes, 0, read, StandardCharsets.UTF_8) : "";
            }
        } catch (Throwable t) {
            return "";
        }
    }

    static void clearCrashReport() {
        try {
            new File(appContext.getFilesDir(), "last_crash.txt").delete();
        } catch (Throwable ignored) {
        }
    }

    /**
     * Write the log where the user can find it from the phone's own file
     * manager: Documents/PocketDoomBuilder/.
     *
     * Android 10+ goes through MediaStore (no storage permission needed);
     * older releases use the public Documents directory, which needs
     * WRITE_EXTERNAL_STORAGE that MainActivity asks for at startup.
     *
     * @return a human-readable location, or null when it could not be written.
     */
    static String saveToDocuments(String text) {
        if (appContext == null) {
            return null;
        }
        String name = "PocketDoomBuilder-" + FILE_STAMP.format(new Date()) + ".txt";
        byte[] bytes = text.getBytes(StandardCharsets.UTF_8);
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                ContentResolver resolver = appContext.getContentResolver();
                ContentValues values = new ContentValues();
                values.put(MediaStore.MediaColumns.DISPLAY_NAME, name);
                values.put(MediaStore.MediaColumns.MIME_TYPE, "text/plain");
                values.put(MediaStore.MediaColumns.RELATIVE_PATH,
                        Environment.DIRECTORY_DOCUMENTS + "/PocketDoomBuilder");
                Uri uri = resolver.insert(MediaStore.Files.getContentUri("external"), values);
                if (uri == null) {
                    return null;
                }
                try (OutputStream out = resolver.openOutputStream(uri, "wt")) {
                    if (out == null) {
                        return null;
                    }
                    out.write(bytes);
                    out.flush();
                }
                return "Documents/PocketDoomBuilder/" + name;
            }
            File dir = new File(
                    Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS),
                    "PocketDoomBuilder");
            if (!dir.exists() && !dir.mkdirs()) {
                return null;
            }
            File file = new File(dir, name);
            try (FileOutputStream out = new FileOutputStream(file)) {
                out.write(bytes);
            }
            return file.getAbsolutePath();
        } catch (Throwable t) {
            log("app", "could not write to Documents: " + t);
            return null;
        }
    }

    /**
     * The on-screen log plus the persistent log of earlier runs, which is what
     * is worth attaching to a bug report (a crash usually ends the run that
     * recorded it).
     */
    static String fullReport() {
        StringBuilder report = new StringBuilder(text());
        try {
            if (internalLog != null && internalLog.exists()) {
                byte[] bytes = new byte[(int) Math.min(internalLog.length(), 128 * 1024)];
                try (java.io.FileInputStream in = new java.io.FileInputStream(internalLog)) {
                    int read = in.read(bytes);
                    if (read > 0) {
                        report.append("\n===== log file (all runs) =====\n");
                        String all = new String(bytes, 0, read, StandardCharsets.UTF_8);
                        // The tail is the interesting part.
                        report.append(all.length() > 60000 ? all.substring(all.length() - 60000) : all);
                    }
                }
            }
        } catch (Throwable ignored) {
        }
        return report.toString();
    }

    /** Put the log on the clipboard so it can be pasted into a bug report. */
    static boolean copyToClipboard(Context context) {
        try {
            ClipboardManager manager =
                    (ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);
            if (manager == null) {
                return false;
            }
            manager.setPrimaryClip(ClipData.newPlainText("PocketDoomBuilder log", fullReport()));
            return true;
        } catch (Throwable t) {
            log("app", "clipboard failed: " + t);
            return false;
        }
    }
}
