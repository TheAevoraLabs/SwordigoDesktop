package net.kiwi.overlay;

import android.annotation.SuppressLint;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;

import android.app.Activity;
import in.aevora.ruby.R;

final class Drawers {
	static final class Data {
		FrameLayout frame;
		LinearLayout contentLayout;
		ScrollView scrollView;
		String lastPressedButtonId = "", parentOverlayId;
		int itemSpacingDp = 6, itemPaddingDp = 12, itemBgResource;
		float itemTextSizePx = -1, nx, ny, nw, nh;
		Data(FrameLayout f, LinearLayout c, ScrollView s) { frame = f; contentLayout = c; scrollView = s; }
	}

	private Drawers() {}

	static void create(String id, float nx, float ny, float nw, float nh) {
		Core.ui(() -> {
			Activity ctx = Core.act(); ViewGroup root = Core.root();
			if (ctx == null || root == null || Core.drawers.containsKey(id)) return;
			int rw = Core.rootW(), rh = Core.rootH(), w = (int)(rw * nw), h = (int)(rh * nh);
			FrameLayout frame = new FrameLayout(ctx);
			GradientDrawable bg = new GradientDrawable();
			bg.setColor(0xEE1A1A1A); bg.setCornerRadius(Core.dp(8));
			frame.setBackground(bg);
			ScrollView sv = new ScrollView(ctx); sv.setFillViewport(true); sv.setVerticalScrollBarEnabled(true);
			LinearLayout content = new LinearLayout(ctx);
			content.setOrientation(LinearLayout.VERTICAL);
			content.setPadding(Core.dp(8), Core.dp(8), Core.dp(8), Core.dp(8));
			sv.addView(content, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
			frame.addView(sv, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
			root.addView(frame, Core.flp(w, h, (int)(rw * nx) - w / 2, (int)(rh * ny) - h / 2));
			Data data = new Data(frame, content, sv);
			data.nx = nx; data.ny = ny; data.nw = nw; data.nh = nh;
			Core.drawers.put(id, data);
		});
	}

	static void addToOverlay(String overlayId, String drawerId) {
		Core.ui(() -> {
			Overlays.Data o = Core.overlays.get(overlayId); Data d = Core.drawers.get(drawerId);
			if (o == null || d == null) return;
			ViewGroup old = (ViewGroup)d.frame.getParent(); if (old != null) old.removeView(d.frame);
			int ow = (int)(Core.rootW() * o.nw), oh = (int)(Core.rootH() * o.nh);
			int w = (int)(ow * d.nw), h = (int)(oh * d.nh);
			d.parentOverlayId = overlayId;
			o.frame.addView(d.frame, Core.flp(w, h, (int)(ow * d.nx) - w / 2, (int)(oh * d.ny) - h / 2));
		});
	}

	@SuppressLint("ClickableViewAccessibility")
	static void addButtonCustom(String drawerId, String btnId, String label, int bgResId, float textSizeSp, int paddingDp, int spacingDp) {
		Core.ui(() -> {
			Activity ctx = Core.act(); Data drawer = Core.drawers.get(drawerId);
			if (ctx == null || drawer == null) return;
			Button btn = new Button(ctx);
			btn.setText(label); btn.setTextColor(0xffffffff); btn.setAllCaps(false); btn.setSingleLine(true);
			int pad = paddingDp > 0 ? paddingDp : drawer.itemPaddingDp;
			btn.setPadding(Core.dp(pad), Core.dp(pad), Core.dp(pad), Core.dp(pad));
			int bg = bgResId != 0 ? bgResId : (drawer.itemBgResource != 0 ? drawer.itemBgResource : R.drawable.game_button);
			btn.setBackgroundResource(bg);
			if (textSizeSp > 0) btn.setTextSize(TypedValue.COMPLEX_UNIT_SP, textSizeSp);
			else if (drawer.itemTextSizePx > 0) btn.setTextSize(TypedValue.COMPLEX_UNIT_PX, drawer.itemTextSizePx);
			Core.applyDefaultFont(ctx, btn);
			btn.setOnTouchListener((v, e) -> {
				switch (e.getAction()) {
					case MotionEvent.ACTION_DOWN: drawer.lastPressedButtonId = btnId; break;
					case MotionEvent.ACTION_MOVE:
						if (e.getX() < 0 || e.getX() > v.getWidth() || e.getY() < 0 || e.getY() > v.getHeight()) {
							if (drawer.lastPressedButtonId.equals(btnId)) drawer.lastPressedButtonId = "";
						} else drawer.lastPressedButtonId = btnId;
						break;
					case MotionEvent.ACTION_UP:
					case MotionEvent.ACTION_CANCEL:
						if (drawer.lastPressedButtonId.equals(btnId)) drawer.lastPressedButtonId = "";
						break;
				}
				return false;
			});
			LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
			lp.setMargins(0, 0, 0, Core.dp(spacingDp > 0 ? spacingDp : drawer.itemSpacingDp));
			drawer.contentLayout.addView(btn, lp);
		});
	}

	static void setStyle(String id, int bgColor, float cornerRadiusDp) {
		Core.ui(() -> {
			Data d = Core.drawers.get(id); if (d == null) return;
			GradientDrawable bg = new GradientDrawable();
			bg.setColor(bgColor); bg.setCornerRadius(Core.dp(cornerRadiusDp));
			d.frame.setBackground(bg);
		});
	}
	static void addButton(String drawerId, String btnId, String label) {
		addButtonCustom(drawerId, btnId, label, 0, 0, 12, 6);
	}
	static void removeAllItems(String drawerId) {
		Core.ui(() -> {
			Data d = Core.drawers.get(drawerId); if (d == null) return;
			d.lastPressedButtonId = ""; d.contentLayout.removeAllViews();
		});
	}
	static String getPressedItem(String drawerId) {
		Data d = Core.drawers.get(drawerId);
		return d == null ? "" : d.lastPressedButtonId;
	}
	static void setHidden(String id, boolean hidden) {
		Core.ui(() -> { Data d = Core.drawers.get(id); if (d == null) return; d.frame.setVisibility(hidden ? View.GONE : View.VISIBLE); });
	}
	static void remove(String id) {
		Core.ui(() -> {
			ViewGroup root = Core.root(); Data d = Core.drawers.remove(id);
			if (root != null && d != null) root.removeView(d.frame);
		});
	}
}
