package in.aevora.ruby;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextUtils;
import android.text.TextWatcher;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import in.aevora.ruby.lang.FileRiftLanguage;
import in.aevora.ruby.lang.GlslLanguage;
import in.aevora.ruby.lang.JsonLanguage;
import in.aevora.ruby.lang.LuaLanguage;
import in.aevora.ruby.lang.SchemeRubyDark;

import io.github.rosemoe.sora.event.ContentChangeEvent;
import io.github.rosemoe.sora.event.PublishSearchResultEvent;
import io.github.rosemoe.sora.lang.EmptyLanguage;
import io.github.rosemoe.sora.lang.Language;
import io.github.rosemoe.sora.widget.CodeEditor;
import io.github.rosemoe.sora.widget.EditorSearcher;

import java.io.File;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * CodeEditorActivity - High-performance native code editor for RubyTouch
 * powered by Sora Editor (io.github.rosemoe:editor).
 *
 * Features:
 *   - Clean, professional dark IDE UI with zero emojis
 *   - Strictly fixed-height top toolbar (48dp) and accessory bar (40dp)
 *   - 120 FPS virtualized line rendering on hardware-accelerated Canvas
 *   - Asynchronous FileRift protobuf decode/recode JNI bridging
 *   - Dedicated mobile symbol bar with touch-ergonomic undo/redo/pairs
 *   - "3D" app bar button for .scene and .scl files to return to viewport
 *   - Fast background syntax highlighting for FileRift, Lua, GLSL, and JSON
 *   - Compact inline search and replace with match count navigation
 */
public class CodeEditorActivity extends Activity {
    private static final String TAG = "CodeEditorActivity";

    static {
        try {
            System.loadLibrary("c++_shared");
        } catch (Throwable ignored) {}

        boolean loaded = false;
        if (Build.SUPPORTED_ABIS != null) {
            for (String abi : Build.SUPPORTED_ABIS) {
                try {
                    System.loadLibrary("ruby_" + abi);
                    loaded = true;
                    Log.i(TAG, "Loaded native ruby_" + abi);
                    break;
                } catch (Throwable ignored) {}
            }
        }
        if (!loaded) {
            try {
                System.loadLibrary("ruby");
                loaded = true;
                Log.i(TAG, "Loaded native ruby");
            } catch (Throwable t) {
                Log.w(TAG, "Native library ruby load note: " + t.getMessage());
            }
        }
    }

    // Native JNI FileRift methods from scene_orientation.cpp
    public static native String nativeLoadFile(String path, boolean[] outIsFilerift, String[] outFileType);
    public static native String nativeSaveFile(String path, String content, boolean transcodeFilerift);

    // Ruby Dark Design Palette
    private static final int BG_COLOR = 0xFF121316;
    private static final int SURFACE_COLOR = 0xFF181A20;
    private static final int SURFACE_ALT_COLOR = 0xFF15171C;
    private static final int BORDER_COLOR = 0xFF262932;
    private static final int TEXT_PRIMARY = 0xFFE5E9F0;
    private static final int TEXT_MUTED = 0xFF5C6370;
    private static final int ACCENT_CRIMSON = 0xFFE06C75;
    private static final int ACCENT_CYAN = 0xFF56B6C2;
    private static final int ACCENT_GREEN = 0xFF98C379;
    private static final int ACCENT_ORANGE = 0xFFD19A66;

    private String mFilePath = "";
    private boolean mIsModified = false;
    private boolean mIsFileriftTranscoded = false;
    private String mFileType = "";
    private boolean mInitialLoadDone = false;
    private boolean mIsWordWrap = false;

    // UI Widgets
    private CodeEditor mEditor;
    private TextView mTitleText;
    private View mStatusDot;
    private TextView mFormatBadge;
    private Button mSaveBtn;
    private Button mVisualBtn;
    private Button mUndoBtn;
    private Button mRedoBtn;
    private Button mWrapBtn;

    // Search Bar
    private LinearLayout mSearchBar;
    private EditText mSearchInput;
    private TextView mSearchCountLabel;
    private LinearLayout mReplaceRow;
    private EditText mReplaceInput;
    private boolean mSearchBarVisible = false;
    private boolean mReplaceRowVisible = false;

    // Loading overlay
    private FrameLayout mLoadingOverlay;
    private TextView mLoadingLabel;

    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private final ExecutorService mIoExecutor = Executors.newSingleThreadExecutor();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        super.onCreate(savedInstanceState);

        // Window background and soft input behavior
        Window window = getWindow();
        window.setBackgroundDrawable(new ColorDrawable(BG_COLOR));
        window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
        window.setStatusBarColor(SURFACE_COLOR);
        window.setNavigationBarColor(SURFACE_ALT_COLOR);

        mFilePath = getIntent().getStringExtra("file_path");
        if (mFilePath == null) mFilePath = "";

        // Root layout
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(BG_COLOR);

        // 1. Top Header Toolbar (Strictly fixed height 48dp)
        root.addView(buildHeaderView());

        // 1b. Header Divider Line (1dp)
        View headerDivider = new View(this);
        headerDivider.setBackgroundColor(BORDER_COLOR);
        root.addView(headerDivider, new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, dpToPx(1)));

        // 2. Search / Replace Panel (Collapsible)
        mSearchBar = buildSearchBar();
        mSearchBar.setVisibility(View.GONE);
        root.addView(mSearchBar);

        // 3. Editor Container with Loading Overlay
        FrameLayout editorContainer = new FrameLayout(this);
        LinearLayout.LayoutParams editorLp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, 0, 1.0f);
        editorContainer.setLayoutParams(editorLp);

        mEditor = new CodeEditor(this);
        mEditor.setColorScheme(new SchemeRubyDark());
        mEditor.setTextSize(14f);
        mEditor.setScalable(true);
        mEditor.setLineNumberEnabled(true);
        mEditor.setHighlightCurrentLine(true);
        mEditor.setHighlightBracketPair(true);
        mEditor.setTabWidth(4);
        mEditor.setTypefaceText(Typeface.MONOSPACE);
        mEditor.setTypefaceLineNumber(Typeface.MONOSPACE);
        mEditor.setWordwrap(false);

        // Track text changes for modified indicator and undo/redo state
        mEditor.subscribeEvent(ContentChangeEvent.class, (event, unsubscribe) -> {
            if (mInitialLoadDone) {
                mHandler.post(() -> {
                    if (!mIsModified) {
                        mIsModified = true;
                        updateStatusIndicators();
                    }
                    updateUndoRedoButtons();
                });
            }
        });

        // Search match updates
        mEditor.subscribeEvent(PublishSearchResultEvent.class, (event, unsubscribe) -> {
            mHandler.post(this::updateSearchMatchCount);
        });

        editorContainer.addView(mEditor, new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        // Loading overlay
        mLoadingOverlay = buildLoadingOverlay();
        editorContainer.addView(mLoadingOverlay, new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        root.addView(editorContainer);

        // 4. Accessory Bar Divider Line (1dp)
        View accessoryDivider = new View(this);
        accessoryDivider.setBackgroundColor(BORDER_COLOR);
        root.addView(accessoryDivider, new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, dpToPx(1)));

        // 5. Soft Keyboard Symbol Accessory Bar (Strictly fixed height 40dp)
        root.addView(buildAccessoryBar());

        setContentView(root);

        // Start loading the file asynchronously
        loadFileAsync();
    }

    private View buildHeaderView() {
        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setBackgroundColor(SURFACE_COLOR);
        bar.setPadding(dpToPx(10), 0, dpToPx(8), 0);

        LinearLayout.LayoutParams barLp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, dpToPx(48));
        bar.setLayoutParams(barLp);

        // Back button (vector icon, crisp 34dp x 34dp)
        ImageButton backBtn = new ImageButton(this);
        backBtn.setImageResource(R.drawable.ic_arrow_back_24);
        backBtn.setColorFilter(TEXT_PRIMARY);
        backBtn.setScaleType(ImageView.ScaleType.FIT_CENTER);
        backBtn.setPadding(dpToPx(6), dpToPx(6), dpToPx(6), dpToPx(6));
        backBtn.setBackground(createButtonRippleDrawable(SURFACE_COLOR, BORDER_COLOR, dpToPx(6)));
        LinearLayout.LayoutParams backLp = new LinearLayout.LayoutParams(dpToPx(34), dpToPx(34));
        backLp.rightMargin = dpToPx(6);
        backBtn.setLayoutParams(backLp);
        backBtn.setOnClickListener(v -> handleBackAction());
        bar.addView(backBtn);

        // Title Block (File name + clean status dot + format badge)
        LinearLayout titleCol = new LinearLayout(this);
        titleCol.setOrientation(LinearLayout.HORIZONTAL);
        titleCol.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams titleLp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1.0f);
        titleLp.leftMargin = dpToPx(2);
        titleLp.rightMargin = dpToPx(6);
        titleCol.setLayoutParams(titleLp);

        mTitleText = new TextView(this);
        mTitleText.setTextColor(TEXT_PRIMARY);
        mTitleText.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        mTitleText.setTypeface(Typeface.DEFAULT_BOLD);
        mTitleText.setSingleLine(true);
        mTitleText.setEllipsize(TextUtils.TruncateAt.MIDDLE);
        File f = new File(mFilePath);
        mTitleText.setText(f.getName().isEmpty() ? "Untitled" : f.getName());
        LinearLayout.LayoutParams textLp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        mTitleText.setLayoutParams(textLp);
        titleCol.addView(mTitleText);

        // Clean status dot (circular dot, no emoji)
        mStatusDot = new View(this);
        LinearLayout.LayoutParams dotLp = new LinearLayout.LayoutParams(dpToPx(6), dpToPx(6));
        dotLp.leftMargin = dpToPx(6);
        mStatusDot.setLayoutParams(dotLp);
        mStatusDot.setBackground(createDotDrawable(ACCENT_GREEN));
        titleCol.addView(mStatusDot);

        // Format badge (small pill badge for Protobuf / Scene / SCL)
        mFormatBadge = new TextView(this);
        mFormatBadge.setTextSize(TypedValue.COMPLEX_UNIT_SP, 9);
        mFormatBadge.setTextColor(ACCENT_CYAN);
        mFormatBadge.setTypeface(Typeface.DEFAULT_BOLD);
        mFormatBadge.setPadding(dpToPx(5), dpToPx(1), dpToPx(5), dpToPx(1));
        GradientDrawable badgeBg = new GradientDrawable();
        badgeBg.setColor(0xFF1E293B);
        badgeBg.setCornerRadius(dpToPx(3));
        mFormatBadge.setBackground(badgeBg);
        LinearLayout.LayoutParams badgeLp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        badgeLp.leftMargin = dpToPx(6);
        mFormatBadge.setLayoutParams(badgeLp);
        mFormatBadge.setVisibility(View.GONE);
        titleCol.addView(mFormatBadge);

        bar.addView(titleCol);

        // 3D Viewport return button (for .scene and .scl files)
        String lower = mFilePath.toLowerCase();
        if (lower.endsWith(".scene") || lower.endsWith(".scl")) {
            mVisualBtn = new Button(this);
            mVisualBtn.setText("3D");
            mVisualBtn.setTextColor(ACCENT_CYAN);
            mVisualBtn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
            mVisualBtn.setTypeface(Typeface.DEFAULT_BOLD);
            mVisualBtn.setPadding(dpToPx(8), 0, dpToPx(8), 0);
            mVisualBtn.setBackground(createPillDrawable(0xFF162630, ACCENT_CYAN, dpToPx(6)));
            LinearLayout.LayoutParams visualLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, dpToPx(30));
            visualLp.rightMargin = dpToPx(6);
            mVisualBtn.setLayoutParams(visualLp);
            mVisualBtn.setOnClickListener(v -> handle3DViewAction());
            bar.addView(mVisualBtn);
        }

        // Find Button (vector icon, crisp 34dp x 32dp)
        ImageButton findBtn = new ImageButton(this);
        findBtn.setImageResource(R.drawable.ic_search_24);
        findBtn.setColorFilter(TEXT_PRIMARY);
        findBtn.setScaleType(ImageView.ScaleType.FIT_CENTER);
        findBtn.setPadding(dpToPx(6), dpToPx(6), dpToPx(6), dpToPx(6));
        findBtn.setBackground(createButtonRippleDrawable(SURFACE_COLOR, BORDER_COLOR, dpToPx(6)));
        LinearLayout.LayoutParams findLp = new LinearLayout.LayoutParams(dpToPx(34), dpToPx(32));
        findLp.rightMargin = dpToPx(6);
        findBtn.setLayoutParams(findLp);
        findBtn.setOnClickListener(v -> toggleSearchBar());
        bar.addView(findBtn);

        // Save Button (Clean typography, dynamic color state, no emoji)
        mSaveBtn = new Button(this);
        mSaveBtn.setText("SAVE");
        mSaveBtn.setTextColor(0xFF707888);
        mSaveBtn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        mSaveBtn.setTypeface(Typeface.DEFAULT_BOLD);
        mSaveBtn.setPadding(dpToPx(12), 0, dpToPx(12), 0);
        mSaveBtn.setBackground(createButtonRippleDrawable(0xFF1E2128, BORDER_COLOR, dpToPx(6)));
        LinearLayout.LayoutParams saveLp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, dpToPx(30));
        mSaveBtn.setLayoutParams(saveLp);
        mSaveBtn.setOnClickListener(v -> saveFileAsync(null));
        bar.addView(mSaveBtn);

        return bar;
    }

    private LinearLayout buildSearchBar() {
        LinearLayout searchLayout = new LinearLayout(this);
        searchLayout.setOrientation(LinearLayout.VERTICAL);
        searchLayout.setBackgroundColor(SURFACE_ALT_COLOR);
        int padH = dpToPx(8);
        int padV = dpToPx(6);
        searchLayout.setPadding(padH, padV, padH, padV);

        // Row 1: Find Row
        LinearLayout row1 = new LinearLayout(this);
        row1.setOrientation(LinearLayout.HORIZONTAL);
        row1.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams row1Lp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, dpToPx(34));
        row1.setLayoutParams(row1Lp);

        mSearchInput = new EditText(this);
        mSearchInput.setHint("Find in file...");
        mSearchInput.setHintTextColor(TEXT_MUTED);
        mSearchInput.setTextColor(TEXT_PRIMARY);
        mSearchInput.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        mSearchInput.setBackground(createInputBackground());
        mSearchInput.setPadding(dpToPx(8), 0, dpToPx(8), 0);
        mSearchInput.setSingleLine(true);
        LinearLayout.LayoutParams searchLp = new LinearLayout.LayoutParams(0, dpToPx(30), 1.0f);
        mSearchInput.setLayoutParams(searchLp);
        mSearchInput.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {
                performSearch(s.toString());
            }
            @Override public void afterTextChanged(Editable s) {}
        });
        row1.addView(mSearchInput);

        mSearchCountLabel = new TextView(this);
        mSearchCountLabel.setTextColor(TEXT_MUTED);
        mSearchCountLabel.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
        mSearchCountLabel.setTypeface(Typeface.MONOSPACE);
        mSearchCountLabel.setPadding(dpToPx(6), 0, dpToPx(6), 0);
        mSearchCountLabel.setText("0/0");
        row1.addView(mSearchCountLabel);

        // Prev match button
        ImageButton prevBtn = new ImageButton(this);
        prevBtn.setImageResource(R.drawable.ic_arrow_up_24);
        prevBtn.setColorFilter(TEXT_PRIMARY);
        prevBtn.setScaleType(ImageView.ScaleType.FIT_CENTER);
        prevBtn.setPadding(dpToPx(4), dpToPx(4), dpToPx(4), dpToPx(4));
        prevBtn.setBackground(createButtonRippleDrawable(SURFACE_COLOR, BORDER_COLOR, dpToPx(4)));
        LinearLayout.LayoutParams prevLp = new LinearLayout.LayoutParams(dpToPx(28), dpToPx(28));
        prevLp.leftMargin = dpToPx(2);
        prevBtn.setLayoutParams(prevLp);
        prevBtn.setOnClickListener(v -> {
            if (mEditor != null && mEditor.getSearcher().hasQuery()) {
                mEditor.getSearcher().gotoPrevious();
                updateSearchMatchCount();
            }
        });
        row1.addView(prevBtn);

        // Next match button
        ImageButton nextBtn = new ImageButton(this);
        nextBtn.setImageResource(R.drawable.ic_arrow_down_24);
        nextBtn.setColorFilter(TEXT_PRIMARY);
        nextBtn.setScaleType(ImageView.ScaleType.FIT_CENTER);
        nextBtn.setPadding(dpToPx(4), dpToPx(4), dpToPx(4), dpToPx(4));
        nextBtn.setBackground(createButtonRippleDrawable(SURFACE_COLOR, BORDER_COLOR, dpToPx(4)));
        LinearLayout.LayoutParams nextLp = new LinearLayout.LayoutParams(dpToPx(28), dpToPx(28));
        nextLp.leftMargin = dpToPx(2);
        nextBtn.setLayoutParams(nextLp);
        nextBtn.setOnClickListener(v -> {
            if (mEditor != null && mEditor.getSearcher().hasQuery()) {
                mEditor.getSearcher().gotoNext();
                updateSearchMatchCount();
            }
        });
        row1.addView(nextBtn);

        // Replace toggle button
        Button repToggleBtn = new Button(this);
        repToggleBtn.setText("Rep");
        repToggleBtn.setTextColor(TEXT_PRIMARY);
        repToggleBtn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
        repToggleBtn.setBackground(createButtonRippleDrawable(SURFACE_COLOR, BORDER_COLOR, dpToPx(4)));
        repToggleBtn.setPadding(dpToPx(6), 0, dpToPx(6), 0);
        LinearLayout.LayoutParams repToggleLp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, dpToPx(28));
        repToggleLp.leftMargin = dpToPx(3);
        repToggleBtn.setLayoutParams(repToggleLp);
        repToggleBtn.setOnClickListener(v -> {
            mReplaceRowVisible = !mReplaceRowVisible;
            mReplaceRow.setVisibility(mReplaceRowVisible ? View.VISIBLE : View.GONE);
            repToggleBtn.setTextColor(mReplaceRowVisible ? ACCENT_CYAN : TEXT_PRIMARY);
        });
        row1.addView(repToggleBtn);

        // Close search button
        ImageButton closeBtn = new ImageButton(this);
        closeBtn.setImageResource(R.drawable.ic_close_24);
        closeBtn.setColorFilter(TEXT_MUTED);
        closeBtn.setScaleType(ImageView.ScaleType.FIT_CENTER);
        closeBtn.setPadding(dpToPx(4), dpToPx(4), dpToPx(4), dpToPx(4));
        closeBtn.setBackground(createButtonRippleDrawable(SURFACE_COLOR, BORDER_COLOR, dpToPx(4)));
        LinearLayout.LayoutParams closeLp = new LinearLayout.LayoutParams(dpToPx(28), dpToPx(28));
        closeLp.leftMargin = dpToPx(2);
        closeBtn.setLayoutParams(closeLp);
        closeBtn.setOnClickListener(v -> toggleSearchBar());
        row1.addView(closeBtn);

        searchLayout.addView(row1);

        // Row 2: Replace Row (Collapsible)
        mReplaceRow = new LinearLayout(this);
        mReplaceRow.setOrientation(LinearLayout.HORIZONTAL);
        mReplaceRow.setGravity(Gravity.CENTER_VERTICAL);
        mReplaceRow.setVisibility(View.GONE);
        LinearLayout.LayoutParams repRowLp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, dpToPx(34));
        repRowLp.topMargin = dpToPx(4);
        mReplaceRow.setLayoutParams(repRowLp);

        mReplaceInput = new EditText(this);
        mReplaceInput.setHint("Replace with...");
        mReplaceInput.setHintTextColor(TEXT_MUTED);
        mReplaceInput.setTextColor(TEXT_PRIMARY);
        mReplaceInput.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        mReplaceInput.setBackground(createInputBackground());
        mReplaceInput.setPadding(dpToPx(8), 0, dpToPx(8), 0);
        mReplaceInput.setSingleLine(true);
        LinearLayout.LayoutParams repLp = new LinearLayout.LayoutParams(0, dpToPx(30), 1.0f);
        mReplaceInput.setLayoutParams(repLp);
        mReplaceRow.addView(mReplaceInput);

        Button repBtn = new Button(this);
        repBtn.setText("This");
        repBtn.setTextColor(TEXT_PRIMARY);
        repBtn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
        repBtn.setBackground(createButtonRippleDrawable(SURFACE_COLOR, BORDER_COLOR, dpToPx(4)));
        repBtn.setPadding(dpToPx(8), 0, dpToPx(8), 0);
        LinearLayout.LayoutParams repBtnLp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, dpToPx(28));
        repBtnLp.leftMargin = dpToPx(4);
        repBtn.setLayoutParams(repBtnLp);
        repBtn.setOnClickListener(v -> {
            if (mEditor != null && mEditor.getSearcher().hasQuery()) {
                mEditor.getSearcher().replaceCurrentMatch(mReplaceInput.getText().toString());
                updateSearchMatchCount();
            }
        });
        mReplaceRow.addView(repBtn);

        Button repAllBtn = new Button(this);
        repAllBtn.setText("All");
        repAllBtn.setTextColor(ACCENT_ORANGE);
        repAllBtn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
        repAllBtn.setBackground(createButtonRippleDrawable(SURFACE_COLOR, BORDER_COLOR, dpToPx(4)));
        repAllBtn.setPadding(dpToPx(8), 0, dpToPx(8), 0);
        LinearLayout.LayoutParams repAllLp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, dpToPx(28));
        repAllLp.leftMargin = dpToPx(3);
        repAllBtn.setLayoutParams(repAllLp);
        repAllBtn.setOnClickListener(v -> {
            if (mEditor != null && mEditor.getSearcher().hasQuery()) {
                mEditor.getSearcher().replaceAll(mReplaceInput.getText().toString());
                updateSearchMatchCount();
            }
        });
        mReplaceRow.addView(repAllBtn);

        searchLayout.addView(mReplaceRow);

        // Bottom separator under search layout
        View searchDivider = new View(this);
        searchDivider.setBackgroundColor(BORDER_COLOR);
        LinearLayout.LayoutParams divLp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, dpToPx(1));
        divLp.topMargin = dpToPx(6);
        searchLayout.addView(searchDivider, divLp);

        return searchLayout;
    }

    private View buildAccessoryBar() {
        HorizontalScrollView scroll = new HorizontalScrollView(this);
        scroll.setBackgroundColor(SURFACE_ALT_COLOR);
        scroll.setHorizontalScrollBarEnabled(false);
        scroll.setOverScrollMode(View.OVER_SCROLL_NEVER);

        LinearLayout.LayoutParams scrollLp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, dpToPx(40));
        scroll.setLayoutParams(scrollLp);

        LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.HORIZONTAL);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        int padH = dpToPx(4);
        int padV = dpToPx(3);
        bar.setPadding(padH, padV, padH, padV);

        // Core Touch Controls: Undo, Redo, Tab, Untab, Wrap
        mUndoBtn = addSymbolButton(bar, "Undo", () -> {
            if (mEditor != null && mEditor.canUndo()) {
                mEditor.undo();
                updateUndoRedoButtons();
            }
        });
        mUndoBtn.setEnabled(false);
        mUndoBtn.setAlpha(0.4f);

        mRedoBtn = addSymbolButton(bar, "Redo", () -> {
            if (mEditor != null && mEditor.canRedo()) {
                mEditor.redo();
                updateUndoRedoButtons();
            }
        });
        mRedoBtn.setEnabled(false);
        mRedoBtn.setAlpha(0.4f);

        addSymbolButton(bar, "Tab", () -> {
            if (mEditor != null) mEditor.indentOrCommitTab();
        });
        addSymbolButton(bar, "Untab", () -> {
            if (mEditor != null) mEditor.unindentSelection();
        });

        mWrapBtn = addSymbolButton(bar, "Wrap", () -> {
            mIsWordWrap = !mIsWordWrap;
            if (mEditor != null) {
                mEditor.setWordwrap(mIsWordWrap);
            }
            mWrapBtn.setTextColor(mIsWordWrap ? ACCENT_CYAN : TEXT_PRIMARY);
        });

        // Surrounding pairs (Auto-wraps selection or inserts pair)
        addPairButton(bar, "{ }", "{", "}");
        addPairButton(bar, "[ ]", "[", "]");
        addPairButton(bar, "( )", "(", ")");
        addPairButton(bar, "\" \"", "\"", "\"");
        addPairButton(bar, "' '", "'", "'");
        addPairButton(bar, "< >", "<", ">");

        // Operators & Syntax Tokens
        addInsertButton(bar, "=", "=");
        addInsertButton(bar, ":", ":");
        addInsertButton(bar, ";", ";");
        addInsertButton(bar, ".", ".");
        addInsertButton(bar, ",", ",");
        addInsertButton(bar, "$", "$");
        addInsertButton(bar, "_", "_");
        addInsertButton(bar, "/", "/");
        addInsertButton(bar, "\\", "\\");
        addInsertButton(bar, "+", "+");
        addInsertButton(bar, "-", "-");
        addInsertButton(bar, "*", "*");
        addInsertButton(bar, "!", "!");
        addInsertButton(bar, "?", "?");
        addInsertButton(bar, "&", "&");
        addInsertButton(bar, "|", "|");
        addInsertButton(bar, "#", "#");

        // Language-specific tokens
        String low = mFilePath.toLowerCase();
        if (low.endsWith(".scene") || low.endsWith(".scl")) {
            addInsertButton(bar, "$lua", "$lua\n");
            addInsertButton(bar, "$end", "$end\n");
            addInsertButton(bar, "Object", "Object {\n    \n}");
            addInsertButton(bar, "Component", "Component {\n    \n}");
            addInsertButton(bar, "Position", "Position {\n    X : 0\n    Y : 0\n}");
            addInsertButton(bar, "LocalAabb", "LocalAabb {\n    X : 0\n    Y : 0\n    Width : 1\n    Height : 1\n}");
        }

        if (low.endsWith(".lua") || low.endsWith(".scene") || low.endsWith(".scl")) {
            addInsertButton(bar, "local", "local ");
            addInsertButton(bar, "function", "function ");
            addInsertButton(bar, "end", "end");
            addInsertButton(bar, "return", "return ");
            addInsertButton(bar, "if", "if ");
            addInsertButton(bar, "then", "then ");
            addInsertButton(bar, "else", "else\n    ");
            addInsertButton(bar, "true", "true");
            addInsertButton(bar, "false", "false");
            addInsertButton(bar, "nil", "nil");
        }

        if (low.endsWith(".vert") || low.endsWith(".frag") || low.endsWith(".glsl")) {
            addInsertButton(bar, "uniform", "uniform ");
            addInsertButton(bar, "attribute", "attribute ");
            addInsertButton(bar, "varying", "varying ");
            addInsertButton(bar, "vec2", "vec2 ");
            addInsertButton(bar, "vec3", "vec3 ");
            addInsertButton(bar, "vec4", "vec4 ");
            addInsertButton(bar, "mat4", "mat4 ");
            addInsertButton(bar, "float", "float ");
            addInsertButton(bar, "void", "void ");
        }

        scroll.addView(bar);
        return scroll;
    }

    private Button addSymbolButton(LinearLayout bar, String text, Runnable action) {
        Button b = new Button(this);
        b.setText(text);
        b.setTextColor(TEXT_PRIMARY);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        b.setTypeface(Typeface.MONOSPACE);
        b.setBackground(createButtonRippleDrawable(0xFF1E2128, 0xFF2C303B, dpToPx(5)));
        int padH = dpToPx(8);
        b.setPadding(padH, 0, padH, 0);
        b.setMinWidth(dpToPx(32));

        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, dpToPx(30));
        lp.setMargins(dpToPx(2), 0, dpToPx(2), 0);
        b.setLayoutParams(lp);
        b.setOnClickListener(v -> action.run());
        bar.addView(b);
        return b;
    }

    private void addInsertButton(LinearLayout bar, String label, String textToInsert) {
        addSymbolButton(bar, label, () -> {
            if (mEditor != null) {
                mEditor.insertText(textToInsert, textToInsert.length());
            }
        });
    }

    private void addPairButton(LinearLayout bar, String label, String open, String close) {
        addSymbolButton(bar, label, () -> {
            if (mEditor == null) return;
            if (mEditor.getCursor().isSelected()) {
                int left = mEditor.getCursor().getLeft();
                int right = mEditor.getCursor().getRight();
                CharSequence selected = mEditor.getText().subSequence(left, right);
                mEditor.getText().replace(left, right, open + selected + close);
                mEditor.setSelection(left + open.length(), right + open.length());
            } else {
                mEditor.insertText(open + close, open.length());
            }
        });
    }

    private FrameLayout buildLoadingOverlay() {
        FrameLayout overlay = new FrameLayout(this);
        overlay.setBackgroundColor(0xEE121316);
        overlay.setClickable(true);
        overlay.setFocusable(true);

        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setGravity(Gravity.CENTER);
        FrameLayout.LayoutParams boxLp = new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.CENTER);
        box.setLayoutParams(boxLp);

        ProgressBar spinner = new ProgressBar(this);
        spinner.setIndeterminateTintList(ColorStateList.valueOf(ACCENT_CRIMSON));
        box.addView(spinner);

        mLoadingLabel = new TextView(this);
        mLoadingLabel.setText("Loading file...");
        mLoadingLabel.setTextColor(TEXT_PRIMARY);
        mLoadingLabel.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        mLoadingLabel.setPadding(0, dpToPx(12), 0, 0);
        box.addView(mLoadingLabel);

        overlay.addView(box);
        return overlay;
    }

    private void loadFileAsync() {
        mLoadingOverlay.setVisibility(View.VISIBLE);
        mIoExecutor.execute(() -> {
            boolean[] isFilerift = new boolean[1];
            String[] fileType = new String[1];
            String content = null;
            String loadError = null;

            try {
                content = nativeLoadFile(mFilePath, isFilerift, fileType);
            } catch (Throwable t) {
                loadError = t.getMessage();
            }

            final String finalContent = content;
            final boolean finalIsFilerift = isFilerift[0];
            final String finalType = fileType[0] != null ? fileType[0] : "";
            final String finalError = loadError;

            mHandler.post(() -> {
                if (finalContent != null) {
                    mIsFileriftTranscoded = finalIsFilerift;
                    mFileType = finalType;

                    // Configure Language based on file type / extension
                    Language lang = selectLanguage(mFilePath, mFileType);
                    mEditor.setEditorLanguage(lang);

                    // Set text into Sora Editor virtualized buffer
                    mEditor.setText(finalContent);
                    mIsModified = false;
                    mInitialLoadDone = true;

                    // Format badge
                    if (mIsFileriftTranscoded) {
                        mFormatBadge.setText("PROTO");
                        mFormatBadge.setVisibility(View.VISIBLE);
                    } else if (!mFileType.isEmpty()) {
                        mFormatBadge.setText(mFileType.toUpperCase());
                        mFormatBadge.setVisibility(View.VISIBLE);
                    } else {
                        mFormatBadge.setVisibility(View.GONE);
                    }

                    updateStatusIndicators();
                    updateUndoRedoButtons();
                    mLoadingOverlay.setVisibility(View.GONE);
                } else {
                    mLoadingOverlay.setVisibility(View.GONE);
                    new AlertDialog.Builder(this)
                        .setTitle("Load Error")
                        .setMessage("Failed to load file:\n" + (finalError != null ? finalError : "Unknown error"))
                        .setPositiveButton("Exit", (dialog, which) -> finish())
                        .setCancelable(false)
                        .show();
                }
            });
        });
    }

    private Language selectLanguage(String filePath, String detectedType) {
        String low = filePath.toLowerCase();
        if (mIsFileriftTranscoded || "scene".equalsIgnoreCase(detectedType) || "scl".equalsIgnoreCase(detectedType) ||
            low.endsWith(".scene") || low.endsWith(".scl") || low.endsWith(".scmap") || low.endsWith(".gplayer")) {
            return new FileRiftLanguage();
        } else if (low.endsWith(".lua")) {
            return new LuaLanguage();
        } else if (low.endsWith(".vert") || low.endsWith(".frag") || low.endsWith(".glsl")) {
            return new GlslLanguage();
        } else if (low.endsWith(".json")) {
            return new JsonLanguage();
        }
        return new EmptyLanguage();
    }

    private void saveFileAsync(Runnable onComplete) {
        if (!mIsModified) {
            Toast.makeText(this, "No changes to save", Toast.LENGTH_SHORT).show();
            if (onComplete != null) onComplete.run();
            return;
        }

        mSaveBtn.setEnabled(false);
        mSaveBtn.setText("SAVING...");

        final String content = mEditor.getText().toString();
        mIoExecutor.execute(() -> {
            String error = null;
            try {
                error = nativeSaveFile(mFilePath, content, mIsFileriftTranscoded);
            } catch (Throwable t) {
                error = t.getMessage();
            }

            final String finalError = error;
            mHandler.post(() -> {
                mSaveBtn.setEnabled(true);
                if (finalError == null) {
                    mIsModified = false;
                    updateStatusIndicators();
                    Toast.makeText(this, "File saved successfully", Toast.LENGTH_SHORT).show();
                    if (onComplete != null) onComplete.run();
                } else {
                    updateStatusIndicators();
                    new AlertDialog.Builder(this)
                        .setTitle("Compilation Error")
                        .setMessage("Failed to compile and save markup:\n\n" + finalError)
                        .setPositiveButton("Keep Editing", null)
                        .show();
                }
            });
        });
    }

    private void handle3DViewAction() {
        if (mIsModified) {
            new AlertDialog.Builder(this)
                .setTitle("Save before 3D View?")
                .setMessage("Save changes to " + new File(mFilePath).getName() + " before switching to 3D Viewport?")
                .setPositiveButton("Save & Open", (dialog, which) -> {
                    saveFileAsync(this::returnToVisualViewport);
                })
                .setNeutralButton("Discard & Open", (dialog, which) -> {
                    returnToVisualViewport();
                })
                .setNegativeButton("Cancel", null)
                .show();
        } else {
            returnToVisualViewport();
        }
    }

    private void returnToVisualViewport() {
        Intent data = new Intent();
        data.putExtra("file_path", mFilePath);
        data.putExtra("action", "open_visual");
        setResult(RESULT_OK, data);
        finish();
    }

    private void handleBackAction() {
        if (mIsModified) {
            new AlertDialog.Builder(this)
                .setTitle("Unsaved Changes")
                .setMessage("Save changes before closing?")
                .setPositiveButton("Save & Close", (dialog, which) -> {
                    saveFileAsync(this::finish);
                })
                .setNeutralButton("Discard", (dialog, which) -> finish())
                .setNegativeButton("Cancel", null)
                .show();
        } else {
            finish();
        }
    }

    @Override
    public void onBackPressed() {
        handleBackAction();
    }

    private void toggleSearchBar() {
        mSearchBarVisible = !mSearchBarVisible;
        mSearchBar.setVisibility(mSearchBarVisible ? View.VISIBLE : View.GONE);
        if (mSearchBarVisible) {
            mSearchInput.requestFocus();
        } else {
            if (mEditor != null) {
                mEditor.getSearcher().stopSearch();
            }
        }
    }

    private void performSearch(String query) {
        if (mEditor == null) return;
        if (query == null || query.isEmpty()) {
            mEditor.getSearcher().stopSearch();
            mSearchCountLabel.setText("0/0");
            return;
        }
        try {
            mEditor.getSearcher().search(query, new EditorSearcher.SearchOptions(EditorSearcher.SearchOptions.TYPE_NORMAL, false));
        } catch (Exception e) {
            Log.w(TAG, "Search error: " + e.getMessage());
        }
    }

    private void updateSearchMatchCount() {
        if (mEditor == null || !mEditor.getSearcher().hasQuery()) {
            mSearchCountLabel.setText("0/0");
            return;
        }
        int count = mEditor.getSearcher().getMatchedPositionCount();
        int current = mEditor.getSearcher().getCurrentMatchedPositionIndex();
        mSearchCountLabel.setText((current >= 0 ? (current + 1) : 0) + "/" + count);
    }

    private void updateStatusIndicators() {
        mTitleText.setText(new File(mFilePath).getName());
        if (mIsModified) {
            mStatusDot.setBackground(createDotDrawable(ACCENT_CRIMSON));
            mSaveBtn.setText("SAVE");
            mSaveBtn.setTextColor(Color.WHITE);
            mSaveBtn.setBackground(createPillDrawable(ACCENT_CRIMSON, ACCENT_CRIMSON, dpToPx(6)));
        } else {
            mStatusDot.setBackground(createDotDrawable(ACCENT_GREEN));
            mSaveBtn.setText("SAVE");
            mSaveBtn.setTextColor(0xFF707888);
            mSaveBtn.setBackground(createButtonRippleDrawable(0xFF1E2128, BORDER_COLOR, dpToPx(6)));
        }
    }

    private void updateUndoRedoButtons() {
        if (mEditor != null) {
            if (mUndoBtn != null) {
                mUndoBtn.setEnabled(mEditor.canUndo());
                mUndoBtn.setAlpha(mEditor.canUndo() ? 1.0f : 0.4f);
            }
            if (mRedoBtn != null) {
                mRedoBtn.setEnabled(mEditor.canRedo());
                mRedoBtn.setAlpha(mEditor.canRedo() ? 1.0f : 0.4f);
            }
        }
    }

    private GradientDrawable createDotDrawable(int color) {
        GradientDrawable gd = new GradientDrawable();
        gd.setShape(GradientDrawable.OVAL);
        gd.setColor(color);
        return gd;
    }

    private GradientDrawable createPillDrawable(int bgColor, int strokeColor, int cornerRadiusPx) {
        GradientDrawable gd = new GradientDrawable();
        gd.setColor(bgColor);
        gd.setStroke(dpToPx(1), strokeColor);
        gd.setCornerRadius(cornerRadiusPx);
        return gd;
    }

    private RippleDrawable createButtonRippleDrawable(int bgColor, int strokeColor, int cornerRadiusPx) {
        GradientDrawable content = createPillDrawable(bgColor, strokeColor, cornerRadiusPx);
        GradientDrawable mask = new GradientDrawable();
        mask.setColor(Color.WHITE);
        mask.setCornerRadius(cornerRadiusPx);
        return new RippleDrawable(ColorStateList.valueOf(0x33FFFFFF), content, mask);
    }

    private GradientDrawable createInputBackground() {
        GradientDrawable gd = new GradientDrawable();
        gd.setColor(BG_COLOR);
        gd.setStroke(dpToPx(1), BORDER_COLOR);
        gd.setCornerRadius(dpToPx(6));
        return gd;
    }

    private int dpToPx(int dp) {
        return (int) (dp * getResources().getDisplayMetrics().density);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (mEditor != null) {
            mEditor.release();
        }
    }
}
