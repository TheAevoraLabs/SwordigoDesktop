package com.touchfoo.swordigo;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.content.res.AssetManager;

@SuppressLint("all")
@SuppressWarnings("all")
public class Native {

    public static Object mainActivity;

    public static native void setFilesDir(String path);
    public static native void setCacheDir(String path);
    public static native void setAssetManager(AssetManager assetManager);
    public static native void handleApplicationLaunch();
    public static native void setupNativeInterface();
    public static native void setupApplication();
    public static native void updateApplication(float dt);
    public static native void drawApplication();
    public static native void reloadContext();
    public static native void handleApplicationQuit();
    public static native void applicationDidBecomeActive();
    public static native void applicationDidBecomeInactive();
    public static native void applicationDidEnterBackground();
    public static native void applicationDidEnterForeground();
    public static native void handleBackButtonPress();
    public static native void handleMenuButtonPress();
    public static native void handleTouchEvent(int i, int i2, double d,
                                               float f, float f2,
                                               float f3, float f4, int i3);
    public static native String uniqueIdentifier();
    public static native void setApplicationViewSize(int width, int height,
                                                     boolean b, int i3, int i4);
    public static native void textInputTextDidChange(String text);
    public static native void textInputDidFinish();

    public static boolean isGoogleGameServicesAvailable() {
        return false;
    }

    public static void loadSnapshot(String name, double unused) {
        // stub
    }

    public static void deleteSnapshot(String name) {
        // stub
        Log.i("SwordigoRuntime", "deleteSnapshot stub: " + name);
    }

    public static int getInterstitialAdInterval(String placement, int defaultValue) {
        return Integer.MAX_VALUE;
    }


    public static void interstitialAdVisibilityChanged(boolean visible) { }
    public static void loadInterstitialAd() {
    }

    public static void productPurchased(String productId) { }
    public static void productPurchaseFailed(String productId, String error) { }
    public static void startedRestoringPurchases() { }
    public static void finishedRestoringPurchases() { }
    public static void storeProductFetched(String productId, String price, String title) { }
    public static void storeProductFetchFailed(String productId, String error) { }
    public static String getStoreName() {
        return "play_store";
    }

    // --- The Predicted Missing Stubs (C++ -> Java) ---
    // (Add these to prevent future CallStaticVoidMethod crashes)
    public static void signInToGoogleGameServices() { }

    public static void showAchievements() { }

    public static void showLeaderboards() { }

    public static void saveSnapshot(String name, byte[] data, String description) { }


    public static void queueStoreProductFetch(String productId) {
        if (com.L.SwordigoRuntime.GameView.instance != null) {
            com.L.SwordigoRuntime.GameView.instance.queueEvent(() -> {
                try {
                    Native.storeProductFetchFailed(productId, "no_store_backend");
                } catch (Throwable t) {
                    Log.e("SwordigoRuntime", "storeProductFetchFailed failed", t);
                }
            });
        }
    }

    public static void processStoreQueue() {
    }

    // social stuff
    public static void googleSignInCompleted(boolean success) { }
    public static void reviewFlowCompleted() { }
    public static void reportAchievementProgress(String achievementId, int progress, boolean completed) {
    }
    public static void openURL(String url) {



        if (mainActivity != null && mainActivity instanceof Activity) {
            Activity activity = (Activity) mainActivity;
            activity.runOnUiThread(() -> {
                try {
                    android.content.Intent intent = new android.content.Intent(android.content.Intent.ACTION_VIEW);
                    intent.setData(android.net.Uri.parse(url));
                    activity.startActivity(intent);
                } catch (Exception e) {
                    android.util.Log.e("SwordigoRuntime", "Failed to open URL: " + url, e);
                }
            });
        } else {
            android.util.Log.e("SwordigoRuntime", "Cannot open URL: mainActivity is null or not an Activity.");
        }
    }

    public static void startTextInput(String initialText) {
        Activity activity = (Activity) mainActivity;
        if (activity == null || com.L.SwordigoRuntime.GameView.instance == null) {
            Log.w("SwordigoRuntime", "Cant start text input");
            return;
        }

        activity.runOnUiThread(() -> {
            EditText editText = new EditText(activity);

            // 1. Prevent Android from opening the full-screen landscape text box (Extract UI)
            editText.setImeOptions(android.view.inputmethod.EditorInfo.IME_FLAG_NO_EXTRACT_UI
                    | android.view.inputmethod.EditorInfo.IME_ACTION_DONE);

            // 2. Disable autocorrect/suggestions bar if you want only the keyboard keys showing
            editText.setInputType(android.text.InputType.TYPE_CLASS_TEXT
                    | android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);

            // 3. Make text, background, and cursor invisible
            editText.setBackground(null);
            editText.setTextColor(android.graphics.Color.TRANSPARENT);
            editText.setCursorVisible(false);
            editText.setAlpha(0.0f);

            editText.setText(initialText != null ? initialText : "");
            editText.setFocusable(true);
            editText.setFocusableInTouchMode(true);

            ViewGroup root = activity.findViewById(android.R.id.content);
            root.addView(editText, new ViewGroup.LayoutParams(1, 1));

            editText.requestFocus();

            InputMethodManager imm = (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);
            editText.postDelayed(() -> {
                imm.showSoftInput(editText, InputMethodManager.SHOW_FORCED);
            }, 100);

            editText.addTextChangedListener(new TextWatcher() {
                @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
                @Override public void onTextChanged(CharSequence s, int start, int before, int count) {
                    final String text = s.toString();
                    com.L.SwordigoRuntime.GameView.instance.queueEvent(() -> {
                        try {
                            Native.textInputTextDidChange(text);
                        } catch (Throwable t) {
                            Log.e("SwordigoRuntime", "textInputTextDidChange failed", t);
                        }
                    });
                }
                @Override public void afterTextChanged(Editable s) {}
            });

            editText.setOnEditorActionListener((v, actionId, event) -> {
                if (actionId == android.view.inputmethod.EditorInfo.IME_ACTION_DONE ||
                        (event != null && event.getAction() == android.view.KeyEvent.ACTION_DOWN &&
                                event.getKeyCode() == android.view.KeyEvent.KEYCODE_ENTER)) {
                    imm.hideSoftInputFromWindow(editText.getWindowToken(), 0);
                    root.removeView(editText);
                    com.L.SwordigoRuntime.GameView.instance.queueEvent(() -> {
                        try {
                            Native.textInputDidFinish();
                        } catch (Throwable t) {
                            Log.e("SwordigoRuntime", "textInputDidFinish failed", t);
                        }
                    });
                    return true;
                }
                return false;
            });
        });
    }

    public static void stopTextInput() {
        Activity activity = (Activity) mainActivity;
        if (activity == null) {
            Log.w("SwordigoRuntime", "cant stop text input");
            return;
        }

        activity.runOnUiThread(() -> {
            InputMethodManager imm = (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);
            View currentFocus = activity.getCurrentFocus();
            if (currentFocus instanceof EditText) {
                imm.hideSoftInputFromWindow(currentFocus.getWindowToken(), 0);
                ViewGroup root = activity.findViewById(android.R.id.content);
                root.removeView(currentFocus);
            }
        });
    }
}