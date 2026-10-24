package net.kiwi.overlay;

import android.graphics.Color;
import android.graphics.Typeface;
import android.text.InputType;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;
import android.widget.FrameLayout;

import androidx.annotation.NonNull;

import android.app.Activity;
import in.aevora.ruby.R;

final class TextInputs {
	static final class Data {
		final EditText editText;
		float nx, ny, nw, nh;
		boolean hidden;
		String overlayId;
		Data(EditText e) { editText = e; }
	}

	private TextInputs() {}

	static void add(String id, float nx, float ny, float w, float h, String hint) {
		Core.ui(() -> {
			Activity ctx = Core.act(); ViewGroup root = Core.root();
			if (ctx == null || root == null || Core.textInputs.containsKey(id)) return;
			EditText edit = new EditText(ctx) {
				@Override public void draw(@NonNull android.graphics.Canvas canvas) {
					try { super.draw(canvas); } catch (NullPointerException e) { Log.e(Core.TAG, "textinput draw: " + e); }
				}
			};
			edit.setHint(hint); edit.setHorizontallyScrolling(false); edit.setMaxLines(Integer.MAX_VALUE);
			edit.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE | InputType.TYPE_TEXT_FLAG_CAP_SENTENCES);
			edit.setImeOptions(EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN);
			edit.setOnEditorActionListener((v, a, e) -> false);
			edit.setBackgroundResource(R.drawable.game_textinput); edit.setOverScrollMode(View.OVER_SCROLL_NEVER);
			edit.setTextColor(Color.WHITE); edit.setHintTextColor(0x88FFFFFF);
			edit.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
			edit.setPadding(Core.dp(12), Core.dp(10), Core.dp(12), Core.dp(10));
			edit.setGravity(Gravity.TOP | Gravity.START); edit.setTypeface(Typeface.MONOSPACE);
			edit.setTextIsSelectable(true); edit.setLongClickable(true);
			edit.setFocusable(true); edit.setFocusableInTouchMode(true); edit.setCursorVisible(true);
			edit.setVerticalScrollBarEnabled(true);
			Data data = new Data(edit);
			data.nx = nx; data.ny = ny; data.nw = w; data.nh = h;
			root.addView(edit, Core.squareLp(nx, ny, w, h));
			Core.textInputs.put(id, data);
			Log.d(Core.TAG, "Added text input: " + id);
		});
	}

	static void dismiss(String id) {
		Core.ui(() -> {
			Data d = Core.textInputs.get(id); if (d == null) return;
			d.editText.clearFocus(); Core.hideKeyboard(d.editText);
		});
	}
	static void addToOverlay(String overlayId, String inputId) {
		Core.ui(() -> {
			Overlays.Data o = Core.overlays.get(overlayId); Data d = Core.textInputs.get(inputId);
			if (o == null || d == null) return;
			ViewGroup old = (ViewGroup)d.editText.getParent(); if (old != null) old.removeView(d.editText);
			int ow = (int)(Core.rootW() * o.nw), oh = (int)(Core.rootH() * o.nh);
			int w = (int)(ow * d.nw), h = (int)(oh * d.nh);
			d.overlayId = overlayId;
			o.frame.addView(d.editText, Core.flp(w, h, (int)(ow * d.nx) - w / 2, (int)(oh * d.ny) - h / 2));
		});
	}
	static void setText(String id, String text) {
		Core.ui(() -> { Data d = Core.textInputs.get(id); if (d != null) d.editText.setText(text); });
	}
	static String getText(String id) {
		Data d = Core.textInputs.get(id);
		return d != null ? d.editText.getText().toString() : "";
	}
	static void setHint(String id, String hint) {
		Core.ui(() -> { Data d = Core.textInputs.get(id); if (d != null) d.editText.setHint(hint); });
	}
	static void setBackgroundResource(String id, String resource) {
		Core.ui(() -> { Data d = Core.textInputs.get(id); if (d != null) Core.applyBackground(d.editText, resource); });
	}
	static void setBackgroundColor(String id, int color) {
		Core.ui(() -> { Data d = Core.textInputs.get(id); if (d != null) d.editText.setBackgroundColor(color); });
	}
	static void setTextColor(String id, int color) {
		Core.ui(() -> { Data d = Core.textInputs.get(id); if (d != null) d.editText.setTextColor(color); });
	}
	static void setTextSizeSp(String id, float sp) {
		Core.ui(() -> { Data d = Core.textInputs.get(id); if (d != null) d.editText.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp); });
	}
	static void setAlpha(String id, int alpha) {
		Core.ui(() -> { Data d = Core.textInputs.get(id); if (d != null) d.editText.setAlpha(Math.max(0f, Math.min(1f, alpha / 255f))); });
	}
	static void setClickable(String id, boolean clickable) {
		Core.ui(() -> {
			Data d = Core.textInputs.get(id); if (d == null) return;
			d.editText.setEnabled(clickable); d.editText.setFocusable(clickable); d.editText.setFocusableInTouchMode(clickable);
		});
	}
	static void setPosition(String id, float nx, float ny) {
		Core.ui(() -> {
			Data d = Core.textInputs.get(id); if (d == null) return;
			d.nx = nx; d.ny = ny;
			ViewGroup.LayoutParams lp = d.editText.getLayoutParams();
			if (!(lp instanceof FrameLayout.LayoutParams)) return;
			FrameLayout.LayoutParams flp = (FrameLayout.LayoutParams)lp;
			flp.leftMargin = (int)(Core.rootW() * nx) - flp.width / 2;
			flp.topMargin = (int)(Core.rootH() * ny) - flp.height / 2;
			d.editText.setLayoutParams(flp);
		});
	}
	static void setDimensions(String id, float nw, float nh) {
		Core.ui(() -> {
			Data d = Core.textInputs.get(id); if (d == null) return;
			d.nw = nw; d.nh = nh;
			ViewGroup.LayoutParams lp = d.editText.getLayoutParams();
			if (!(lp instanceof FrameLayout.LayoutParams)) return;
			FrameLayout.LayoutParams flp = (FrameLayout.LayoutParams)lp;
			int base = Core.squareBase(), w = (int)(base * nw), h = (int)(base * nh);
			flp.width = w; flp.height = h;
			flp.leftMargin = (int)(Core.rootW() * d.nx) - w / 2;
			flp.topMargin = (int)(Core.rootH() * d.ny) - h / 2;
			d.editText.setLayoutParams(flp);
		});
	}
	static void setPadding(String id, int l, int t, int r, int b) {
		Core.ui(() -> { Data d = Core.textInputs.get(id); if (d != null) d.editText.setPadding(Core.dp(l), Core.dp(t), Core.dp(r), Core.dp(b)); });
	}
	static void setFont(String id, String font) {
		Core.ui(() -> {
			Data d = Core.textInputs.get(id); if (d == null) return;
			Typeface tf = Core.resolveFont(d.editText.getContext(), font);
			if (tf != null) d.editText.setTypeface(tf);
			else Log.e(Core.TAG, "Couldn't load font " + font);
		});
	}
	static void setHidden(String id, boolean hidden) {
		Core.ui(() -> {
			Data d = Core.textInputs.get(id); if (d == null) return;
			d.hidden = hidden;
			d.editText.setVisibility(!Core.globallyHidden && !hidden ? View.VISIBLE : View.GONE);
		});
	}
	static void remove(String id) {
		Core.ui(() -> {
			Data d = Core.textInputs.get(id); if (d == null) return;
			ViewGroup p = (ViewGroup)d.editText.getParent(); if (p != null) p.removeView(d.editText);
			Core.textInputs.remove(id);
		});
	}
	static boolean isFocused(String id) {
		Data d = Core.textInputs.get(id);
		return d != null && d.editText.isFocused();
	}
	static void setSelection(String id, int index) {
		Core.ui(() -> {
			Data d = Core.textInputs.get(id); if (d == null) return;
			int len = d.editText.getText().length();
			d.editText.setSelection(Math.max(0, Math.min(index, len)));
		});
	}
	static void setSelection(String id, int start, int end) {
		Core.ui(() -> {
			Data d = Core.textInputs.get(id); if (d == null) return;
			int len = d.editText.getText().length();
			d.editText.setSelection(Math.max(0, Math.min(start, len)), Math.max(0, Math.min(end, len)));
		});
	}
	static int[] getSelection(String id) {
		Data d = Core.textInputs.get(id);
		if (d == null) return new int[]{0, 0};
		return new int[]{d.editText.getSelectionStart(), d.editText.getSelectionEnd()};
	}
}
