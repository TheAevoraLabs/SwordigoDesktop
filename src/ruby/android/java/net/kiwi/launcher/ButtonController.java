package net.kiwi.launcher;

import net.kiwi.overlay.Controller;

/**
 * Legacy compatibility bridge for Swordigo Launcher mods (such as LuaJIT CLI)
 * that target net.kiwi.launcher.ButtonController.
 */
public final class ButtonController {
    private ButtonController() {}

    public static void addButton(String id, String label, float nx, float ny, float w, float h) {
        Controller.addButton(id, label, nx, ny, w, h);
    }

    public static void setHidden(String id, boolean hidden) {
        Controller.setHidden(id, hidden);
    }

    public static boolean isPressed(String id) {
        return Controller.isPressed(id);
    }

    public static void setText(String id, String text) {
        Controller.setText(id, text);
    }

    public static void setTextSizeSp(String id, float sp) {
        Controller.setTextSizeSp(id, sp);
    }

    public static void setAlignment(String id, int gravity) {
        Controller.setAlignment(id, gravity);
    }

    public static void setPadding(String id, int left, int top, int right, int bottom) {
        Controller.setPadding(id, left, top, right, bottom);
    }

    public static void setBackgroundColor(String id, int color) {
        Controller.setBackgroundColor(id, color);
    }

    public static void setBackgroundAlpha(String id, int alpha) {
        Controller.setBackgroundAlpha(id, alpha);
    }

    public static void setTextColor(String id, int color) {
        Controller.setTextColor(id, color);
    }

    public static void addTextInput(String id, float nx, float ny, float w, float h, String hint) {
        Controller.addTextInput(id, nx, ny, w, h, hint);
    }

    public static void setTextInputHidden(String id, boolean hidden) {
        Controller.setTextInputHidden(id, hidden);
    }

    public static void setTextInputBackgroundColor(String id, int color) {
        Controller.setTextInputBackgroundColor(id, color);
    }

    public static void setTextInputTextColor(String id, int color) {
        Controller.setTextInputTextColor(id, color);
    }

    public static void setTextInputTextSizeSp(String id, float sp) {
        Controller.setTextInputTextSizeSp(id, sp);
    }

    public static void setTextInputPadding(String id, int left, int top, int right, int bottom) {
        Controller.setTextInputPadding(id, left, top, right, bottom);
    }

    public static String getTextInputText(String id) {
        return Controller.getTextInputText(id);
    }

    public static void setTextInputText(String id, String text) {
        Controller.setTextInputText(id, text);
    }

    public static void dismissTextInput(String id) {
        Controller.dismissTextInput(id);
    }
}
