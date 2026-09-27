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
 * GlslLanguage - Syntax highlighter for OpenGL ES shaders (.vert, .frag, .glsl).
 */
public class GlslLanguage extends EmptyLanguage {

    private final GlslAnalyzeManager mAnalyzeManager = new GlslAnalyzeManager();
    private final SymbolPairMatch mSymbolPairs = new SymbolPairMatch.DefaultSymbolPairs();

    private static final Set<String> KEYWORDS = new HashSet<>(Arrays.asList(
        "attribute", "varying", "uniform", "precision", "lowp", "mediump", "highp",
        "void", "bool", "int", "uint", "float", "vec2", "vec3", "vec4",
        "bvec2", "bvec3", "bvec4", "ivec2", "ivec3", "ivec4", "uvec2", "uvec3", "uvec4",
        "mat2", "mat3", "mat4", "sampler2D", "samplerCube",
        "struct", "if", "else", "for", "while", "do", "return", "break", "continue", "discard",
        "layout", "in", "out", "inout"
    ));

    private static final Set<String> BUILTINS = new HashSet<>(Arrays.asList(
        "gl_Position", "gl_PointSize", "gl_FragColor", "gl_FragCoord", "gl_FrontFacing", "gl_PointCoord",
        "texture2D", "textureCube", "mix", "clamp", "step", "smoothstep",
        "length", "distance", "dot", "cross", "normalize", "reflect", "refract",
        "sin", "cos", "tan", "asin", "acos", "atan", "pow", "exp", "log", "sqrt", "inversesqrt",
        "abs", "sign", "floor", "ceil", "fract", "mod", "min", "max"
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

    private static class GlslAnalyzeManager extends SimpleAnalyzeManager<Void> {
        @Override
        protected Styles analyze(StringBuilder text, Delegate<Void> delegate) {
            Styles styles = new Styles();
            MappedSpans.Builder builder = new MappedSpans.Builder();
            Deque<BlockPos> braceStack = new ArrayDeque<>();

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

                // 1. Comments: '//' or '/* ... */'
                if (c == '/' && i + 1 < len) {
                    char next = text.charAt(i + 1);
                    if (next == '/') {
                        while (i < len && text.charAt(i) != '\n') {
                            i++;
                            col++;
                        }
                        builder.add(line, SpanFactory.obtainNoExt(tokenStartCol,
                            TextStyle.makeStyle(EditorColorScheme.COMMENT, 0, false, true, false, true)));
                        builder.add(line, SpanFactory.obtainNoExt(col,
                            TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                        continue;
                    } else if (next == '*') {
                        i += 2;
                        col += 2;
                        while (i < len) {
                            if (text.charAt(i) == '\n') {
                                line++;
                                col = 0;
                                i++;
                                continue;
                            }
                            if (text.charAt(i) == '*' && i + 1 < len && text.charAt(i + 1) == '/') {
                                i += 2;
                                col += 2;
                                break;
                            }
                            i++;
                            col++;
                        }
                        builder.add(line, SpanFactory.obtainNoExt(tokenStartCol,
                            TextStyle.makeStyle(EditorColorScheme.COMMENT, 0, false, true, false, true)));
                        builder.add(line, SpanFactory.obtainNoExt(col,
                            TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                        continue;
                    }
                }

                // 2. Preprocessor: '#...'
                if (c == '#') {
                    while (i < len && text.charAt(i) != '\n') {
                        i++;
                        col++;
                    }
                    builder.add(line, SpanFactory.obtainNoExt(tokenStartCol,
                        TextStyle.makeStyle(EditorColorScheme.ANNOTATION, 0, true, false, false)));
                    builder.add(line, SpanFactory.obtainNoExt(col,
                        TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                    continue;
                }

                // 3. Numbers
                if (Character.isDigit(c) || (c == '.' && i + 1 < len && Character.isDigit(text.charAt(i + 1)))) {
                    while (i < len) {
                        char nc = text.charAt(i);
                        if (Character.isDigit(nc) || nc == '.' || nc == 'f' || nc == 'F' || nc == 'u' || nc == 'U' ||
                            nc == 'x' || nc == 'X' || (nc >= 'a' && nc <= 'f') || (nc >= 'A' && nc <= 'F')) {
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

                // 4. Braces
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

                // Operators
                if ("()[]:;,=+-*/%^&|<>!~?".indexOf(c) != -1) {
                    builder.add(line, SpanFactory.obtainNoExt(tokenStartCol,
                        TextStyle.makeStyle(EditorColorScheme.OPERATOR)));
                    i++;
                    col++;
                    builder.add(line, SpanFactory.obtainNoExt(col,
                        TextStyle.makeStyle(EditorColorScheme.TEXT_NORMAL)));
                    continue;
                }

                // 5. Identifiers, Keywords, Builtins
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
                    } else if (BUILTINS.contains(word)) {
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
