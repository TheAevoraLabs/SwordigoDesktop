package net.kiwi.launcher;

import android.app.Activity;

/**
 * Compatibility stub for Launcher mods that query net.kiwi.launcher.MainActivity.
 */
public class MainActivity extends Activity {
    private static String sCurrentMod = "";

    public static String currentMod() {
        return sCurrentMod;
    }

    public static void setCurrentMod(String mod) {
        sCurrentMod = mod != null ? mod : "";
    }
}
