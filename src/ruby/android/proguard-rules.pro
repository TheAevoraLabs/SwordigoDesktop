# ============================================================================
# proguard-rules.pro — keep rules for the Ruby Touch / Ruby GG Mobile R8 pass
#
# Used by src/ruby/android/build_android.sh when it runs R8 instead of d8.
#
# Why this file has to exist: the d8-built dex was 4.6 MB / 2,092 classes, and
# R8 tree-shaking is only safe if everything reached from OUTSIDE the Java world
# is kept. R8 cannot see:
#   * JNI — C++ calls FindClass("in/aevora/ruby/...") / GetMethodID("nativeFoo")
#     by STRING, so a class or method that looks unreferenced in Java can still
#     be the entry point of a native call,
#   * the AndroidManifest, which names Activities/Application by string,
#   * reflection — QtLoader and the Qt Java bridge instantiate org.qtproject.*
#     classes reflectively.
#
# What the dex actually contained, so the splits below are not guesswork:
#   635 io/github/rosemoe/sora  (the code editor)
#   ~800 kotlin/*               (kotlin-stdlib 2.1.0)
#   165 androidx/collection, 75 androidx/annotation
#   101 org/qtproject
#    87 in/aevora           <- our own Java, the part we control
#    10 com/touchfoo
#
# Deliberately conservative. Two flags do the work:
#   -dontobfuscate  a surviving class keeps its exact name  -> JNI/reflection safe
#   -dontoptimize   a surviving method keeps its exact code -> no rewriting
# so the ONLY thing this pass does is delete classes and members that nothing
# can reach. If it ever deletes something it should not, the fix is another
# -keep line here, never disabling the flags above.
# ============================================================================

-dontobfuscate
-dontoptimize

# Annotations and generic signatures are read reflectively by Qt and by
# kotlin-reflect; line numbers make an on-device stack trace usable.
-keepattributes *Annotation*,Signature,InnerClasses,EnclosingMethod,MethodParameters,SourceFile,LineNumberTable

# ── JNI reachability ────────────────────────────────────────────────────────
# Any class declaring a native method must keep that method's name, or the
# native side can never bind to it.
-keepclasseswithmembernames class * {
    native <methods>;
}

# Anything the native code may call back must keep its name too.
-keepclasseswithmembernames class * {
    public <methods>;
}

# ── AndroidManifest.xml components (named by string) ────────────────────────
-keep public class * extends android.app.Activity
-keep public class * extends android.app.Application
-keep public class * extends android.app.Service
-keep public class * extends android.content.BroadcastReceiver
-keep public class * extends android.content.ContentProvider

# Views instantiated from XML / by name.
-keep public class * extends android.view.View {
    public <init>(android.content.Context);
    public <init>(android.content.Context, android.util.AttributeSet);
    public <init>(android.content.Context, android.util.AttributeSet, int);
}

# ── Our own Java ────────────────────────────────────────────────────────────
# in.aevora.ruby holds RubyActivity / GameActivity / CodeEditorActivity (the
# manifest names all three) and the JNI bridge classes that native code calls by
# string (scene_orientation.cpp, the GlossHook runner). Keep wholesale — it is
# only 87 classes, so there is nothing to win by being clever here.
-keep class in.aevora.ruby.** { *; }
-keep class com.openswordigo.** { *; }
-keep class com.L.SwordigoRuntime.** { *; }
-keep class com.touchfoo.** { *; }

# ── Qt for Android ──────────────────────────────────────────────────────────
# org.qtproject.qt.android.bindings.* is loaded reflectively by QtLoader
# (DexClassLoader -> loadApplication/startApplication looked up by name).
-keep class org.qtproject.** { *; }

# ── Sora editor ─────────────────────────────────────────────────────────────
# The code editor ships whole. Its language registry, event subscription
# (subscribeEvent(Class, ...)) and text-mate APIs are driven from Java, and it is
# the one dependency whose reflection use we cannot audit cheaply, so keep it
# intact rather than risk the editor loading a blank document on device.
-keep class io.github.rosemoe.sora.** { *; }
-keep class io.github.rosemoe.sora2.** { *; }

# ── Kotlin ──────────────────────────────────────────────────────────────────
# kotlin.Metadata is read reflectively; without it surviving Kotlin classes can
# misbehave at runtime.
-keep class kotlin.Metadata { *; }
-keep class kotlin.jvm.internal.** { *; }

# ── Optional dependencies ───────────────────────────────────────────────────
# kotlin-stdlib and sora-editor reference a number of optional/compile-only
# artifacts (kotlin-reflect internals, jetbrains annotations, coroutines debug
# agent, ...) that are not on our classpath. Missing them is not an error; they
# are only reachable from code paths the app does not use.
-dontwarn **
