package net.kiwi.overlay;

import android.app.Activity;
import android.view.ViewGroup;

@SuppressWarnings("unused")
// Not necessary anymore.
public final class Controller {
	private Controller() {}

	public static void init(Activity act, ViewGroup view) { Core.init(act, view); }
	public static void removeAll() { Core.removeAll(); }
	public static void setHiddenAll(boolean hidden) { Core.setHiddenAll(hidden); }

	public static void addButton(String id, String label, float nx, float ny, float w, float h) { Buttons.add(id, label, nx, ny, w, h); }
	public static void makeMovable(String id, boolean movable) { Buttons.makeMovable(id, movable); }
	public static void setButtonConfined(String id, boolean confined) { Buttons.setConfined(id, confined); }
	public static void setClickable(String id, int clickable) { Buttons.setClickable(id, clickable); }
	public static void setTextFont(String id, String font) { Buttons.setTextFont(id, font); }
	public static void setPosition(String id, float nx, float ny) { Buttons.setPosition(id, nx, ny); }
	public static float[] getPosition(String id) { return Buttons.getPosition(id); }
	public static void removeButton(String id) { Buttons.remove(id); }
	public static void setHidden(String id, boolean hidden) { Buttons.setHidden(id, hidden); }
	public static boolean isPressed(String id) { return Buttons.isPressed(id); }
	public static boolean isDragging(String id) { return Buttons.isDragging(id); }
	public static boolean exists(String id) { return Buttons.exists(id); }
	public static void setBackgroundResource(String id, String resName) { Buttons.setBackgroundResource(id, resName); }
	public static void setBackgroundColor(String id, int color) { Buttons.setBackgroundColor(id, color); }
	public static void setBackgroundAlpha(String id, int alpha) { Buttons.setBackgroundAlpha(id, alpha); }
	public static void setAlpha(String id, int alpha) { Buttons.setAlpha(id, alpha); }
	public static void setScaling(String id, float scaleX, float scaleY) { Buttons.setScaling(id, scaleX, scaleY); }
	public static void setDimensions(String id, float nw, float nh) { Buttons.setDimensions(id, nw, nh); }
	public static void setText(String id, String text) { Buttons.setText(id, text); }
	public static void setTextSizeSp(String id, float sp) { Buttons.setTextSizeSp(id, sp); }
	public static void setTextScale(String id, float scale) { Buttons.setTextScale(id, scale); }
	public static void setTextColor(String id, int color) { Buttons.setTextColor(id, color); }
	public static void setPadding(String id, int left, int top, int right, int bottom) { Buttons.setPadding(id, left, top, right, bottom); }
	public static void setAlignment(String id, int gravity) { Buttons.setAlignment(id, gravity); }
	public static void setBackground(String id, int resId) { Buttons.setBackground(id, resId); }

	public static void newOverlay(String id, float nx, float ny, float nw, float nh) { Overlays.create(id, nx, ny, nw, nh); }
	public static void setOverlayMovable(String id, boolean movable) { Overlays.setMovable(id, movable); }
	public static void setOverlayPinchable(String id, boolean pinchable) { Overlays.setPinchable(id, pinchable); }
	public static void setOverlayBackgroundColor(String id, int color) { Overlays.setBackgroundColor(id, color); }
	public static void setOverlayBackgroundAlpha(String id, int alpha) { Overlays.setBackgroundAlpha(id, alpha); }
	public static void setOverlayCornerRadius(String id, float radiusDp) { Overlays.setCornerRadius(id, radiusDp); }
	public static void setOverlayDimensions(String id, float nw, float nh) { Overlays.setDimensions(id, nw, nh); }
	public static void removeOverlay(String id) { Overlays.remove(id); }
	public static void overlayAddButton(String overlayId, String btnId) { Overlays.addButton(overlayId, btnId); }
	public static void overlayAddSeparator(String overlayId, float ny) { Overlays.addSeparator(overlayId, ny); }
	public static void setOverlayHidden(String id, boolean hidden) { Overlays.setHidden(id, hidden); }
	public static void setOverlayPosition(String id, float nx, float ny) { Overlays.setPosition(id, nx, ny); }
	public static float[] getOverlayPosition(String id) { return Overlays.getPosition(id); }
	public static float getOverlayScaleFactor(String id) { return Overlays.getScaleFactor(id); }
	public static boolean isOverlayPinching(String id) { return Overlays.isPinching(id); }

	public static void newDrawer(String id, float nx, float ny, float nw, float nh) { Drawers.create(id, nx, ny, nw, nh); }
	public static void overlayAddDrawer(String overlayId, String drawerId) { Drawers.addToOverlay(overlayId, drawerId); }
	public static void drawerAddButtonCustom(String drawerId, String btnId, String itemLabel, int bgResId, float textSizeSp, int paddingDp, int spacingDp) { Drawers.addButtonCustom(drawerId, btnId, itemLabel, bgResId, textSizeSp, paddingDp, spacingDp); }
	public static void setDrawerStyle(String id, int bgColor, float cornerRadiusDp) { Drawers.setStyle(id, bgColor, cornerRadiusDp); }
	public static void drawerAddButton(String drawerId, String btnId, String itemLabel) { Drawers.addButton(drawerId, btnId, itemLabel); }
	public static void drawerRemoveAllItems(String drawerId) { Drawers.removeAllItems(drawerId); }
	public static String getDrawerPressedItem(String drawerId) { return Drawers.getPressedItem(drawerId); }
	public static void setDrawerHidden(String id, boolean hidden) { Drawers.setHidden(id, hidden); }
	public static void removeDrawer(String id) { Drawers.remove(id); }

	public static void addSeekBar(String id, float nx, float ny, float w, float h, int min, int max, int progress) { SeekBars.add(id, nx, ny, w, h, min, max, progress); }
	public static void overlayAddSeekBar(String overlayId, String seekBarId) { SeekBars.addToOverlay(overlayId, seekBarId); }
	public static void setSeekBarShowValue(String id, boolean show) { SeekBars.setShowValue(id, show); }
	public static void setSeekBarValueFormat(String id, String format) { SeekBars.setValueFormat(id, format); }
	public static void setSeekBarHidden(String id, boolean hidden) { SeekBars.setHidden(id, hidden); }
	public static void removeSeekBar(String id) { SeekBars.remove(id); }
	public static void setSeekBarProgress(String id, int progress) { SeekBars.setProgress(id, progress); }
	public static int getSeekBarProgress(String id) { return SeekBars.getProgress(id); }
	public static void setSeekBarMax(String id, int max) { SeekBars.setMax(id, max); }
	public static int getSeekBarMax(String id) { return SeekBars.getMax(id); }
	public static boolean isSeekBarTouching(String id) { return SeekBars.isTouching(id); }
	public static void setSeekBarClickable(String id, boolean clickable) { SeekBars.setClickable(id, clickable); }

	public static void addTextInput(String id, float nx, float ny, float w, float h, String hint) { TextInputs.add(id, nx, ny, w, h, hint); }
	public static void dismissTextInput(String id) { TextInputs.dismiss(id); }
	public static void overlayAddTextInput(String overlayId, String inputId) { TextInputs.addToOverlay(overlayId, inputId); }
	public static void setTextInputText(String id, String text) { TextInputs.setText(id, text); }
	public static String getTextInputText(String id) { return TextInputs.getText(id); }
	public static void setTextInputHint(String id, String hint) { TextInputs.setHint(id, hint); }
	public static void setTextInputBackgroundResource(String id, String resource) { TextInputs.setBackgroundResource(id, resource); }
	public static void setTextInputBackgroundColor(String id, int color) { TextInputs.setBackgroundColor(id, color); }
	public static void setTextInputTextColor(String id, int color) { TextInputs.setTextColor(id, color); }
	public static void setTextInputTextSizeSp(String id, float sp) { TextInputs.setTextSizeSp(id, sp); }
	public static void setTextInputAlpha(String id, int alpha) { TextInputs.setAlpha(id, alpha); }
	public static void setTextInputClickable(String id, boolean clickable) { TextInputs.setClickable(id, clickable); }
	public static void setTextInputPosition(String id, float nx, float ny) { TextInputs.setPosition(id, nx, ny); }
	public static void setTextInputDimensions(String id, float nw, float nh) { TextInputs.setDimensions(id, nw, nh); }
	public static void setTextInputPadding(String id, int left, int top, int right, int bottom) { TextInputs.setPadding(id, left, top, right, bottom); }
	public static void setTextInputFont(String id, String font) { TextInputs.setFont(id, font); }
	public static void setTextInputHidden(String id, boolean hidden) { TextInputs.setHidden(id, hidden); }
	public static void removeTextInput(String id) { TextInputs.remove(id); }
	public static boolean isTextInputFocused(String id) { return TextInputs.isFocused(id); }
	public static void setTextInputSelection(String id, int index) { TextInputs.setSelection(id, index); }
	public static void setTextInputSelection(String id, int start, int end) { TextInputs.setSelection(id, start, end); }
	public static int[] getTextInputSelection(String id) { return TextInputs.getSelection(id); }
}
