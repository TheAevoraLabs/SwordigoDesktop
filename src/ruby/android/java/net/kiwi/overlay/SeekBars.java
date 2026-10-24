package net.kiwi.overlay;

import android.annotation.SuppressLint;
import android.graphics.Color;
import android.util.Log;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.SeekBar;

import android.app.Activity;
import in.aevora.ruby.R;

final class SeekBars {
	static final class Data {
		SeekBar seekBar;
		boolean hidden, touching, showValue;
		String overlayId, valueFormat = "%d";
		float nw, nh, nx, ny;
		Data(SeekBar s) { seekBar = s; }
	}

	static final class ValueSeekBar extends SeekBar {
		boolean showValue;
		String valueFormat = "%d";
		final android.text.TextPaint paint = new android.text.TextPaint(android.graphics.Paint.ANTI_ALIAS_FLAG);
		ValueSeekBar(android.content.Context ctx) {
			super(ctx);
			paint.setColor(Color.WHITE);
			paint.setTextAlign(android.graphics.Paint.Align.CENTER);
			paint.setFakeBoldText(true);
		}
		void setShowValue(boolean s) { showValue = s; invalidate(); }
		void setValueFormat(String f) { valueFormat = (f != null && !f.isEmpty()) ? f : "%d"; invalidate(); }
		@Override protected synchronized void onDraw(android.graphics.Canvas c) {
			super.onDraw(c);
			if (!showValue) return;
			String t;
			try { t = String.format(java.util.Locale.US, valueFormat, getProgress()); }
			catch (Exception e) { t = String.valueOf(getProgress()); }
			float cx = getWidth() / 2f, cy = getHeight() / 2f;
			paint.setTextSize(Math.max(12f, getHeight() * 0.55f));
			android.graphics.Paint.FontMetrics fm = paint.getFontMetrics();
			c.drawText(t, cx, cy - (fm.ascent + fm.descent) / 2f, paint);
		}
	}

	private SeekBars() {}

	static void refreshValue(Data d) {
		if (d == null || !(d.seekBar instanceof ValueSeekBar)) return;
		ValueSeekBar vsb = (ValueSeekBar)d.seekBar;
		vsb.setShowValue(d.showValue);
		vsb.setValueFormat(d.valueFormat);
		vsb.invalidate();
	}

	@SuppressLint({"ClickableViewAccessibility", "UseCompatLoadingForDrawables"})
	static void add(String id, float nx, float ny, float w, float h, int min, int max, int progress) {
		Core.ui(() -> {
			Activity ctx = Core.act(); ViewGroup root = Core.root();
			if (ctx == null || root == null || Core.seekBars.containsKey(id)) return;
			ValueSeekBar sb = new ValueSeekBar(ctx);
			sb.setProgressDrawable(ctx.getDrawable(R.drawable.game_seekbar));
			sb.setThumb(ctx.getDrawable(R.drawable.game_seekbar_thumb));
			sb.setSplitTrack(false);
			if (android.os.Build.VERSION.SDK_INT >= 26) sb.setMin(min);
			sb.setMax(max); sb.setProgress(progress);
			Data data = new Data(sb);
			data.nx = nx; data.ny = ny; data.nw = w; data.nh = h;
			sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
				@Override public void onProgressChanged(SeekBar s, int p, boolean f) { refreshValue(data); }
				@Override public void onStartTrackingTouch(SeekBar s) { data.touching = true; }
				@Override public void onStopTrackingTouch(SeekBar s) { data.touching = false; }
			});
			root.addView(sb, Core.squareLp(nx, ny, w, h));
			Core.seekBars.put(id, data);
			Log.d(Core.TAG, "Added seekbar: " + id);
		});
	}

	static void addToOverlay(String overlayId, String seekBarId) {
		Core.ui(() -> {
			Overlays.Data o = Core.overlays.get(overlayId); Data s = Core.seekBars.get(seekBarId);
			if (o == null || s == null) return;
			ViewGroup old = (ViewGroup)s.seekBar.getParent(); if (old != null) old.removeView(s.seekBar);
			int ow = (int)(Core.rootW() * o.nw), oh = (int)(Core.rootH() * o.nh);
			int w = (int)(ow * s.nw), h = (int)(oh * s.nh);
			s.overlayId = overlayId;
			o.frame.addView(s.seekBar, Core.flp(w, h, (int)(ow * s.nx) - w / 2, (int)(oh * s.ny) - h / 2));
		});
	}
	static void setShowValue(String id, boolean show) {
		Core.ui(() -> { Data d = Core.seekBars.get(id); if (d == null) return; d.showValue = show; refreshValue(d); });
	}
	static void setValueFormat(String id, String format) {
		Core.ui(() -> { Data d = Core.seekBars.get(id); if (d == null) return; d.valueFormat = format; refreshValue(d); });
	}
	static void setHidden(String id, boolean hidden) {
		Core.ui(() -> {
			Data d = Core.seekBars.get(id); if (d == null) return;
			d.hidden = hidden;
			d.seekBar.setVisibility(!Core.globallyHidden && !hidden ? View.VISIBLE : View.GONE);
		});
	}
	static void remove(String id) {
		Core.ui(() -> {
			Data d = Core.seekBars.get(id); if (d == null) return;
			ViewGroup p = (ViewGroup)d.seekBar.getParent(); if (p != null) p.removeView(d.seekBar);
			Core.seekBars.remove(id);
		});
	}
	static void setProgress(String id, int progress) {
		Core.ui(() -> { Data d = Core.seekBars.get(id); if (d == null) return; d.seekBar.setProgress(progress); refreshValue(d); });
	}
	static int getProgress(String id) { Data d = Core.seekBars.get(id); return d != null ? d.seekBar.getProgress() : 0; }
	static void setMax(String id, int max) {
		Core.ui(() -> { Data d = Core.seekBars.get(id); if (d != null) d.seekBar.setMax(max); });
	}
	static int getMax(String id) { Data d = Core.seekBars.get(id); return d != null ? d.seekBar.getMax() : 0; }
	static boolean isTouching(String id) { Data d = Core.seekBars.get(id); return d != null && d.touching; }
	static void setClickable(String id, boolean clickable) {
		Core.ui(() -> { Data d = Core.seekBars.get(id); if (d != null) d.seekBar.setEnabled(clickable); });
	}
}
