package in.aevora.ruby;

import android.app.Activity;
import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.media.AudioAttributes;
import android.media.MediaPlayer;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;

public class Port {
    private static final String TAG = "RubyPort";

    private static MediaPlayer player;
    private static String currentTrack = "";
    private static String pendingTrack = "";
    private static boolean preparing;
    private static boolean playWhenReady;
    private static boolean looping;
    private static float volume = 1f;
    private static String modResourceDir = "";

    public static void setResourceDir(String dir) {
        modResourceDir = (dir != null) ? dir : "";
        Log.i(TAG, "Port modResourceDir set to: " + modResourceDir);
    }

    private static void ensure() {
        if (player != null) return;
        player = new MediaPlayer();
        player.setAudioAttributes(new AudioAttributes.Builder()
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .setUsage(AudioAttributes.USAGE_GAME)
                .build());
        player.setOnPreparedListener(mp -> {
            preparing = false;
            if (!currentTrack.equals(pendingTrack)) {
                prepare();
                return;
            }
            if (!preparing && player != null) {
                try {
                    player.setLooping(looping);
                    player.setVolume(volume, volume);
                } catch (Throwable ignored) {}
            }
            if (playWhenReady && player != null) {
                playWhenReady = false;
                try { player.start(); } catch (IllegalStateException ignored) {}
            }
        });
        player.setOnErrorListener((mp, what, extra) -> {
            Log.w(TAG, "MediaPlayer error: what=" + what + " extra=" + extra);
            preparing = false;
            return true;
        });
    }

    public static boolean loadFile(String track) {
        if (track == null || track.isEmpty()) return false;
        Log.i(TAG, "loadFile requested for: " + track);
        ensure();
        pendingTrack = track;
        playWhenReady = false;
        return preparing || prepare();
    }

    public static void play() {
        Log.d(TAG, "play() called");
        ensure();
        if (preparing) {
            playWhenReady = true;
            return;
        }
        try { player.start(); } catch (IllegalStateException ignored) {}
    }

    public static void pause() {
        Log.d(TAG, "pause() called");
        playWhenReady = false;
        if (player == null || preparing || currentTrack.isEmpty()) return;
        try { player.pause(); } catch (IllegalStateException ignored) {}
    }

    public static void stop() {
        Log.d(TAG, "stop() called");
        playWhenReady = false;
        if (player == null || preparing || currentTrack.isEmpty()) return;
        try {
            player.stop();
            player.reset();
        } catch (IllegalStateException ignored) {}
        currentTrack = "";
    }

    public static void setLooping(boolean loop) {
        looping = loop;
        if (player != null && !preparing) {
            try { player.setLooping(loop); } catch (IllegalStateException ignored) {}
        }
    }

    public static void setVolume(float vol) {
        volume = vol;
        if (player != null && !preparing) {
            try { player.setVolume(vol, vol); } catch (IllegalStateException ignored) {}
        }
    }

    public static void onGamePause() {
        pause();
    }

    public static void onGameResume() {
        if (player == null || preparing || currentTrack.isEmpty()) return;
        try { player.start(); } catch (IllegalStateException ignored) {}
    }

    public static void onGameStop() {
        playWhenReady = false;
        preparing = false;
        if (player != null) {
            try {
                player.stop();
                player.reset();
                player.release();
            } catch (IllegalStateException ignored) {}
            player = null;
        }
        currentTrack = "";
        pendingTrack = "";
    }

    private static boolean prepare() {
        if (currentTrack.equals(pendingTrack) && !currentTrack.isEmpty()) return true;
        ensure();
        try {
            if (!currentTrack.isEmpty()) {
                player.stop();
                player.reset();
            }
            File modFile = resolveModFile(pendingTrack);
            if (modFile != null) {
                Log.i(TAG, "Playing music from mod file: " + modFile.getAbsolutePath());
                try (FileInputStream fis = new FileInputStream(modFile)) {
                    player.setDataSource(fis.getFD());
                }
            } else if (!openFromApk(pendingTrack)) {
                Log.w(TAG, "Could not open music track: " + pendingTrack);
                currentTrack = "";
                return false;
            }
            preparing = true;
            currentTrack = pendingTrack;
            player.prepareAsync();
            return true;
        } catch (Exception e) {
            Log.e(TAG, "Failed to prepare music player", e);
            preparing = false;
            currentTrack = "";
            return false;
        }
    }

    private static File resolveModFile(String track) {
        if (modResourceDir == null || modResourceDir.isEmpty()) return null;
        String norm = track.replace('-', '_');
        File dir = new File(modResourceDir);
        File musicDir = new File(modResourceDir, "music");
        File resMusicDir = new File(modResourceDir, "resources/music");

        File[] candidates = {
            new File(musicDir, norm + ".mp3"),
            new File(musicDir, norm),
            new File(musicDir, track + ".mp3"),
            new File(musicDir, track),
            new File(musicDir, "music_" + norm + ".mp3"),
            new File(musicDir, "music_" + track + ".mp3"),
            new File(resMusicDir, norm + ".mp3"),
            new File(resMusicDir, track + ".mp3"),
            new File(dir, norm + ".mp3"),
            new File(dir, track + ".mp3"),
            new File(dir, "music_" + norm + ".mp3")
        };
        for (File f : candidates) {
            if (f.isFile() && f.length() > 0) return f;
        }
        return null;
    }

    private static boolean openFromApk(String track) {
        Activity act = GameActivity.getCurrentActivity();
        if (act == null) {
            Log.w(TAG, "GameActivity.getCurrentActivity() is null, cannot open APK resources");
            return false;
        }
        String base = track.replace('-', '_');
        if (base.startsWith("music_")) base = base.substring(6);
        String[] names = {"music_" + base, base};
        try {
            Context game = act.createPackageContext("com.touchfoo.swordigo", Context.CONTEXT_IGNORE_SECURITY);
            for (String name : names) {
                int id = game.getResources().getIdentifier(name, "raw", "com.touchfoo.swordigo");
                if (id == 0) continue;
                AssetFileDescriptor afd = game.getResources().openRawResourceFd(id);
                if (afd == null) continue;
                player.setDataSource(afd.getFileDescriptor(), afd.getStartOffset(), afd.getLength());
                afd.close();
                Log.i(TAG, "Loaded music track '" + name + "' from vanilla Swordigo APK");
                return true;
            }
        } catch (Exception e) {
            Log.w(TAG, "createPackageContext openRawResourceFd failed for track " + track, e);
        }
        return false;
    }
}
