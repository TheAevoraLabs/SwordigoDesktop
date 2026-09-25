package in.aevora.ruby;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.view.Gravity;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.ImageView;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;

import android.media.MediaPlayer;

import org.qtproject.qt.android.bindings.QtActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

/**
 * RubyActivity — Main Android activity for Ruby GG Mobile (in.aevora.ruby).
 * Extends QtActivity to integrate Qt 6.6 application lifecycle, surfaces, and input.
 * Features:
 *  - Direct All Files Access (MANAGE_EXTERNAL_STORAGE)
 *  - Dynamic runtime orientation switching (Portrait Hub/Editor, Landscape 3D Viewport)
 *  - In-place file intent handling (open .scene, .scl, .pod, .lua directly)
 *  - Immersive fullscreen and punch-hole/notch edge-to-edge layout
 *  - Tactile haptic feedback
 *  - Bidirectional Qt <-> Android JNI bridge
 */
public class RubyActivity extends QtActivity {
    private static final String TAG = "RubyActivity";
    private static final int REQUEST_MANAGE_STORAGE = 1001;
    private static final int REQUEST_LEGACY_STORAGE = 1002;
    private static final int REQUEST_PICK_FOLDER = 2001;
    private static final int REQUEST_PICK_FILE = 2002;
    private static final int REQUEST_CODE_EDITOR = 2003;
    private static String sPendingPickerTag = "";

    private static RubyActivity sInstance = null;
    private static String sPendingFilePath = null;
    private static boolean sNativeReady = false;
    private FrameLayout mSplashOverlay = null;

    // Native C++ callbacks
    public static native void nativeOnFileOpened(String filePath);
    public static native void nativeOnPickerResult(String tag, String path, boolean isFolder);
    public static native void nativeOnStoragePermissionGranted(boolean granted);
    public static native void nativeUpdateWindowInsets(int top, int bottom, int left, int right);

    public static RubyActivity getInstance() {
        return sInstance;
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        sInstance = this;
        super.onCreate(savedInstanceState);

        // Guard against Samsung / Android display event race condition during early Qt initialization
        installSafeDisplayListener();

        // Immediate dark window surface to prevent any white/black flash
        try {
            getWindow().setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(android.graphics.Color.parseColor("#121316")));
        } catch (Exception ignored) {}

        // Configure edge-to-edge and display cutout for modern screens
        setupDisplayCutout();
        hideSystemBars();

        try {
            getWindow().getDecorView().setOnSystemUiVisibilityChangeListener(visibility -> {
                if ((visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0) {
                    hideSystemBars();
                }
            });
        } catch (Exception ignored) {}

        // Listen for WindowInsets (status bar, navigation bar, punch-hole cutouts)
        setupWindowInsets();

        // Lock maximum hardware display refresh rate (90Hz / 120Hz / 144Hz)
        lockHighRefreshRate();

        // Request direct all-files storage access
        checkAndRequestAllFilesAccess();

        // Handle file open intent if launched from external file manager
        handleIntent(getIntent());
    }

    /**
     * Locks display refresh rate to maximum hardware capability (90Hz / 120Hz / 144Hz)
     * preventing Samsung and Android thermal/power governors from throttling the editor.
     */
    private void lockHighRefreshRate() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            try {
                android.view.Display display = getDisplay();
                if (display != null) {
                    float maxRate = 60.0f;
                    int bestModeId = 0;
                    for (android.view.Display.Mode mode : display.getSupportedModes()) {
                        if (mode.getRefreshRate() > maxRate) {
                            maxRate = mode.getRefreshRate();
                            bestModeId = mode.getModeId();
                        }
                    }
                    WindowManager.LayoutParams lp = getWindow().getAttributes();
                    if (bestModeId != 0) {
                        lp.preferredDisplayModeId = bestModeId;
                    }
                    try {
                        java.lang.reflect.Field minRateField = lp.getClass().getField("preferredMinDisplayRefreshRate");
                        minRateField.setFloat(lp, maxRate);
                        java.lang.reflect.Field maxRateField = lp.getClass().getField("preferredMaxDisplayRefreshRate");
                        maxRateField.setFloat(lp, maxRate);
                    } catch (Throwable ignored) {}

                    getWindow().setAttributes(lp);
                    Log.i(TAG, "Successfully locked display refresh rate to " + maxRate + " Hz (Mode ID: " + bestModeId + ")");
                }
            } catch (Throwable t) {
                Log.w(TAG, "Failed to lock high refresh rate: " + t.getMessage());
            }
        }
    }

    /**
     * Intercepts DisplayListener to protect QtActivityDelegate from NullPointerException
     * if Samsung OneUI or Android posts onDisplayChanged before QtLayout is created.
     */
    private void installSafeDisplayListener() {
        try {
            android.hardware.display.DisplayManager dm =
                (android.hardware.display.DisplayManager) getSystemService(Context.DISPLAY_SERVICE);
            if (dm == null) return;

            final Object delegate = org.qtproject.qt.android.QtNative.activityDelegate();
            if (delegate == null) return;

            java.lang.reflect.Field listenerField = delegate.getClass().getDeclaredField("displayListener");
            listenerField.setAccessible(true);
            final android.hardware.display.DisplayManager.DisplayListener origListener =
                (android.hardware.display.DisplayManager.DisplayListener) listenerField.get(delegate);

            if (origListener != null) {
                dm.unregisterDisplayListener(origListener);
                android.hardware.display.DisplayManager.DisplayListener safeListener =
                    new android.hardware.display.DisplayManager.DisplayListener() {
                        @Override
                        public void onDisplayAdded(int displayId) {
                            try { origListener.onDisplayAdded(displayId); } catch (Throwable ignored) {}
                        }

                        @Override
                        public void onDisplayRemoved(int displayId) {
                            try { origListener.onDisplayRemoved(displayId); } catch (Throwable ignored) {}
                        }

                        @Override
                        public void onDisplayChanged(int displayId) {
                            try {
                                java.lang.reflect.Field layoutField = delegate.getClass().getDeclaredField("m_layout");
                                layoutField.setAccessible(true);
                                if (layoutField.get(delegate) != null) {
                                    origListener.onDisplayChanged(displayId);
                                }
                            } catch (Throwable ignored) {}
                        }
                    };
                dm.registerDisplayListener(safeListener, null);
                Log.i(TAG, "Safe display listener installed");
            }
        } catch (Throwable t) {
            Log.w(TAG, "installSafeDisplayListener notice: " + t.getMessage());
        }
    }

    private void showNativeBootScreen() {
        try {
            mSplashOverlay = new FrameLayout(this);
            mSplashOverlay.setBackgroundColor(android.graphics.Color.parseColor("#121316"));
            mSplashOverlay.setClickable(true);
            mSplashOverlay.setFocusable(true);

            LinearLayout content = new LinearLayout(this);
            content.setOrientation(LinearLayout.VERTICAL);
            content.setGravity(Gravity.CENTER);
            FrameLayout.LayoutParams contentLp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.CENTER
            );

            // Branded Emblem / Icon
            android.widget.ImageView iconView = new android.widget.ImageView(this);
            boolean iconSet = false;
            try {
                int iconRes = getResources().getIdentifier("ic_launcher", "mipmap", getPackageName());
                if (iconRes == 0) {
                    iconRes = getResources().getIdentifier("ic_launcher", "drawable", getPackageName());
                }
                if (iconRes != 0) {
                    iconView.setImageResource(iconRes);
                    iconSet = true;
                }
            } catch (Exception ignored) {}

            if (!iconSet) {
                // Fallback rounded Ruby gemstone emblem
                GradientDrawable emblem = new GradientDrawable();
                emblem.setShape(GradientDrawable.OVAL);
                emblem.setColors(new int[]{
                    android.graphics.Color.parseColor("#E06C75"),
                    android.graphics.Color.parseColor("#BE5059")
                });
                iconView.setImageDrawable(emblem);
            }
            int iconPx = (int) (72 * getResources().getDisplayMetrics().density);
            LinearLayout.LayoutParams iconLp = new LinearLayout.LayoutParams(iconPx, iconPx);
            iconLp.gravity = Gravity.CENTER_HORIZONTAL;
            content.addView(iconView, iconLp);

            // App Title
            TextView title = new TextView(this);
            title.setText("◆ RUBY GG");
            title.setTextColor(android.graphics.Color.parseColor("#E5E9F0"));
            title.setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 24);
            title.setTypeface(Typeface.DEFAULT_BOLD);
            title.setLetterSpacing(0.08f);
            title.setGravity(Gravity.CENTER);
            LinearLayout.LayoutParams titleLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            );
            titleLp.topMargin = (int) (18 * getResources().getDisplayMetrics().density);
            titleLp.gravity = Gravity.CENTER_HORIZONTAL;
            content.addView(title, titleLp);

            // Subtitle
            TextView subTitle = new TextView(this);
            subTitle.setText("SWORDIGO 3D ASSET STUDIO");
            subTitle.setTextColor(android.graphics.Color.parseColor("#9AA2B1"));
            subTitle.setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 11);
            subTitle.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
            subTitle.setLetterSpacing(0.22f);
            subTitle.setGravity(Gravity.CENTER);
            LinearLayout.LayoutParams subLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            );
            subLp.topMargin = (int) (6 * getResources().getDisplayMetrics().density);
            subLp.gravity = Gravity.CENTER_HORIZONTAL;
            content.addView(subTitle, subLp);

            // Sleek Indeterminate Progress Bar in Ruby Crimson
            ProgressBar bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
            bar.setIndeterminate(true);
            try {
                bar.getIndeterminateDrawable().setColorFilter(
                    android.graphics.Color.parseColor("#E06C75"),
                    android.graphics.PorterDuff.Mode.SRC_IN
                );
            } catch (Exception ignored) {}
            int barWidth = (int) (160 * getResources().getDisplayMetrics().density);
            int barHeight = (int) (4 * getResources().getDisplayMetrics().density);
            LinearLayout.LayoutParams barLp = new LinearLayout.LayoutParams(barWidth, barHeight);
            barLp.topMargin = (int) (26 * getResources().getDisplayMetrics().density);
            barLp.gravity = Gravity.CENTER_HORIZONTAL;
            content.addView(bar, barLp);

            // Status label
            TextView statusLabel = new TextView(this);
            statusLabel.setText("Starting workspace...");
            statusLabel.setTextColor(android.graphics.Color.parseColor("#5C6370"));
            statusLabel.setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 11);
            statusLabel.setLetterSpacing(0.08f);
            statusLabel.setGravity(Gravity.CENTER);
            LinearLayout.LayoutParams statusLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            );
            statusLp.topMargin = (int) (12 * getResources().getDisplayMetrics().density);
            statusLp.gravity = Gravity.CENTER_HORIZONTAL;
            content.addView(statusLabel, statusLp);

            mSplashOverlay.addView(content, contentLp);
            addContentView(mSplashOverlay, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            ));
        } catch (Exception e) {
            Log.w(TAG, "showNativeBootScreen error: " + e.getMessage());
        }
    }

    private void setupWindowInsets() {
        try {
            getWindow().getDecorView().setOnApplyWindowInsetsListener((v, insets) -> {
                int top = 0, bottom = 0, left = 0, right = 0;
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    android.graphics.Insets barInsets = insets.getInsets(
                        WindowInsets.Type.statusBars() | WindowInsets.Type.displayCutout()
                    );
                    android.graphics.Insets navInsets = insets.getInsets(
                        WindowInsets.Type.navigationBars()
                    );
                    top = barInsets.top;
                    bottom = navInsets.bottom;
                    left = barInsets.left;
                    right = barInsets.right;
                } else {
                    top = insets.getSystemWindowInsetTop();
                    bottom = insets.getSystemWindowInsetBottom();
                    left = insets.getSystemWindowInsetLeft();
                    right = insets.getSystemWindowInsetRight();
                }
                try {
                    nativeUpdateWindowInsets(top, bottom, left, right);
                } catch (UnsatisfiedLinkError ignored) {}
                return v.onApplyWindowInsets(insets);
            });
        } catch (Exception e) {
            Log.w(TAG, "setupWindowInsets error: " + e.getMessage());
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleIntent(intent);
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideSystemBars();
        if (sPendingFilePath != null && sNativeReady) {
            final String path = sPendingFilePath;
            sPendingFilePath = null;
            nativeOnFileOpened(path);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    @Override
    public void onConfigurationChanged(android.content.res.Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        hideSystemBars();
    }

    @Override
    protected void onDestroy() {
        if (sInstance == this) {
            sInstance = null;
        }
        super.onDestroy();
    }

    /**
     * Enforce permanent, immersive sticky fullscreen game mode.
     * System bars (status bar, navigation pills) are kicked out with swipe-transient behavior.
     */
    public void hideSystemBars() {
        runOnUiThread(() -> {
            try {
                android.view.Window window = getWindow();
                if (window == null) return;

                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                    window.setDecorFitsSystemWindows(false);
                    WindowInsetsController controller = window.getInsetsController();
                    if (controller != null) {
                        controller.hide(
                            WindowInsets.Type.statusBars()
                            | WindowInsets.Type.navigationBars()
                            | WindowInsets.Type.captionBar()
                            | WindowInsets.Type.systemBars()
                        );
                        controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                    }
                }

                View decor = window.getDecorView();
                if (decor != null) {
                    decor.setSystemUiVisibility(
                        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                        | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                    );
                }
            } catch (Exception e) {
                Log.w(TAG, "hideSystemBars error: " + e.getMessage());
            }
        });
    }

    /**
     * Gracefully move task to back when pressing back at root Home screen,
     * keeping the activity, memory, and scenes alive in RAM.
     */
    public void moveToBack() {
        runOnUiThread(() -> {
            try {
                moveTaskToBack(true);
            } catch (Exception e) {
                Log.w(TAG, "moveToBack error: " + e.getMessage());
            }
        });
    }

    /**
     * Called from C++ once Qt event loop and main window are initialized.
     */
    public static void onNativeInitialized() {
        sNativeReady = true;
        if (sPendingFilePath != null) {
            final String path = sPendingFilePath;
            sPendingFilePath = null;
            nativeOnFileOpened(path);
        }

        // Smooth fade out of Qt's native splash screen
        if (sInstance != null) {
            sInstance.runOnUiThread(() -> {
                sInstance.hideSystemBars();
                try {
                    sInstance.hideSplashScreen(250);
                } catch (Throwable ignored) {}

                if (sInstance.mSplashOverlay != null) {
                    sInstance.mSplashOverlay.animate()
                        .alpha(0.0f)
                        .setDuration(250)
                        .withEndAction(() -> {
                            if (sInstance != null && sInstance.mSplashOverlay != null) {
                                ViewGroup parent = (ViewGroup) sInstance.mSplashOverlay.getParent();
                                if (parent != null) {
                                    parent.removeView(sInstance.mSplashOverlay);
                                }
                                sInstance.mSplashOverlay = null;
                            }
                        })
                        .start();
                }
            });
        }
    }

    /**
     * Modern punch-hole / notch cutout support (API 28+).
     */
    private void setupDisplayCutout() {
        try {
            android.view.Window window = getWindow();
            if (window == null) return;

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                WindowManager.LayoutParams lp = window.getAttributes();
                lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
                window.setAttributes(lp);
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                window.setDecorFitsSystemWindows(false);
            }
            window.addFlags(
                WindowManager.LayoutParams.FLAG_FULLSCREEN
                | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
            );
        } catch (Exception e) {
            Log.w(TAG, "Display cutout setup: " + e.getMessage());
        }
    }

    /**
     * Check and request All Files Access (MANAGE_EXTERNAL_STORAGE).
     */
    public void checkAndRequestAllFilesAccess() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // Android 11+ (API 30..36): All Files Access
            if (!Environment.isExternalStorageManager()) {
                try {
                    Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                    intent.addCategory("android.intent.category.DEFAULT");
                    intent.setData(Uri.parse(String.format("package:%s", getPackageName())));
                    startActivityForResult(intent, REQUEST_MANAGE_STORAGE);
                } catch (Exception e) {
                    Intent intent = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                    startActivityForResult(intent, REQUEST_MANAGE_STORAGE);
                }
            }
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            // Android 6..10: Legacy External Storage permissions
            if (checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[]{
                    Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE
                }, REQUEST_LEGACY_STORAGE);
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_MANAGE_STORAGE) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                boolean granted = Environment.isExternalStorageManager();
                Log.i(TAG, "MANAGE_EXTERNAL_STORAGE result: " + granted);
                nativeOnStoragePermissionGranted(granted);
            }
        } else if (requestCode == REQUEST_PICK_FOLDER) {
            if (resultCode == Activity.RESULT_OK && data != null && data.getData() != null) {
                String path = getPathFromTreeUri(this, data.getData());
                if (path != null && !path.isEmpty()) {
                    Log.i(TAG, "Picked folder: " + path + " (tag=" + sPendingPickerTag + ")");
                    nativeOnPickerResult(sPendingPickerTag, path, true);
                }
            }
        } else if (requestCode == REQUEST_PICK_FILE) {
            if (resultCode == Activity.RESULT_OK && data != null && data.getData() != null) {
                String path = resolveUriToFilePath(data.getData());
                if (path != null && !path.isEmpty()) {
                    Log.i(TAG, "Picked file: " + path + " (tag=" + sPendingPickerTag + ")");
                    nativeOnPickerResult(sPendingPickerTag, path, false);
                }
            }
        } else if (requestCode == REQUEST_CODE_EDITOR) {
            if (resultCode == Activity.RESULT_OK && data != null) {
                String action = data.getStringExtra("action");
                String filePath = data.getStringExtra("file_path");
                if ("open_visual".equals(action) && filePath != null && !filePath.isEmpty()) {
                    nativeOnFileOpened(filePath);
                }
            }
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_LEGACY_STORAGE) {
            boolean granted = (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED);
            Log.i(TAG, "WRITE_EXTERNAL_STORAGE result: " + granted);
            nativeOnStoragePermissionGranted(granted);
        }
    }

    /**
     * Handle incoming VIEW / EDIT intents from other apps.
     */
    private void handleIntent(Intent intent) {
        if (intent == null) return;
        String action = intent.getAction();
        if (Intent.ACTION_VIEW.equals(action) || Intent.ACTION_EDIT.equals(action)) {
            Uri uri = intent.getData();
            if (uri != null) {
                String path = resolveUriToFilePath(uri);
                if (path != null && !path.isEmpty()) {
                    Log.i(TAG, "Incoming file open request: " + path);
                    if (sNativeReady) {
                        nativeOnFileOpened(path);
                    } else {
                        sPendingFilePath = path;
                    }
                }
            }
        }
    }

    /**
     * Resolves file:// or content:// URIs to accessible local filesystem paths.
     */
    public String resolveUriToFilePath(Uri uri) {
        if (uri == null) return null;
        if ("file".equalsIgnoreCase(uri.getScheme())) {
            return uri.getPath();
        }

        if ("content".equalsIgnoreCase(uri.getScheme())) {
            // Check DocumentContract for primary: or raw: doc ID
            try {
                if (android.provider.DocumentsContract.isDocumentUri(this, uri)) {
                    String docId = android.provider.DocumentsContract.getDocumentId(uri);
                    if (docId != null) {
                        if (docId.startsWith("primary:")) {
                            String rel = docId.substring("primary:".length());
                            return Environment.getExternalStorageDirectory().getAbsolutePath() + "/" + rel;
                        } else if (docId.startsWith("raw:")) {
                            return docId.substring("raw:".length());
                        } else if (docId.contains(":")) {
                            String[] split = docId.split(":");
                            String type = split[0];
                            String rel = split.length > 1 ? split[1] : "";
                            File candidate = new File("/storage/" + type + "/" + rel);
                            if (candidate.exists()) {
                                return candidate.getAbsolutePath();
                            }
                        }
                    }
                }
            } catch (Exception ignored) {}

            Cursor cursor = null;
            try {
                String[] projection = {"_data"};
                cursor = getContentResolver().query(uri, projection, null, null, null);
                if (cursor != null && cursor.moveToFirst()) {
                    int colIndex = cursor.getColumnIndex("_data");
                    if (colIndex != -1) {
                        String data = cursor.getString(colIndex);
                        if (data != null && new File(data).exists()) {
                            return data;
                        }
                    }
                }
            } catch (Exception ignored) {
            } finally {
                if (cursor != null) cursor.close();
            }

            try {
                String fileName = "imported_asset";
                Cursor nameCursor = getContentResolver().query(uri, null, null, null, null);
                if (nameCursor != null && nameCursor.moveToFirst()) {
                    int nameIdx = nameCursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                    if (nameIdx != -1) {
                        fileName = nameCursor.getString(nameIdx);
                    }
                    nameCursor.close();
                }

                File cacheFile = new File(getCacheDir(), fileName);
                try (InputStream in = getContentResolver().openInputStream(uri);
                     FileOutputStream out = new FileOutputStream(cacheFile)) {
                    if (in != null) {
                        byte[] buf = new byte[8192];
                        int len;
                        while ((len = in.read(buf)) > 0) {
                            out.write(buf, 0, len);
                        }
                        return cacheFile.getAbsolutePath();
                    }
                }
            } catch (Exception e) {
                Log.e(TAG, "Failed resolving content URI: " + e.getMessage());
            }
        }

        return uri.getPath();
    }

    // ========================================================================
    // Methods callable from C++ via QJniObject
    // ========================================================================

    public void setOrientationLandscape() {
        runOnUiThread(() -> setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE));
    }

    public void forceLandscape() {
        runOnUiThread(() -> setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE));
    }

    public void setOrientationPortrait() {
        runOnUiThread(() -> setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT));
    }

    public void setOrientationAuto() {
        runOnUiThread(() -> setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED));
    }

    public void setImmersiveMode(final boolean enable) {
        if (enable) {
            hideSystemBars();
        } else {
            runOnUiThread(() -> {
                try {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                        WindowInsetsController controller = getWindow().getInsetsController();
                        if (controller != null) {
                            controller.show(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                        }
                    } else {
                        View decor = getWindow().getDecorView();
                        if (decor != null) {
                            decor.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
                        }
                    }
                } catch (Exception e) {
                    Log.w(TAG, "setImmersiveMode: " + e.getMessage());
                }
            });
        }
    }

    public void vibrateTouch(int durationMs) {
        try {
            Vibrator v = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
            if (v != null && v.hasVibrator()) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    v.vibrate(VibrationEffect.createOneShot(durationMs, VibrationEffect.DEFAULT_AMPLITUDE));
                } else {
                    v.vibrate(durationMs);
                }
            }
        } catch (Exception ignored) {
        }
    }

    public boolean hasAllFilesAccess() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return Environment.isExternalStorageManager();
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            return checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
        }
        return true;
    }

    // ── Audio Player API ────────────────────────────────────────────────────
    private MediaPlayer mMediaPlayer = null;

    public boolean audioPlay(final String filePath) {
        try {
            audioStop();
            mMediaPlayer = new MediaPlayer();
            mMediaPlayer.setDataSource(filePath);
            mMediaPlayer.prepare();
            mMediaPlayer.start();
            return true;
        } catch (Exception e) {
            Log.e(TAG, "audioPlay error for " + filePath + ": " + e.getMessage());
            return false;
        }
    }

    public void audioPause() {
        try {
            if (mMediaPlayer != null && mMediaPlayer.isPlaying()) {
                mMediaPlayer.pause();
            }
        } catch (Exception ignored) {}
    }

    public void audioResume() {
        try {
            if (mMediaPlayer != null && !mMediaPlayer.isPlaying()) {
                mMediaPlayer.start();
            }
        } catch (Exception ignored) {}
    }

    public void audioStop() {
        try {
            if (mMediaPlayer != null) {
                if (mMediaPlayer.isPlaying()) {
                    mMediaPlayer.stop();
                }
                mMediaPlayer.release();
                mMediaPlayer = null;
            }
        } catch (Exception ignored) {}
    }

    public void audioSeek(int msec) {
        try {
            if (mMediaPlayer != null) {
                mMediaPlayer.seekTo(msec);
            }
        } catch (Exception ignored) {}
    }

    public int audioGetPosition() {
        try {
            if (mMediaPlayer != null) {
                return mMediaPlayer.getCurrentPosition();
            }
        } catch (Exception ignored) {}
        return 0;
    }

    public int audioGetDuration() {
        try {
            if (mMediaPlayer != null) {
                return mMediaPlayer.getDuration();
            }
        } catch (Exception ignored) {}
        return 0;
    }

    public boolean audioIsPlaying() {
        try {
            if (mMediaPlayer != null) {
                return mMediaPlayer.isPlaying();
            }
        } catch (Exception ignored) {}
        return false;
    }

    public static void launchGameActivity(String resourceDir) {
        launchGameActivity(resourceDir, null);
    }

    /**
     * Launch the in-engine runner. When {@code bootScene} is non-null the engine skips the
     * main menu and boots straight into {@code <bootScene>.scene} using its built-in test
     * profile ("testplayer") — the scene name must be a basename WITHOUT the .scene
     * extension, and it must resolve inside {@code resourceDir}.
     */
    public static void launchGameActivity(String resourceDir, String bootScene) {
        if (sInstance == null) return;
        sInstance.runOnUiThread(() -> {
            try {
                Intent intent = new Intent(sInstance, GameActivity.class);
                intent.putExtra("resource_dir", resourceDir);
                if (bootScene != null && !bootScene.isEmpty()) {
                    intent.putExtra("boot_scene", bootScene);
                }
                sInstance.startActivity(intent);
            } catch (Exception e) {
                Log.e(TAG, "Failed to launch GameActivity", e);
            }
        });
    }

    public void pickFolder(final String tag) {
        sPendingPickerTag = tag;
        runOnUiThread(() -> {
            try {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                startActivityForResult(intent, REQUEST_PICK_FOLDER);
            } catch (Exception e) {
                Log.e(TAG, "pickFolder failed: " + e.getMessage(), e);
            }
        });
    }

    public void pickFile(final String tag, final String filterType) {
        sPendingPickerTag = tag;
        runOnUiThread(() -> {
            try {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                intent.setType("*/*");
                if ("models".equalsIgnoreCase(filterType) || "3d".equalsIgnoreCase(filterType)) {
                    String[] mimes = {
                        "model/gltf-binary",
                        "model/gltf+json",
                        "application/octet-stream",
                        "*/*"
                    };
                    intent.putExtra(Intent.EXTRA_MIME_TYPES, mimes);
                }
                startActivityForResult(intent, REQUEST_PICK_FILE);
            } catch (Exception e) {
                Log.e(TAG, "pickFile failed: " + e.getMessage(), e);
            }
        });
    }

    public static void openCodeEditor(final String filePath) {
        if (sInstance == null) return;
        sInstance.runOnUiThread(() -> {
            try {
                Intent intent = new Intent(sInstance, CodeEditorActivity.class);
                intent.putExtra("file_path", filePath);
                sInstance.startActivityForResult(intent, REQUEST_CODE_EDITOR);
            } catch (Exception e) {
                Log.e(TAG, "Failed to launch CodeEditorActivity: " + e.getMessage(), e);
            }
        });
    }

    public static String getPathFromTreeUri(Context context, Uri treeUri) {
        if (treeUri == null) return null;
        try {
            String docId = android.provider.DocumentsContract.getTreeDocumentId(treeUri);
            if (docId != null) {
                String[] split = docId.split(":");
                String type = split[0];
                String relativePath = split.length > 1 ? split[1] : "";
                if ("primary".equalsIgnoreCase(type)) {
                    return Environment.getExternalStorageDirectory().getAbsolutePath() + "/" + relativePath;
                } else {
                    File candidate = new File("/storage/" + type + "/" + relativePath);
                    if (candidate.exists()) {
                        return candidate.getAbsolutePath();
                    }
                    return "/storage/" + type + "/" + relativePath;
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "getPathFromTreeUri error: " + e.getMessage());
        }
        return treeUri.getPath();
    }
}
