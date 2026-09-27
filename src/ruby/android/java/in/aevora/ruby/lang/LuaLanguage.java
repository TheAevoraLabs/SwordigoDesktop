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
 * LuaLanguage - Syntax highlighter & code analysis for Lua 5.1 scripts (.lua).
 */
public class LuaLanguage extends EmptyLanguage {

    private final LuaAnalyzeManager mAnalyzeManager = new LuaAnalyzeManager();
    private final SymbolPairMatch mSymbolPairs = new SymbolPairMatch.DefaultSymbolPairs();

    private static final Set<String> KEYWORDS = new HashSet<>(Arrays.asList(
        "and", "break", "do", "else", "elseif", "end", "false", "for",
        "function", "if", "in", "local", "nil", "not", "or", "repeat",
        "return", "then", "true", "until", "while"
    ));

    private static final Set<String> STD_LIBS = new HashSet<>(Arrays.asList(
        "math", "string", "table", "os", "io", "coroutine", "debug",
        "print", "tostring", "tonumber", "type", "pairs", "ipairs",
        "pcall", "xpcall", "assert", "error", "select", "setmetatable", "getmetatable"
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

    private static class LuaAnalyzeManager extends SimpleAnalyzeManager<Void> {
        @Override
        protected Styles analyze(StringBuilder text, Delegate<Void> delegate) {
            Styles styles = new Styles();
            MappedSpans.Builder builder = new MappedSpans.Builder();
            Deque<BlockPos> blockStack = new ArrayDeque<>();

            int len = text.length();
            int line = 0;
            int col = 0;
            int i = 0;

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

                // 1. Comments: '--' or multi-line '--[['
                if (c == '-' && i + 1 < len && text.charAt(i + 1) == '-') {
                    i += 2;
                    col += 2;
                    if (i + 1 < len && text.charAt(i) == '[' && text.charAt(i + 1) == '[') {
                        // Multi-line comment --[[ ... ]]
                        i += 2;
                        col += 2;
                        while (i < len) {
                            if (text.charAt(i) == '\n') {
                                line++;
                                col = 0;
                                i++;
                                continue;
                            }
                            if (text.charAt(i) == ']' && i + 1 < len && text.charAt(i + 1) == ']') {
                                i += 2;
                                col += 2;
                                break;
                            }
                            i++;
                            col++;
                        }
                    } else {
                        // Single-line comment
                        while (i < len && text.charAt(i) != '\n') {
                            i++;
                            col++;
                        }
                    }
                    builder.add(line, SpanFactory.obtainNoExt(tokenStartCol,
                        TextStyle.makeStyle(EditorColorScheme.COMMENT, 0, false, true, false, true)));
                    builder.add(line, SpanFactory.obtainNoExt(col,
                        TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                    continue;
                }

                // 2. Strings: "...", '...', or [[...]]
                if (c == '"' || c == '\'') {
                    char quote = c;
                    i++;
                    col++;
                    while (i < len) {
                        char sc = text.charAt(i);
                        if (sc == '\\' && i + 1 < len) {
                            i += 2;
                            col += 2;
                            continue;
                        }
                        if (sc == quote || sc == '\n') {
                            if (sc == quote) { i++; col++; }
                            break;
                        }
                        i++;
                        col++;
                    }
                    builder.add(line, SpanFactory.obtainNoExt(tokenStartCol,
                        TextStyle.makeStyle(EditorColorScheme.LITERAL)));
                    builder.add(line, SpanFactory.obtainNoExt(col,
                        TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                    continue;
                }
                if (c == '[' && i + 1 < len && text.charAt(i + 1) == '[') {
                    // Multi-line raw string [[...]]
                    i += 2;
                    col += 2;
                    while (i < len) {
                        if (text.charAt(i) == '\n') {
                            line++;
                            col = 0;
                            i++;
                            continue;
                        }
                        if (text.charAt(i) == ']' && i + 1 < len && text.charAt(i + 1) == ']') {
                            i += 2;
                            col += 2;
                            break;
                        }
                        i++;
                        col++;
                    }
                    builder.add(line, SpanFactory.obtainNoExt(tokenStartCol,
                        TextStyle.makeStyle(EditorColorScheme.LITERAL)));
                    builder.add(line, SpanFactory.obtainNoExt(col,
                        TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                    continue;
                }

                // 3. Numbers
                if (Character.isDigit(c) || (c == '.' && i + 1 < len && Character.isDigit(text.charAt(i + 1)))) {
                    while (i < len) {
                        char nc = text.charAt(i);
                        if (Character.isDigit(nc) || nc == '.' || nc == 'x' || nc == 'X' ||
                            (nc >= 'a' && nc <= 'f') || (nc >= 'A' && nc <= 'F')) {
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

                // 4. Braces for tables
                if (c == '{') {
                    blockStack.push(new BlockPos(line, tokenStartCol));
                    builder.add(line, SpanFactory.obtainNoExt(tokenStartCol,
                        TextStyle.makeStyle(EditorColorScheme.OPERATOR)));
                    i++;
                    col++;
                    builder.add(line, SpanFactory.obtainNoExt(col,
                        TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                    continue;
                }
                if (c == '}') {
                    if (!blockStack.isEmpty()) {
                        BlockPos start = blockStack.pop();
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

                // Operators
                if ("()[]:;,=+-*/%^#<>~".indexOf(c) != -1) {
                    builder.add(line, SpanFactory.obtainNoExt(tokenStartCol,
                        TextStyle.makeStyle(EditorColorScheme.OPERATOR)));
                    i++;
                    col++;
                    builder.add(line, SpanFactory.obtainNoExt(col,
                        TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                    continue;
                }

                // 5. Identifiers & Keywords
                if (Character.isLetter(c) || c == '_') {
                    int startI = i;
                    while (i < len) {
                        char ic = text.charAt(i);
                        if (Character.isLetterOrDigit(ic) || ic == '_') {
                            i++;
                            col++;
                        } else {
                            break;
                        }
                    }
                    String word = text.substring(startI, i);
                    long style;
                    if (KEYWORDS.contains(word)) {
                        style = TextStyle.makeStyle(EditorColorScheme.KEYWORD, 0, true, false, false);
                        if ("function".equals(word) || "then".equals(word) || "do".equals(word) || "repeat".equals(word)) {
                            blockStack.push(new BlockPos(line, tokenStartCol));
                        } else if ("end".equals(word) || "until".equals(word)) {
                            if (!blockStack.isEmpty()) {
                                BlockPos start = blockStack.pop();
                                if (line > start.line) {
                                    CodeBlock block = new CodeBlock();
                                    block.startLine = start.line;
                                    block.startColumn = start.col;
                                    block.endLine = line;
                                    block.endColumn = col;
                                    styles.addCodeBlock(block);
                                }
                            }
                        }
                    } else if (STD_LIBS.contains(word)) {
                        style = TextStyle.makeStyle(EditorColorScheme.IDENTIFIER_VAR, 0, false, false, false);
                    } else {
                        style = TextStyle.makeStyle(EditorColorScheme.IDENTIFIER_NAME);
                    }
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
