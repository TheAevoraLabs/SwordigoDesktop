package net.kiwi.overlay;

import android.annotation.SuppressLint;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Typeface;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.util.DisplayMetrics;
import android.util.Log;
import android.util.Xml;
import android.view.View;
import android.view.ViewGroup;
import android.app.Activity;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.FrameLayout;

import net.kiwi.launcher.MainActivity;

import org.xmlpull.v1.XmlPullParser;

import java.io.File;
import java.io.FileInputStream;
import java.lang.ref.WeakReference;
import java.util.HashMap;

public final class Core {
	static final String TAG = "MiniOverlay";
	static boolean globallyHidden;
	static WeakReference<Activity> actRef;
	static WeakReference<ViewGroup> rootRef;
	static final HashMap<String, Buttons.Data> buttons = new HashMap<>();
	static final HashMap<String, Overlays.Data> overlays = new HashMap<>();
	static final HashMap<String, Drawers.Data> drawers = new HashMap<>();
	static final HashMap<String, SeekBars.Data> seekBars = new HashMap<>();
	static final HashMap<String, TextInputs.Data> textInputs = new HashMap<>();

	private Core() {}

	public static void init(Activity act, ViewGroup root) {
		actRef = new WeakReference<>(act);
		rootRef = new WeakReference<>(root);
	}

	static Activity act() { return actRef != null ? actRef.get() : null; }
	static ViewGroup root() { return rootRef != null ? rootRef.get() : null; }

	static void ui(Runnable r) {
		Activity a = act();
		if (a != null) a.runOnUiThread(r);
	}

	static DisplayMetrics metrics() {
		Activity a = act();
		return a == null ? null : a.getResources().getDisplayMetrics();
	}

	static int screenW() { DisplayMetrics m = metrics(); return m == null ? 0 : m.widthPixels; }
	static int screenH() { DisplayMetrics m = metrics(); return m == null ? 0 : m.heightPixels; }
	static int rootW() { ViewGroup r = root(); return r != null && r.getWidth() > 0 ? r.getWidth() : screenW(); }
	static int rootH() { ViewGroup r = root(); return r != null && r.getHeight() > 0 ? r.getHeight() : screenH(); }
	static int squareBase() { return Math.min(rootW(), rootH()); }
	static int squareBase(ViewGroup r) { return Math.min(r.getWidth(), r.getHeight()); }
	static int dp(float d) { DisplayMetrics m = metrics(); assert m != null; return (int)(d * m.density); }

	static FrameLayout.LayoutParams flp(int w, int h, int l, int t) {
		FrameLayout.LayoutParams p = new FrameLayout.LayoutParams(w, h);
		p.leftMargin = l; p.topMargin = t;
		return p;
	}

	static FrameLayout.LayoutParams squareLp(float nx, float ny, float nw, float nh) {
		int rw = rootW(), rh = rootH(), b = Math.min(rw, rh);
		int w = (int)(b * nw), h = (int)(b * nh);
		return flp(w, h, (int)(rw * nx) - w / 2, (int)(rh * ny) - h / 2);
	}

	static void hideKeyboard(View v) {
		InputMethodManager imm = (InputMethodManager)v.getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
		if (imm != null) imm.hideSoftInputFromWindow(v.getWindowToken(), 0);
	}

	static File modFile(Context ctx, String rel) {
		String mod = MainActivity.currentMod();
		File ext = ctx.getExternalFilesDir(null);
		if (ext == null) return null;
		if (mod != null && !mod.isEmpty()) {
			File f = new File(ext, "mods/" + mod + "/" + rel);
			if (f.isFile()) return f;
		}
		File f = new File(ext, rel);
		return f.isFile() ? f : null;
	}

	static Typeface resolveFont(Context ctx, String font) {
		if (font == null || font.isEmpty()) return null;
		String base = font.contains("/") ? font : "fonts/" + font;
		String[] cands = {"resources/" + base, base, "resources/fonts/" + font, "fonts/" + font, font};
		for (String c : cands) {
			File f = modFile(ctx, c);
			if (f != null) try { return Typeface.createFromFile(f); } catch (Exception ignored) {}
		}
		try { return Typeface.createFromAsset(ctx.getAssets(), base.startsWith("fonts/") ? base : "fonts/" + font); }
		catch (Exception e) { Log.e(TAG, "font fail: " + font, e); return null; }
	}

	@SuppressLint({"DiscouragedApi", "UseCompatLoadingForDrawables"})
	static Drawable resolveDrawable(Context ctx, String name) {
		if (name == null || name.isEmpty()) return null;
		int resId = ctx.getResources().getIdentifier(name, "drawable", ctx.getPackageName());
		if (resId != 0) try { return ctx.getDrawable(resId); } catch (Exception e) { Log.e(TAG, "getDrawable: " + name, e); }
		String rel = name.startsWith("resources/") ? name.substring(10) : name;
		boolean hasExt = name.endsWith(".png") || name.endsWith(".jpg") || name.endsWith(".webp") || name.endsWith(".xml");
		String[] cands = {name, "resources/" + rel, rel, hasExt ? name : name + ".png", hasExt ? name : name + ".xml"};
		for (String c : cands) {
			File f = modFile(ctx, c);
			if (f == null) continue;
			try {
				if (f.getName().endsWith(".xml")) {
					try (FileInputStream fis = new FileInputStream(f)) {
						XmlPullParser p = Xml.newPullParser();
						p.setInput(fis, "UTF-8");
						Drawable d = Drawable.createFromXml(ctx.getResources(), p);
						if (d != null) { Log.d(TAG, "xml drawable: " + f); return d; }
					}
				} else {
					Bitmap bmp = BitmapFactory.decodeFile(f.getAbsolutePath());
					if (bmp != null) { Log.d(TAG, "drawable: " + f); return new BitmapDrawable(ctx.getResources(), bmp); }
				}
			} catch (Exception e) { Log.e(TAG, "drawable fail: " + f, e); }
		}
		Log.e(TAG, "drawable missing: " + name);
		return null;
	}

	static void applyBackground(View v, String name) {
		if (v == null) return;
		Drawable d = resolveDrawable(v.getContext(), name);
		if (d != null) v.setBackground(d);
	}

	static void applyDefaultFont(Context ctx, Button btn) {
		Typeface tf = resolveFont(ctx, "megalopolis_extra.otf");
		if (tf != null) btn.setTypeface(tf);
	}

	public static void removeAll() {
		ui(() -> {
			ViewGroup r = root();
			if (r == null) return;
			for (Buttons.Data d : buttons.values()) r.removeView(d.button);
			for (Overlays.Data d : overlays.values()) r.removeView(d.frame);
			for (Drawers.Data d : drawers.values()) r.removeView(d.frame);
			for (SeekBars.Data d : seekBars.values()) r.removeView(d.seekBar);
			for (TextInputs.Data d : textInputs.values()) r.removeView(d.editText);
			buttons.clear(); overlays.clear(); drawers.clear(); seekBars.clear(); textInputs.clear();
			Log.d(TAG, "Destroyed all");
		});
	}

	public static void setHiddenAll(boolean hidden) {
		ui(() -> {
			globallyHidden = hidden;
			for (Buttons.Data d : buttons.values()) Buttons.updateVisibility(d);
			for (Overlays.Data d : overlays.values())
				d.frame.setVisibility(!globallyHidden && !d.hidden ? View.VISIBLE : View.GONE);
			for (Drawers.Data d : drawers.values())
				d.frame.setVisibility(!globallyHidden ? View.VISIBLE : View.GONE);
			for (SeekBars.Data d : seekBars.values())
				d.seekBar.setVisibility(!globallyHidden && !d.hidden ? View.VISIBLE : View.GONE);
			for (TextInputs.Data d : textInputs.values()) {
				d.editText.setVisibility(!globallyHidden && !d.hidden ? View.VISIBLE : View.GONE);
				if (globallyHidden || d.hidden) { d.editText.clearFocus(); hideKeyboard(d.editText); }
			}
		});
	}
}
