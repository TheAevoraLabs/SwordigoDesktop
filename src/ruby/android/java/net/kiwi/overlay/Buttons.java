package net.kiwi.overlay;

import android.annotation.SuppressLint;
import android.util.Log;
import android.util.TypedValue;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;

import android.app.Activity;
import in.aevora.ruby.R;

final class Buttons {
	static final class Movable {
		boolean dragging;
		float touchOffsetX, touchOffsetY;
	}

	static final class Data {
		Button button;
		boolean pressed, hidden, confined, explicitTextSizeSp;
		float baseTextSize, textSizeSpValue = -1f, nw, nh, nx, ny;
		Movable movable;
		String overlayId;
		Data(Button b) { button = b; baseTextSize = b.getTextSize(); }
	}

	private Buttons() {}

	static void updateVisibility(Data d) {
		if (d == null || d.button == null) return;
		d.button.setVisibility(!Core.globallyHidden && !d.hidden ? View.VISIBLE : View.GONE);
	}

	static void applyTextSize(Data d, int heightPx) {
		d.baseTextSize = heightPx * 0.35f;
		if (d.explicitTextSizeSp) d.button.setTextSize(TypedValue.COMPLEX_UNIT_SP, d.textSizeSpValue);
		else d.button.setTextSize(TypedValue.COMPLEX_UNIT_PX, d.baseTextSize);
	}

	@SuppressLint("ClickableViewAccessibility")
	static void add(String id, String label, float nx, float ny, float w, float h) {
		Core.ui(() -> {
			Activity ctx = Core.act(); ViewGroup root = Core.root();
			if (ctx == null || root == null || Core.buttons.containsKey(id)) return;
			Button btn = new Button(ctx);
			btn.setText(label); btn.setTextColor(0xffffffff); btn.setVisibility(View.VISIBLE);
			btn.setAllCaps(false); btn.setBackgroundResource(R.drawable.game_button);
			btn.setSingleLine(false); btn.setEllipsize(null); btn.setMaxLines(Integer.MAX_VALUE);
			Core.applyDefaultFont(ctx, btn);
			Data data = new Data(btn);
			btn.setOnTouchListener((v, e) -> {
				switch (e.getAction()) {
					case MotionEvent.ACTION_DOWN: data.pressed = true; break;
					case MotionEvent.ACTION_UP:
					case MotionEvent.ACTION_CANCEL: data.pressed = false; break;
				}
				return false;
			});
			data.nw = w; data.nh = h; data.nx = nx; data.ny = ny;
			FrameLayout.LayoutParams lp = Core.squareLp(nx, ny, w, h);
			applyTextSize(data, lp.height);
			root.addView(btn, lp);
			Core.buttons.put(id, data);
			Log.d(Core.TAG, "Added button: " + id);
		});
	}

	@SuppressLint("ClickableViewAccessibility")
	static void makeMovable(String id, boolean movable) {
		Core.ui(() -> {
			Data data = Core.buttons.get(id); if (data == null) return;
			if (!movable) {
				data.movable = null;
				data.button.setOnTouchListener((v, e) -> {
					switch (e.getAction()) {
						case MotionEvent.ACTION_DOWN: data.pressed = true; break;
						case MotionEvent.ACTION_UP:
						case MotionEvent.ACTION_CANCEL: data.pressed = false; break;
					}
					return false;
				});
				return;
			}
			data.movable = new Movable();
			data.button.setOnTouchListener((v, e) -> {
				Button btn = data.button;
				FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams)btn.getLayoutParams();
				float rawX = e.getRawX(), rawY = e.getRawY();
				if (data.overlayId != null) {
					Overlays.Data o = Core.overlays.get(data.overlayId);
					if (o != null) {
						FrameLayout.LayoutParams flp = (FrameLayout.LayoutParams)o.frame.getLayoutParams();
						rawX -= flp.leftMargin; rawY -= flp.topMargin;
					}
				}
				switch (e.getAction()) {
					case MotionEvent.ACTION_DOWN:
						data.pressed = true; data.movable.dragging = true;
						data.movable.touchOffsetX = rawX - lp.leftMargin;
						data.movable.touchOffsetY = rawY - lp.topMargin;
						break;
					case MotionEvent.ACTION_MOVE:
						if (!data.movable.dragging) break;
						int nl = (int)(rawX - data.movable.touchOffsetX);
						int nt = (int)(rawY - data.movable.touchOffsetY);
						if (data.confined && data.overlayId != null) {
							Overlays.Data o = Core.overlays.get(data.overlayId);
							if (o != null) {
								nl = Math.max(0, Math.min(nl, o.frame.getWidth() - btn.getWidth()));
								nt = Math.max(0, Math.min(nt, o.frame.getHeight() - btn.getHeight()));
							}
						}
						lp.leftMargin = nl; lp.topMargin = nt; btn.setLayoutParams(lp);
						break;
					case MotionEvent.ACTION_UP:
					case MotionEvent.ACTION_CANCEL:
						data.pressed = false; data.movable.dragging = false;
						break;
				}
				return true;
			});
		});
	}

	static void setConfined(String id, boolean confined) {
		Core.ui(() -> { Data d = Core.buttons.get(id); if (d != null) d.confined = confined; });
	}
	static void setClickable(String id, int clickable) {
		Core.ui(() -> { Data d = Core.buttons.get(id); if (d == null) return; d.button.setClickable(clickable == 1); });
	}
	static void setTextFont(String id, String font) {
		Core.ui(() -> {
			Data d = Core.buttons.get(id); if (d == null) return;
			android.graphics.Typeface tf = Core.resolveFont(d.button.getContext(), font);
			if (tf != null) d.button.setTypeface(tf);
		});
	}
	static void setPosition(String id, float nx, float ny) {
		Core.ui(() -> {
			Data d = Core.buttons.get(id); if (d == null) return;
			d.nx = nx; d.ny = ny;
			FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams)d.button.getLayoutParams();
			int w, h;
			if (d.overlayId != null) {
				Overlays.Data o = Core.overlays.get(d.overlayId); if (o == null) return;
				int ow = (int)(Core.rootW() * o.nw), oh = (int)(Core.rootH() * o.nh);
				w = (int)(ow * d.nw); h = (int)(oh * d.nh);
				lp.leftMargin = (int)(ow * nx) - w / 2; lp.topMargin = (int)(oh * ny) - h / 2;
			} else {
				int base = Core.squareBase(), rw = Core.rootW(), rh = Core.rootH();
				w = (int)(base * d.nw); h = (int)(base * d.nh);
				lp.leftMargin = (int)(rw * nx) - w / 2; lp.topMargin = (int)(rh * ny) - h / 2;
			}
			d.button.setLayoutParams(lp);
		});
	}
	static float[] getPosition(String id) {
		Data d = Core.buttons.get(id);
		if (d == null) return new float[]{0f, 0f};
		FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams)d.button.getLayoutParams();
		float cx = lp.leftMargin + d.button.getWidth() / 2f, cy = lp.topMargin + d.button.getHeight() / 2f;
		if (d.overlayId != null) {
			Overlays.Data o = Core.overlays.get(d.overlayId);
			if (o != null) {
				FrameLayout.LayoutParams olp = (FrameLayout.LayoutParams)o.frame.getLayoutParams();
				cx += olp.leftMargin; cy += olp.topMargin;
			}
		}
		return new float[]{cx / Core.rootW(), cy / Core.rootH()};
	}
	static void remove(String id) {
		Core.ui(() -> {
			ViewGroup root = Core.root(); Data d = Core.buttons.get(id);
			if (root == null || d == null) return;
			root.removeView(d.button); Core.buttons.remove(id);
		});
	}
	static void setHidden(String id, boolean hidden) {
		Core.ui(() -> { Data d = Core.buttons.get(id); if (d == null) return; d.hidden = hidden; updateVisibility(d); });
	}
	static boolean isPressed(String id) { Data d = Core.buttons.get(id); return d != null && d.pressed; }
	static boolean isDragging(String id) { Data d = Core.buttons.get(id); return d != null && d.movable != null && d.movable.dragging; }
	static boolean exists(String id) { return Core.buttons.containsKey(id); }

	static void setBackgroundResource(String id, String name) {
		Core.ui(() -> { Data d = Core.buttons.get(id); if (d != null) Core.applyBackground(d.button, name); });
	}
	static void setBackgroundColor(String id, int color) {
		Core.ui(() -> { Data d = Core.buttons.get(id); if (d != null) d.button.setBackgroundColor(color); });
	}
	static void setBackgroundAlpha(String id, int alpha) {
		Core.ui(() -> { Data d = Core.buttons.get(id); if (d != null && d.button.getBackground() != null) d.button.getBackground().setAlpha(alpha); });
	}
	static void setAlpha(String id, int alpha) {
		Core.ui(() -> { Data d = Core.buttons.get(id); if (d != null) d.button.setAlpha(Math.max(0f, Math.min(1f, alpha / 255f))); });
	}
	static void setScaling(String id, float sx, float sy) {
		Core.ui(() -> {
			ViewGroup root = Core.root(); Data d = Core.buttons.get(id);
			if (root == null || d == null) return;
			FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams)d.button.getLayoutParams();
			int base = Core.squareBase(root), sw = root.getWidth(), sh = root.getHeight();
			int nw = (int)(base * d.nw * sx), nh = (int)(base * d.nh * sy);
			float cx = (lp.leftMargin + nw / 2f) / sw, cy = (lp.topMargin + nh / 2f) / sh;
			lp.width = nw; lp.height = nh;
			lp.leftMargin = (int)(sw * cx) - nw / 2; lp.topMargin = (int)(sh * cy) - nh / 2;
			d.button.setLayoutParams(lp);
		});
	}
	static void setDimensions(String id, float nw, float nh) {
		Core.ui(() -> {
			ViewGroup root = Core.root(); Data d = Core.buttons.get(id);
			if (root == null || d == null) return;
			d.nw = nw; d.nh = nh;
			FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams)d.button.getLayoutParams();
			int base = Core.squareBase(root), w = (int)(base * nw), h = (int)(base * nh);
			float[] pos = getPosition(id);
			lp.width = w; lp.height = h;
			lp.leftMargin = (int)(root.getWidth() * pos[0]) - w / 2;
			lp.topMargin = (int)(root.getHeight() * pos[1]) - h / 2;
			d.button.setLayoutParams(lp);
			applyTextSize(d, h);
		});
	}
	static void setText(String id, String text) {
		Core.ui(() -> { Data d = Core.buttons.get(id); if (d != null) d.button.setText(text); });
	}
	static void setTextSizeSp(String id, float sp) {
		Core.ui(() -> { Data d = Core.buttons.get(id); if (d == null) return; d.explicitTextSizeSp = true; d.textSizeSpValue = sp; d.button.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp); });
	}
	static void setTextScale(String id, float scale) {
		Core.ui(() -> { Data d = Core.buttons.get(id); if (d == null) return; d.explicitTextSizeSp = false; d.button.setTextSize(TypedValue.COMPLEX_UNIT_PX, d.baseTextSize * scale); });
	}
	static void setTextColor(String id, int color) {
		Core.ui(() -> { Data d = Core.buttons.get(id); if (d != null) d.button.setTextColor(color); });
	}
	static void setPadding(String id, int l, int t, int r, int b) {
		Core.ui(() -> { Data d = Core.buttons.get(id); if (d != null) d.button.setPadding(l, t, r, b); });
	}
	static void setAlignment(String id, int gravity) {
		Core.ui(() -> { Data d = Core.buttons.get(id); if (d != null) d.button.setGravity(gravity); });
	}
	static void setBackground(String id, int resId) {
		Core.ui(() -> {
			Data d = Core.buttons.get(id); if (d == null) return;
			if (resId == 0) { Log.e(Core.TAG, "Invalid drawable resource ID"); return; }
			d.button.setBackgroundResource(resId);
		});
	}
}
