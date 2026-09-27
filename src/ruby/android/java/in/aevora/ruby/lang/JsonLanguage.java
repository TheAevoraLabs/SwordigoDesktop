package in.aevora.ruby.lang;

import androidx.annotation.NonNull;

import io.github.rosemoe.sora.lang.EmptyLanguage;
import io.github.rosemoe.sora.lang.analysis.AnalyzeManager;
import io.github.rosemoe.sora.lang.analysis.SimpleAnalyzeManager;
import io.github.rosemoe.sora.lang.styling.CodeBlock;
import io.github.rosemoe.sora.lang.styling.MappedSpans;
import io.github.rosemoe.sora.lang.styling.SpanFactory;
import io.github.rosemoe.sora.lang.styling.Styles;
import io.github.rosemoe.sora.lang.styling.TextStyle;
import io.github.rosemoe.sora.widget.SymbolPairMatch;
import io.github.rosemoe.sora.widget.schemes.EditorColorScheme;

import java.util.ArrayDeque;
import java.util.Arrays;
import java.util.Collections;
import java.util.Deque;
import java.util.HashSet;
import java.util.Set;

/**
 * JsonLanguage - Syntax highlighter & code folding for JSON configuration files.
 */
public class JsonLanguage extends EmptyLanguage {

    private final JsonAnalyzeManager mAnalyzeManager = new JsonAnalyzeManager();
    private final SymbolPairMatch mSymbolPairs = new SymbolPairMatch.DefaultSymbolPairs();

    private static final Set<String> LITERALS = new HashSet<>(Arrays.asList(
        "true", "false", "null"
    ));

    @NonNull
    @Override
    public AnalyzeManager getAnalyzeManager() {
        return mAnalyzeManager;
    }

    @Override
    public SymbolPairMatch getSymbolPairs() {
        return mSymbolPairs;
    }

    private static class BlockPos {
        final int line;
        final int col;
        BlockPos(int l, int c) { line = l; col = c; }
    }

    private static class JsonAnalyzeManager extends SimpleAnalyzeManager<Void> {
        @Override
        protected Styles analyze(StringBuilder text, Delegate<Void> delegate) {
            Styles styles = new Styles();
            MappedSpans.Builder builder = new MappedSpans.Builder();
            Deque<BlockPos> braceStack = new ArrayDeque<>();

            int len = text.length();
            int line = 0;
            int col = 0;
            int i = 0;
            boolean expectingColonForProperty = false;

            while (i < len) {
                if (delegate.isCancelled()) return null;

                char c = text.charAt(i);

                if (c == '\n') {
                    line++;
                    col = 0;
                    i++;
                    continue;
                }

                if (Character.isWhitespace(c)) {
                    i++;
                    col++;
                    continue;
                }

                int tokenStartCol = col;

                // 1. Strings: key or value
                if (c == '"') {
                    i++;
                    col++;
                    while (i < len) {
                        char sc = text.charAt(i);
                        if (sc == '\\' && i + 1 < len) {
                            i += 2;
                            col += 2;
                            continue;
                        }
                        if (sc == '"' || sc == '\n') {
                            if (sc == '"') { i++; col++; }
                            break;
                        }
                        i++;
                        col++;
                    }

                    // Peek ahead to check if colon follows (meaning this is an object property key)
                    int peek = i;
                    while (peek < len && Character.isWhitespace(text.charAt(peek))) {
                        peek++;
                    }
                    boolean isKey = (peek < len && text.charAt(peek) == ':');
                    long style = isKey
                        ? TextStyle.makeStyle(EditorColorScheme.IDENTIFIER_VAR, 0, true, false, false)
                        : TextStyle.makeStyle(EditorColorScheme.LITERAL);

                    builder.add(line, SpanFactory.obtainNoExt(tokenStartCol, style));
                    builder.add(line, SpanFactory.obtainNoExt(col,
                        TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                    continue;
                }

                // 2. Numbers
                if (Character.isDigit(c) || (c == '-' && i + 1 < len && Character.isDigit(text.charAt(i + 1)))) {
                    while (i < len) {
                        char nc = text.charAt(i);
                        if (Character.isDigit(nc) || nc == '.' || nc == 'e' || nc == 'E' || nc == '+' || nc == '-') {
                            i++;
                            col++;
                        } else {
                            break;
                        }
                    }
                    builder.add(line, SpanFactory.obtainNoExt(tokenStartCol,
                        TextStyle.makeStyle(EditorColorScheme.ATTRIBUTE_VALUE)));
                    builder.add(line, SpanFactory.obtainNoExt(col,
                        TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                    continue;
                }

                // 3. Braces and Brackets
                if (c == '{' || c == '[') {
                    braceStack.push(new BlockPos(line, tokenStartCol));
                    builder.add(line, SpanFactory.obtainNoExt(tokenStartCol,
                        TextStyle.makeStyle(EditorColorScheme.OPERATOR)));
                    i++;
                    col++;
                    builder.add(line, SpanFactory.obtainNoExt(col,
                        TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                    continue;
                }
                if (c == '}' || c == ']') {
                    if (!braceStack.isEmpty()) {
                        BlockPos start = braceStack.pop();
                        if (line > start.line) {
                            CodeBlock block = new CodeBlock();
                            block.startLine = start.line;
                            block.startColumn = start.col;
                            block.endLine = line;
                            block.endColumn = col + 1;
                            styles.addCodeBlock(block);
                        }
                    }
                    builder.add(line, SpanFactory.obtainNoExt(tokenStartCol,
                        TextStyle.makeStyle(EditorColorScheme.OPERATOR)));
                    i++;
                    col++;
                    builder.add(line, SpanFactory.obtainNoExt(col,
                        TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                    continue;
                }

                if (c == ':' || c == ',') {
                    builder.add(line, SpanFactory.obtainNoExt(tokenStartCol,
                        TextStyle.makeStyle(EditorColorScheme.OPERATOR)));
                    i++;
                    col++;
                    builder.add(line, SpanFactory.obtainNoExt(col,
                        TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                    continue;
                }

                // 4. Literals: true, false, null
                if (Character.isLetter(c)) {
                    int startI = i;
                    while (i < len && Character.isLetter(text.charAt(i))) {
                        i++;
                        col++;
                    }
                    String word = text.substring(startI, i);
                    long style = LITERALS.contains(word)
                        ? TextStyle.makeStyle(EditorColorScheme.KEYWORD, 0, true, false, false)
                        : TextStyle.makeStyle(EditorColorScheme.IDENTIFIER_NAME);

                    builder.add(line, SpanFactory.obtainNoExt(tokenStartCol, style));
                    builder.add(line, SpanFactory.obtainNoExt(col,
                        TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                    continue;
                }

                i++;
                col++;
            }

            builder.determine(Math.max(0, line));
            styles.spans = builder.build();
            if (styles.blocks != null && !styles.blocks.isEmpty()) {
                Collections.sort(styles.blocks, CodeBlock.COMPARATOR_START);
            }
            return styles;
        }
    }
}
