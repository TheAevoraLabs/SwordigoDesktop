package in.aevora.ruby.lang;

import io.github.rosemoe.sora.widget.schemes.EditorColorScheme;

/**
 * SchemeRubyDark - Custom sleek dark color scheme for RubyTouch Code Studio.
 * Matches RubyTouch's dark charcoal, deep crimson, and cyan UI palette.
 */
public class SchemeRubyDark extends EditorColorScheme {

    public SchemeRubyDark() {
        super(true);
    }

    @Override
    public void applyDefault() {
        super.applyDefault();

        // Base & Background
        setColor(WHOLE_BACKGROUND, 0xFF121316);
        setColor(CURRENT_LINE, 0xFF181A20);
        setColor(TEXT_NORMAL, 0xFFE5E9F0);

        // Gutter & Line Numbers
        setColor(LINE_NUMBER_BACKGROUND, 0xFF14161A);
        setColor(LINE_NUMBER, 0xFF5C6370);
        setColor(LINE_NUMBER_CURRENT, 0xFFE5E9F0);
        setColor(LINE_DIVIDER, 0xFF262932);

        // Selection & Search Matches
        setColor(SELECTED_TEXT_BACKGROUND, 0xFF2D323E);
        setColor(SELECTION_HANDLE, 0xFFE06C75);
        setColor(SELECTION_INSERT, 0xFFE06C75);
        setColor(MATCHED_TEXT_BACKGROUND, 0xFF434C5E);

        // Syntax Highlighting Tokens
        setColor(KEYWORD, 0xFFE06C75);          // Crimson
        setColor(IDENTIFIER_NAME, 0xFFABB2BF);  // Light Gray
        setColor(IDENTIFIER_VAR, 0xFF56B6C2);   // Cyan (components / types)
        setColor(FUNCTION_NAME, 0xFF61AFEF);    // Blue
        setColor(LITERAL, 0xFF98C379);          // Olive Green (strings)
        setColor(ATTRIBUTE_VALUE, 0xFFD19A66);  // Orange (numbers)
        setColor(OPERATOR, 0xFFC678DD);         // Purple
        setColor(COMMENT, 0xFF5C6370);          // Slate Gray (italic)
        setColor(ANNOTATION, 0xFFE5C07B);       // Gold / Tag

        // Blocks & Folding
        setColor(BLOCK_LINE, 0xFF262932);
        setColor(BLOCK_LINE_CURRENT, 0xFFE06C75);

        // Scrollbars
        setColor(SCROLL_BAR_TRACK, 0xFF14161A);
        setColor(SCROLL_BAR_THUMB, 0xFF3A3F4B);
        setColor(SCROLL_BAR_THUMB_PRESSED, 0xFF4C5363);

        // Completion & Popups
        setColor(COMPLETION_WND_BACKGROUND, 0xFF181A20);
        setColor(COMPLETION_WND_CORNER, 0xFF262932);
        setColor(COMPLETION_WND_TEXT_PRIMARY, 0xFFE5E9F0);
        setColor(COMPLETION_WND_TEXT_SECONDARY, 0xFF5C6370);
    }
}
