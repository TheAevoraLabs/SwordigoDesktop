package in.aevora.ruby.lang;

import android.os.Bundle;
import androidx.annotation.NonNull;

import io.github.rosemoe.sora.lang.EmptyLanguage;
import io.github.rosemoe.sora.lang.analysis.AnalyzeManager;
import io.github.rosemoe.sora.lang.analysis.SimpleAnalyzeManager;
import io.github.rosemoe.sora.lang.styling.CodeBlock;
import io.github.rosemoe.sora.lang.styling.MappedSpans;
import io.github.rosemoe.sora.lang.styling.SpanFactory;
import io.github.rosemoe.sora.lang.styling.Styles;
import io.github.rosemoe.sora.lang.styling.TextStyle;
import io.github.rosemoe.sora.text.CharPosition;
import io.github.rosemoe.sora.text.ContentReference;
import io.github.rosemoe.sora.widget.SymbolPairMatch;
import io.github.rosemoe.sora.widget.schemes.EditorColorScheme;

import java.util.ArrayDeque;
import java.util.Arrays;
import java.util.Collections;
import java.util.Deque;
import java.util.HashSet;
import java.util.Set;

/**
 * FileRiftLanguage - Syntax highlighter & code analysis for Swordigo FileRift markup
 * (.scene, .scl, .scmap, .gplayer) with embedded Lua support.
 */
public class FileRiftLanguage extends EmptyLanguage {

    private final FileRiftAnalyzeManager mAnalyzeManager = new FileRiftAnalyzeManager();
    private final SymbolPairMatch mSymbolPairs = new SymbolPairMatch.DefaultSymbolPairs();

    private static final Set<String> KEYWORDS = new HashSet<>(Arrays.asList(
        "Object", "Component", "Template", "Package", "import", "export",
        "struct", "table", "null", "true", "false",
        "LocalAabb", "Position", "Depth", "Rotation", "Scaling", "Hidden",
        "OnLoad", "ClassName", "Identifier", "TemplateName", "Points", "Flags",
        "min", "max", "x", "y", "z", "w", "r", "g", "b", "a"
    ));

    private static final Set<String> SWORDIGO_COMPONENTS = new HashSet<>(Arrays.asList(
        "BackgroundComponent", "MeshComponent", "GroundPolygonComponent",
        "PhysicsComponent", "SpawnPointComponent", "WaterMesh",
        "MovingPlatformComponent", "TriggerComponent", "ItemComponent",
        "EnemyComponent", "DoorComponent", "LightComponent",
        "AudioSourceComponent", "CameraComponent", "ParticleComponent"
    ));

    private static final Set<String> LUA_KEYWORDS = new HashSet<>(Arrays.asList(
        "and", "break", "do", "else", "elseif", "end", "false", "for",
        "function", "if", "in", "local", "nil", "not", "or", "repeat",
        "return", "then", "true", "until", "while"
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

    private static class FileRiftAnalyzeManager extends SimpleAnalyzeManager<Void> {
        @Override
        protected Styles analyze(StringBuilder text, Delegate<Void> delegate) {
            Styles styles = new Styles();
            MappedSpans.Builder builder = new MappedSpans.Builder();
            Deque<BlockPos> braceStack = new ArrayDeque<>();
            Deque<BlockPos> luaStack = new ArrayDeque<>();

            int len = text.length();
            int line = 0;
            int col = 0;
            int i = 0;
            boolean inLuaBlock = false;

            while (i < len) {
                if (delegate.isCancelled()) return null;

                char c = text.charAt(i);

                // Handle Newlines
                if (c == '\n') {
                    line++;
                    col = 0;
                    i++;
                    continue;
                }

                // Whitespace
                if (Character.isWhitespace(c)) {
                    i++;
                    col++;
                    continue;
                }

                int tokenStartCol = col;

                // 1. Comments: '#' or '//' (or '--' when inside Lua block)
                if (c == '#' || (c == '/' && i + 1 < len && text.charAt(i + 1) == '/') ||
                    (inLuaBlock && c == '-' && i + 1 < len && text.charAt(i + 1) == '-')) {
                    int startI = i;
                    while (i < len && text.charAt(i) != '\n') {
                        i++;
                        col++;
                    }
                    builder.add(line, SpanFactory.obtainNoExt(tokenStartCol,
                        TextStyle.makeStyle(EditorColorScheme.COMMENT, 0, false, true, false, true)));
                    builder.add(line, SpanFactory.obtainNoExt(col,
                        TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                    continue;
                }

                // 2. Directives: $lua and $end
                if (c == '$') {
                    int startI = i;
                    i++;
                    col++;
                    while (i < len && Character.isLetterOrDigit(text.charAt(i))) {
                        i++;
                        col++;
                    }
                    String tag = text.substring(startI, i);
                    if ("$lua".equals(tag)) {
                        inLuaBlock = true;
                        luaStack.push(new BlockPos(line, tokenStartCol));
                    } else if ("$end".equals(tag)) {
                        inLuaBlock = false;
                        if (!luaStack.isEmpty()) {
                            BlockPos start = luaStack.pop();
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
                    builder.add(line, SpanFactory.obtainNoExt(tokenStartCol,
                        TextStyle.makeStyle(EditorColorScheme.ANNOTATION, 0, true, false, false)));
                    builder.add(line, SpanFactory.obtainNoExt(col,
                        TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                    continue;
                }

                // 3. String literals ("..." or '...')
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
                        TextStyle.makeStyle(EditorColorScheme.LITERAL, 0, false, false, false)));
                    builder.add(line, SpanFactory.obtainNoExt(col,
                        TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                    continue;
                }

                // 4. Numbers
                if (Character.isDigit(c) || (c == '-' && i + 1 < len && Character.isDigit(text.charAt(i + 1)))) {
                    i++;
                    col++;
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
                        TextStyle.makeStyle(EditorColorScheme.ATTRIBUTE_VALUE, 0, false, false, false)));
                    builder.add(line, SpanFactory.obtainNoExt(col,
                        TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                    continue;
                }

                // 5. Braces and Brackets (Track for code folding)
                if (c == '{') {
                    braceStack.push(new BlockPos(line, tokenStartCol));
                    builder.add(line, SpanFactory.obtainNoExt(tokenStartCol,
                        TextStyle.makeStyle(EditorColorScheme.OPERATOR)));
                    i++;
                    col++;
                    builder.add(line, SpanFactory.obtainNoExt(col,
                        TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                    continue;
                }
                if (c == '}') {
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

                // Other operators / punctuation
                if (c == '[' || c == ']' || c == '(' || c == ')' || c == ':' || c == '=' || c == ',' || c == ';') {
                    builder.add(line, SpanFactory.obtainNoExt(tokenStartCol,
                        TextStyle.makeStyle(EditorColorScheme.OPERATOR)));
                    i++;
                    col++;
                    builder.add(line, SpanFactory.obtainNoExt(col,
                        TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                    continue;
                }

                // 6. Identifiers / Words
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
                    if (inLuaBlock && LUA_KEYWORDS.contains(word)) {
                        style = TextStyle.makeStyle(EditorColorScheme.KEYWORD, 0, true, false, false);
                    } else if (SWORDIGO_COMPONENTS.contains(word)) {
                        style = TextStyle.makeStyle(EditorColorScheme.IDENTIFIER_VAR, 0, true, false, false);
                    } else if (KEYWORDS.contains(word)) {
                        style = TextStyle.makeStyle(EditorColorScheme.KEYWORD, 0, true, false, false);
                    } else {
                        style = TextStyle.makeStyle(EditorColorScheme.IDENTIFIER_NAME);
                    }
                    builder.add(line, SpanFactory.obtainNoExt(tokenStartCol, style));
                    builder.add(line, SpanFactory.obtainNoExt(col,
                        TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                    continue;
                }

                // Any other character
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
