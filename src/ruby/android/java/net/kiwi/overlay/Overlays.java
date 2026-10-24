package net.kiwi.overlay;

import android.annotation.SuppressLint;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.util.Log;
import android.view.MotionEvent;
import android.view.ScaleGestureDetector;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;

import android.app.Activity;

final class Overlays {
	static final class Data {
		FrameLayout frame;
		boolean hidden, movable, pinchable, pinching, needsResync, dragging;
		float scaleFactor = 1f, nx, ny, nw, nh, touchOffsetX, touchOffsetY;
		ScaleGestureDetector scaleDetector;
		Data(FrameLayout f) { frame = f; }
	}

	private Overlays() {}

	@SuppressLint("ClickableViewAccessibility")
	static void reattachTouch(String id) {
		Activity ctx = Core.act();
		if (ctx == null) return;
		Data data = Core.overlays.get(id);
		if (data == null) return;
		if (data.scaleDetector == null) {
			data.scaleDetector = new ScaleGestureDetector(ctx, new ScaleGestureDetector.SimpleOnScaleGestureListener() {
				@Override public boolean onScale(ScaleGestureDetector d) {
					if (!data.pinchable) return false;
					data.scaleFactor = d.getScaleFactor();
					data.pinching = true;
					return true;
				}
				@Override public void onScaleEnd(ScaleGestureDetector d) {
					data.pinching = false; data.scaleFactor = 1f;
				}
			});
		}
		data.frame.setOnTouchListener((v, e) -> {
			data.scaleDetector.onTouchEvent(e);
			if (data.movable) {
				FrameLayout.LayoutParams flp = (FrameLayout.LayoutParams)data.frame.getLayoutParams();
				switch (e.getActionMasked()) {
					case MotionEvent.ACTION_DOWN:
						data.dragging = true; data.needsResync = false;
						data.touchOffsetX = e.getRawX() - flp.leftMargin;
						data.touchOffsetY = e.getRawY() - flp.topMargin;
						break;
					case MotionEvent.ACTION_POINTER_DOWN:
						data.dragging = false;
						break;
					case MotionEvent.ACTION_POINTER_UP:
						if (e.getPointerCount() - 1 == 1) data.needsResync = true;
						break;
					case MotionEvent.ACTION_MOVE:
						if (e.getPointerCount() > 1) break;
						if (data.needsResync) {
							data.touchOffsetX = e.getRawX() - flp.leftMargin;
							data.touchOffsetY = e.getRawY() - flp.topMargin;
							data.dragging = true; data.needsResync = false;
							break;
						}
						if (!data.dragging) break;
						flp.leftMargin = (int)(e.getRawX() - data.touchOffsetX);
						flp.topMargin = (int)(e.getRawY() - data.touchOffsetY);
						data.frame.setLayoutParams(flp);
						break;
					case MotionEvent.ACTION_UP:
					case MotionEvent.ACTION_CANCEL:
						data.dragging = false; data.needsResync = false;
						break;
				}
			}
			return true;
		});
	}

	@SuppressLint("ClickableViewAccessibility")
	static void create(String id, float nx, float ny, float nw, float nh) {
		Core.ui(() -> {
			Activity ctx = Core.act(); ViewGroup root = Core.root();
			if (ctx == null || root == null || Core.overlays.containsKey(id)) return;
			int rw = Core.rootW(), rh = Core.rootH();
			int w = (int)(rw * nw), h = (int)(rh * nh);
			FrameLayout frame = new FrameLayout(ctx);
			GradientDrawable bg = new GradientDrawable();
			bg.setColor(0xCC000000); bg.setCornerRadius(Core.dp(12));
			frame.setBackground(bg);
			Data data = new Data(frame);
			data.nx = nx; data.ny = ny; data.nw = nw; data.nh = nh;
			root.addView(frame, Core.flp(w, h, (int)(rw * nx) - w / 2, (int)(rh * ny) - h / 2));
			Core.overlays.put(id, data);
			reattachTouch(id);
			Log.d(Core.TAG, "Added overlay: " + id);
		});
	}

	static void setMovable(String id, boolean movable) {
		Core.ui(() -> { Data d = Core.overlays.get(id); if (d == null) return; d.movable = movable; reattachTouch(id); });
	}
	static void setPinchable(String id, boolean pinchable) {
		Core.ui(() -> { Data d = Core.overlays.get(id); if (d == null) return; d.pinchable = pinchable; reattachTouch(id); });
	}
	static void setBackgroundColor(String id, int color) {
		Core.ui(() -> {
			Data d = Core.overlays.get(id); if (d == null) return;
			Drawable bg = d.frame.getBackground();
			if (bg instanceof GradientDrawable) ((GradientDrawable)bg).setColor(color);
		});
	}
	static void setBackgroundAlpha(String id, int alpha) {
		Core.ui(() -> { Data d = Core.overlays.get(id); if (d == null) return; d.frame.getBackground().setAlpha(alpha); });
	}
	static void setCornerRadius(String id, float radiusDp) {
		Core.ui(() -> {
			Data d = Core.overlays.get(id); if (d == null) return;
			Drawable bg = d.frame.getBackground();
			if (bg instanceof GradientDrawable) ((GradientDrawable)bg).setCornerRadius(Core.dp(radiusDp));
		});
	}
	static void setDimensions(String id, float nw, float nh) {
		Core.ui(() -> {
			Data d = Core.overlays.get(id); if (d == null) return;
			d.nw = nw; d.nh = nh;
			int rw = Core.rootW(), rh = Core.rootH(), w = (int)(rw * nw), h = (int)(rh * nh);
			FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams)d.frame.getLayoutParams();
			lp.width = w; lp.height = h;
			lp.leftMargin = (int)(rw * d.nx) - w / 2; lp.topMargin = (int)(rh * d.ny) - h / 2;
			d.frame.setLayoutParams(lp);
		});
	}
	static void remove(String id) {
		Core.ui(() -> {
			ViewGroup root = Core.root(); Data d = Core.overlays.get(id);
			if (root == null || d == null) return;
			for (Buttons.Data b : Core.buttons.values()) if (id.equals(b.overlayId)) b.overlayId = null;
			root.removeView(d.frame); Core.overlays.remove(id);
		});
	}
	static void addButton(String overlayId, String btnId) {
		Core.ui(() -> {
			Data o = Core.overlays.get(overlayId); Buttons.Data b = Core.buttons.get(btnId);
			if (o == null || b == null) return;
			ViewGroup old = (ViewGroup)b.button.getParent(); if (old != null) old.removeView(b.button);
			int ow = (int)(Core.rootW() * o.nw), oh = (int)(Core.rootH() * o.nh);
			int w = (int)(ow * b.nw), h = (int)(oh * b.nh);
			Buttons.applyTextSize(b, h);
			b.overlayId = overlayId;
			o.frame.addView(b.button, Core.flp(w, h, (int)(ow * b.nx) - w / 2, (int)(oh * b.ny) - h / 2));
		});
	}
	static void addSeparator(String overlayId, float ny) {
		Core.ui(() -> {
			Activity ctx = Core.act(); Data d = Core.overlays.get(overlayId);
			if (ctx == null || d == null) return;
			View sep = new View(ctx); sep.setBackgroundColor(0x88ffffff);
			int oh = (int)(Core.rootH() * d.nh), ow = (int)(Core.rootW() * d.nw);
			d.frame.addView(sep, Core.flp(ow, Core.dp(1), 0, (int)(oh * ny)));
		});
	}
	static void setHidden(String id, boolean hidden) {
		Core.ui(() -> {
			Data d = Core.overlays.get(id); if (d == null) return;
			d.hidden = hidden;
			d.frame.setVisibility(!Core.globallyHidden && !hidden ? View.VISIBLE : View.GONE);
		});
	}
	static void setPosition(String id, float nx, float ny) {
		Core.ui(() -> {
			Data d = Core.overlays.get(id); if (d == null) return;
			d.nx = nx; d.ny = ny;
			int rw = Core.rootW(), rh = Core.rootH(), w = (int)(rw * d.nw), h = (int)(rh * d.nh);
			FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams)d.frame.getLayoutParams();
			lp.leftMargin = (int)(rw * nx) - w / 2; lp.topMargin = (int)(rh * ny) - h / 2;
			d.frame.setLayoutParams(lp);
		});
	}
	static float[] getPosition(String id) {
		Data d = Core.overlays.get(id);
		if (d == null) return new float[]{0f, 0f};
		FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams)d.frame.getLayoutParams();
		return new float[]{
			(lp.leftMargin + d.frame.getWidth() / 2f) / Core.rootW(),
			(lp.topMargin + d.frame.getHeight() / 2f) / Core.rootH()
		};
	}
	static float getScaleFactor(String id) { Data d = Core.overlays.get(id); return d != null ? d.scaleFactor : 1f; }
	static boolean isPinching(String id) { Data d = Core.overlays.get(id); return d != null && d.pinching; }
}
