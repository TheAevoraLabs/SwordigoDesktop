package in.aevora.ruby;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import com.L.SwordigoRuntime.GameRenderer;
import com.L.SwordigoRuntime.GameView;
import com.touchfoo.swordigo.Native;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.lang.reflect.Method;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

public class GameActivity extends Activity {
    private static final String TAG = "RubyGameActivity";

    public static native void setResourceDirectory(String dirPath);
    public static native void loadHooks();
    public static native void unloadHooks();

    /**
     * Arm a direct boot into a specific scene. Pass the scene BASENAME without the
     * ".scene" extension — the engine appends it. Must be called after {@link #loadHooks()}
     * and before the GL surface reports its first size (i.e. before the engine's
     * setApplicationViewSize), which is why GameActivity does it in onCreate. Pass null to
     * boot the normal main menu.
     */
    public static native void setBootScene(String sceneName);

    /** True when the engine hook behind {@link #setBootScene} is installed. */
    public static native boolean isBootSceneReady();

    private GameView glSurfaceView;
    private String targetApkPath;
    private String[] targetApkPaths;
    private boolean hooksLoaded = false;
    private static AssetManager gameAssetManager;

    public static String getAbi() {
        if (Build.SUPPORTED_ABIS.length > 0) {
            String primary = Build.SUPPORTED_ABIS[0];
            if ("arm64-v8a".equals(primary) || primary.contains("arm64")) return "arm64-v8a";
        }
        return "armeabi-v7a";
    }

    public File getExtractedPath() {
        File dir = new File(getFilesDir(), "Extracted/lib/" + getAbi());
        if (!dir.exists() && !dir.mkdirs() && !dir.isDirectory()) {
            Log.w(TAG, "extract dir creation failed");
        }
        return dir;
    }

    private static String[] collectApkPaths(ApplicationInfo info) {
        if (info.splitSourceDirs == null || info.splitSourceDirs.length == 0)
            return new String[]{info.sourceDir};
        String[] paths = new String[info.splitSourceDirs.length + 1];
        paths[0] = info.sourceDir;
        System.arraycopy(info.splitSourceDirs, 0, paths, 1, info.splitSourceDirs.length);
        return paths;
    }

    @SuppressLint("SetWorldReadable")
    private void extractLibFromZip(String[] apkPaths, String zipEntryPath, File destFile) throws IOException {
        if (destFile.exists() && destFile.length() > 0) {
            destFile.setReadable(true, false);
            destFile.setExecutable(true, false);
            return;
        }
        for (String apkPath : apkPaths) {
            try (ZipFile zipFile = new ZipFile(apkPath)) {
                ZipEntry entry = zipFile.getEntry(zipEntryPath);
                if (entry == null) continue;
                try (InputStream in = zipFile.getInputStream(entry);
                     FileOutputStream out = new FileOutputStream(destFile)) {
                    byte[] buf = new byte[8192];
                    int n;
                    while ((n = in.read(buf)) != -1) out.write(buf, 0, n);
                }
                destFile.setReadable(true, false);
                destFile.setExecutable(true, false);
                return;
            }
        }
        throw new java.io.FileNotFoundException("missing " + zipEntryPath + " in split APKs");
    }

    @Override
    @SuppressLint("UnsafeDynamicallyLoadedCode")
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN, WindowManager.LayoutParams.FLAG_FULLSCREEN);
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);

        String resourceDir = getIntent().getStringExtra("resource_dir");
        String bootScene = getIntent().getStringExtra("boot_scene");
        Log.i(TAG, "Starting GameActivity with resource_dir: " + resourceDir
                + (bootScene != null ? (", boot_scene: " + bootScene) : ""));

        String abi = getAbi();
        File libDir = getExtractedPath();
        File openAlFile = new File(libDir, "libopenal-soft.so");
        File swordigoFile = new File(libDir, "libswordigo.so");

        try {
            ApplicationInfo info = getPackageManager().getApplicationInfo("com.touchfoo.swordigo", 0);
            targetApkPath = info.sourceDir;
            targetApkPaths = collectApkPaths(info);
            extractLibFromZip(targetApkPaths, "lib/" + abi + "/libopenal-soft.so", openAlFile);
            extractLibFromZip(targetApkPaths, "lib/" + abi + "/libswordigo.so", swordigoFile);
        } catch (PackageManager.NameNotFoundException e) {
            Log.e(TAG, "Vanilla Swordigo (com.touchfoo.swordigo) is not installed", e);
            Toast.makeText(this, "Vanilla Swordigo (com.touchfoo.swordigo) is not installed on this device.", Toast.LENGTH_LONG).show();
            finish();
            return;
        } catch (Exception e) {
            Log.e(TAG, "Failed extracting Swordigo libraries", e);
            Toast.makeText(this, "Failed extracting game engine: " + e.getMessage(), Toast.LENGTH_LONG).show();
            finish();
            return;
        }

        try {
            System.loadLibrary("GlossHook");
            System.load(openAlFile.getAbsolutePath());
            System.load(swordigoFile.getAbsolutePath());
            System.loadLibrary("ruby_runner");

            setResourceDirectory(resourceDir);
            loadHooks();
            hooksLoaded = true;

            // Arm the direct scene boot now: libswordigo.so is loaded, the InitView hook is
            // installed, and the engine's own setupApplication (which resets the boot-scene
            // field to "menu") has not run yet — that happens in the first onSurfaceCreated.
            if (bootScene != null && !bootScene.isEmpty()) {
                if (isBootSceneReady()) {
                    setBootScene(bootScene);
                    Log.i(TAG, "Direct scene boot armed for '" + bootScene + "'");
                } else {
                    Log.w(TAG, "Direct scene boot requested for '" + bootScene
                            + "' but the engine hook is not installed; booting the main menu");
                }
            }
        } catch (Throwable t) {
            Log.e(TAG, "Failed loading game libraries or hooks", t);
            Toast.makeText(this, "Error initializing engine: " + t.getMessage(), Toast.LENGTH_LONG).show();
            finish();
            return;
        }

        FrameLayout gameRoot = new FrameLayout(this);
        gameRoot.setLayoutParams(new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        gameRoot.setBackgroundColor(Color.BLACK);

        glSurfaceView = new GameView(this);
        glSurfaceView.setEGLConfigChooser(5, 6, 5, 0, 16, 0);
        glSurfaceView.setPreserveEGLContextOnPause(true);
        glSurfaceView.setRenderer(new GameRenderer());
        gameRoot.addView(glSurfaceView, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        // Floating top-center return overlay button
        addOverlayReturnButton(gameRoot);

        setContentView(gameRoot);
        enableImmersiveMode();

        setupNativeEnvironment(targetApkPath);
    }

    private void addOverlayReturnButton(FrameLayout parent) {
        int dp44 = (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 44, getResources().getDisplayMetrics());
        int dp10 = (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 10, getResources().getDisplayMetrics());
        int dp8 = (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 8, getResources().getDisplayMetrics());

        LinearLayout btn = new LinearLayout(this);
        btn.setOrientation(LinearLayout.HORIZONTAL);
        btn.setGravity(Gravity.CENTER);
        btn.setPadding(dp10, dp8, dp10, dp8);

        GradientDrawable bg = new GradientDrawable();
        bg.setShape(GradientDrawable.RECTANGLE);
        bg.setCornerRadius(dp44 / 2.0f);
        bg.setColor(Color.parseColor("#D916181D"));
        bg.setStroke(1, Color.parseColor("#66FFFFFF"));
        btn.setBackground(bg);

        TextView icon = new TextView(this);
        icon.setText("<");
        icon.setTextColor(Color.parseColor("#61AFEF"));
        icon.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        icon.setTypeface(Typeface.DEFAULT_BOLD);
        btn.addView(icon);

        TextView label = new TextView(this);
        label.setText(" Ruby");
        label.setTextColor(Color.WHITE);
        label.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        label.setTypeface(Typeface.DEFAULT_BOLD);
        btn.addView(label);

        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT,
                (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 32, getResources().getDisplayMetrics())
        );
        lp.gravity = Gravity.TOP | Gravity.CENTER_HORIZONTAL;
        lp.topMargin = (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, 12, getResources().getDisplayMetrics());
        btn.setLayoutParams(lp);

        btn.setOnClickListener(v -> {
            Log.i(TAG, "Return overlay button pressed, finishing GameActivity");
            finish();
        });

        parent.addView(btn);
    }

    private void enableImmersiveMode() {
        if (glSurfaceView == null) return;
        int flags = View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE;
        glSurfaceView.setSystemUiVisibility(flags);
    }

    private void setupNativeEnvironment(String apkPath) {
        try {
            Native.mainActivity = this;
            Native.setFilesDir(getApplicationContext().getFilesDir().toString());
            Native.setCacheDir(getApplicationContext().getCacheDir().toString());
            AssetManager am = buildAssetManager(apkPath);
            if (am == null) {
                Log.e(TAG, "Failed building AssetManager for apk: " + apkPath);
                finish();
                return;
            }
            Native.setAssetManager(am);
            Native.handleApplicationLaunch();
        } catch (Throwable t) {
            Log.e(TAG, "setupNativeEnvironment failed", t);
        }
    }

    private static AssetManager buildAssetManager(String apkPath) {
        try {
            AssetManager am = AssetManager.class.getDeclaredConstructor().newInstance();
            Method addAssetPath = AssetManager.class.getMethod("addAssetPath", String.class);
            Object result = addAssetPath.invoke(am, apkPath);
            boolean ok = (result instanceof Integer) && ((Integer) result) != 0;
            return ok ? am : null;
        } catch (Exception e) {
            return null;
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (glSurfaceView != null) glSurfaceView.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (glSurfaceView != null) {
            glSurfaceView.onResume();
            enableImmersiveMode();
        }
    }

    @Override
    public void onBackPressed() {
        Log.i(TAG, "onBackPressed: finishing GameActivity");
        finish();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (hooksLoaded) {
            try {
                unloadHooks();
            } catch (Throwable t) {
                Log.e(TAG, "unloadHooks failed", t);
            }
            hooksLoaded = false;
        }
        if (glSurfaceView != null) {
            glSurfaceView.onPause();
            glSurfaceView = null;
        }
        // Dedicated process isolation (:game):
        // Cleanly terminate this engine process so subsequent launches always start
        // with pristine C++ static variables, clean singletons, and fresh EGL state.
        Log.i(TAG, "onDestroy: Terminating :game process for clean relaunch");
        android.os.Process.killProcess(android.os.Process.myPid());
    }
}
