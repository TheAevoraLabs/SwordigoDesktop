package in.aevora.ruby;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.channels.FileChannel;
import java.util.Enumeration;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * VanillaAssetManager — Steals and manages pristine vanilla assets from com.touchfoo.swordigo.
 * 
 * Flow:
 *  1. At runtime, locates com.touchfoo.swordigo APK.
 *  2. Extracts all assets/resources/* to internal cache (vanilla_assets/) once.
 *  3. On project creation, seeds project/ directory from this pristine cache.
 *  4. Copies bundled devtime/runtime plugins (libluajitcli.so, libswdbg.so) to plugins/<abi>/.
 *  5. Serves as reference directory for diffing and file-revert operations.
 */
public class VanillaAssetManager {
    private static final String TAG = "VanillaAssetManager";
    public static final String TARGET_PACKAGE = "com.touchfoo.swordigo";
    private static final String CACHE_DIR_NAME = "vanilla_assets";
    private static final String MARKER_FILE = ".extracted";

    public interface ProgressCallback {
        void onProgress(int percent, String message);
    }

    public static native void nativeOnSeedProgress(int percent, String message);

    public static void reportProgress(int percent, String message) {
        try {
            nativeOnSeedProgress(percent, message);
        } catch (Throwable ignored) {}
    }

    public static File getVanillaCacheDir(Context context) {
        return new File(context.getFilesDir(), CACHE_DIR_NAME);
    }

    public static String getVanillaCachePath() {
        RubyActivity act = RubyActivity.getInstance();
        if (act == null) return "";
        return getVanillaCacheDir(act).getAbsolutePath();
    }

    public static boolean isVanillaCacheReady(Context context) {
        File cacheDir = getVanillaCacheDir(context);
        File marker = new File(cacheDir, MARKER_FILE);
        return marker.exists() && cacheDir.isDirectory();
    }

    private static String[] collectApkPaths(ApplicationInfo info) {
        if (info.splitSourceDirs == null || info.splitSourceDirs.length == 0) {
            return new String[]{ info.sourceDir };
        }
        String[] paths = new String[info.splitSourceDirs.length + 1];
        paths[0] = info.sourceDir;
        System.arraycopy(info.splitSourceDirs, 0, paths, 1, info.splitSourceDirs.length);
        return paths;
    }

    /**
     * Extracts all assets/resources/* from com.touchfoo.swordigo APK into internal vanilla_assets/.
     */
    public static synchronized boolean extractVanillaAssets(Context context, ProgressCallback callback) {
        File cacheDir = getVanillaCacheDir(context);
        if (isVanillaCacheReady(context)) {
            Log.i(TAG, "Vanilla assets already cached at: " + cacheDir.getAbsolutePath());
            if (callback != null) callback.onProgress(100, "Vanilla assets ready");
            return true;
        }

        if (!cacheDir.exists() && !cacheDir.mkdirs()) {
            Log.e(TAG, "Failed to create vanilla cache directory: " + cacheDir.getAbsolutePath());
            return false;
        }

        try {
            PackageManager pm = context.getPackageManager();
            ApplicationInfo info = pm.getApplicationInfo(TARGET_PACKAGE, 0);
            String[] apkPaths = collectApkPaths(info);

            if (callback != null) callback.onProgress(5, "Scanning vanilla game APK...");

            // First count entries for progress tracking
            int totalEntries = 0;
            for (String apkPath : apkPaths) {
                try (ZipFile zip = new ZipFile(apkPath)) {
                    Enumeration<? extends ZipEntry> e = zip.entries();
                    while (e.hasMoreElements()) {
                        ZipEntry entry = e.nextElement();
                        if (entry.getName().startsWith("assets/resources/") && !entry.isDirectory()) {
                            totalEntries++;
                        }
                    }
                }
            }

            if (totalEntries == 0) {
                Log.w(TAG, "No assets/resources/ entries found in vanilla Swordigo APK");
                return false;
            }

            int extracted = 0;
            byte[] buffer = new byte[65536];

            for (String apkPath : apkPaths) {
                try (ZipFile zip = new ZipFile(apkPath)) {
                    Enumeration<? extends ZipEntry> e = zip.entries();
                    while (e.hasMoreElements()) {
                        ZipEntry entry = e.nextElement();
                        String name = entry.getName();
                        if (name.startsWith("assets/resources/") && !entry.isDirectory()) {
                            String relName = name.substring("assets/resources/".length());
                            File outFile = new File(cacheDir, relName);
                            File parent = outFile.getParentFile();
                            if (parent != null && !parent.exists()) {
                                parent.mkdirs();
                            }

                            try (InputStream in = zip.getInputStream(entry);
                                 OutputStream out = new FileOutputStream(outFile)) {
                                int read;
                                while ((read = in.read(buffer)) != -1) {
                                    out.write(buffer, 0, read);
                                }
                            }

                            extracted++;
                            if (callback != null && (extracted % 30 == 0 || extracted == totalEntries)) {
                                int pct = 5 + (int) ((extracted / (float) totalEntries) * 45);
                                callback.onProgress(pct, "Extracting vanilla assets (" + extracted + "/" + totalEntries + ")");
                            }
                        }
                    }
                }
            }

            // Write marker file to indicate full extraction completed
            File marker = new File(cacheDir, MARKER_FILE);
            marker.createNewFile();
            Log.i(TAG, "Successfully extracted " + extracted + " vanilla assets to " + cacheDir.getAbsolutePath());
            return true;
        } catch (PackageManager.NameNotFoundException e) {
            Log.e(TAG, "Vanilla Swordigo (" + TARGET_PACKAGE + ") is not installed!", e);
            new Handler(Looper.getMainLooper()).post(() -> {
                Toast.makeText(context, "Vanilla Swordigo (" + TARGET_PACKAGE + ") is not installed on this device.", Toast.LENGTH_LONG).show();
            });
            return false;
        } catch (Exception e) {
            Log.e(TAG, "Failed to extract vanilla assets: " + e.getMessage(), e);
            return false;
        }
    }

    /**
     * Seeds project/ folder with vanilla assets and plugins/<abi>/ with bundled native libraries.
     */
    public static boolean seedProject(String projectPath, String[] abis) {
        RubyActivity act = RubyActivity.getInstance();
        if (act == null) {
            Log.e(TAG, "RubyActivity instance is null, cannot seed project");
            return false;
        }
        return seedProject(act, projectPath, abis, (percent, message) -> {
            reportProgress(percent, message);
        });
    }

    public static boolean seedProject(Context context, String projectPath, String[] abis, ProgressCallback callback) {
        Log.i(TAG, "Seeding project at: " + projectPath);

        // 1. Ensure vanilla assets cache is ready
        if (!isVanillaCacheReady(context)) {
            Log.i(TAG, "Vanilla cache missing or incomplete, extracting now...");
            if (!extractVanillaAssets(context, callback)) {
                Log.e(TAG, "Could not extract vanilla assets, project seeding failed");
                return false;
            }
        }

        File cacheDir = getVanillaCacheDir(context);
        File projectDir = new File(projectPath);
        File projectResourcesDir = new File(projectDir, "project");

        if (!projectResourcesDir.exists() && !projectResourcesDir.mkdirs()) {
            Log.e(TAG, "Failed to create project resources dir: " + projectResourcesDir.getAbsolutePath());
            return false;
        }

        // 2. Copy vanilla assets to project/ using zero-copy FileChannel
        File[] assets = cacheDir.listFiles();
        if (assets != null && assets.length > 0) {
            int total = assets.length;
            int copied = 0;

            for (File src : assets) {
                if (src.getName().equals(MARKER_FILE)) continue;

                File dest = new File(projectResourcesDir, src.getName());
                try (FileInputStream fis = new FileInputStream(src);
                     FileOutputStream fos = new FileOutputStream(dest);
                     FileChannel inChannel = fis.getChannel();
                     FileChannel outChannel = fos.getChannel()) {
                    inChannel.transferTo(0, inChannel.size(), outChannel);
                } catch (IOException e) {
                    Log.w(TAG, "Error copying " + src.getName() + " to project: " + e.getMessage());
                }

                copied++;
                if (callback != null && (copied % 40 == 0 || copied == total)) {
                    int pct = 50 + (int) ((copied / (float) total) * 45);
                    callback.onProgress(pct, "Seeding project assets (" + copied + "/" + total + ")");
                }
            }
            Log.i(TAG, "Seeded " + copied + " files into " + projectResourcesDir.getAbsolutePath());
        }

        // 3. Copy bundled plugins from APK assets/plugins/<abi>/*
        AssetManager am = context.getAssets();
        if (abis != null) {
            for (String abi : abis) {
                String assetPluginDir = "plugins/" + abi;
                File targetPluginDir = new File(projectDir, "plugins/" + abi);
                if (!targetPluginDir.exists()) targetPluginDir.mkdirs();

                try {
                    String[] plugins = am.list(assetPluginDir);
                    if (plugins != null) {
                        byte[] buf = new byte[32768];
                        for (String pluginName : plugins) {
                            File destPlugin = new File(targetPluginDir, pluginName);
                            try (InputStream in = am.open(assetPluginDir + "/" + pluginName);
                                 OutputStream out = new FileOutputStream(destPlugin)) {
                                int r;
                                while ((r = in.read(buf)) != -1) {
                                    out.write(buf, 0, r);
                                }
                            }
                            Log.i(TAG, "Copied bundled plugin " + pluginName + " to " + targetPluginDir.getAbsolutePath());
                        }
                    }
                } catch (IOException e) {
                    Log.w(TAG, "No assets found in " + assetPluginDir + ": " + e.getMessage());
                }
            }
        }

        if (callback != null) {
            callback.onProgress(100, "Project successfully seeded with vanilla assets");
        }
        return true;
    }
}
