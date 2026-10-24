#include "scene_orientation.h"
#include "tools/filerift.h"

#include <QGuiApplication>
#include <QScreen>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <fstream>
#include <cstdio>

#if defined(Q_OS_ANDROID)
#  include <QCoreApplication>
#  include <QJniObject>
#  include <QtCore/qnativeinterface.h>
#  include <jni.h>
#endif

namespace ruby::android {

SceneOrientation* SceneOrientation::s_instance = nullptr;

namespace {

// android.content.pm.ActivityInfo orientation constants. Spelled out as plain
// ints so this translation unit does not need the Android SDK headers; the
// values are part of the stable public Android API.
constexpr int kOrientationUnspecified      = -1; // SCREEN_ORIENTATION_UNSPECIFIED
constexpr int kOrientationLandscape        =  0; // SCREEN_ORIENTATION_LANDSCAPE
constexpr int kOrientationPortrait         =  1; // SCREEN_ORIENTATION_PORTRAIT
constexpr int kOrientationSensorLandscape  =  6; // SCREEN_ORIENTATION_SENSOR_LANDSCAPE

} // namespace

SceneOrientation::SceneOrientation(QObject* parent)
    : QObject(parent)
{
    s_instance = this;
}

SceneOrientation::~SceneOrientation()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

bool SceneOrientation::isSupported() const
{
#if defined(Q_OS_ANDROID)
    return true;
#else
    return false;
#endif
}

bool SceneOrientation::isLandscape() const
{
    if (m_landscapeLocked) return true;
    if (auto* screen = QGuiApplication::primaryScreen()) {
        const auto o = screen->orientation();
        return o == Qt::LandscapeOrientation || o == Qt::InvertedLandscapeOrientation;
    }
    return false;
}

void SceneOrientation::updateInsets(int top, int bottom, int left, int right)
{
    qreal dpr = 1.0;
    if (auto* screen = QGuiApplication::primaryScreen()) {
        dpr = screen->devicePixelRatio();
        if (dpr < 0.1) dpr = 1.0;
    }

    m_safeInsetTop = top / dpr;
    m_safeInsetBottom = bottom / dpr;
    m_safeInsetLeft = left / dpr;
    m_safeInsetRight = right / dpr;

    emit insetsChanged();
}

void SceneOrientation::vibrateTouch(int durationMs)
{
#if defined(Q_OS_ANDROID)
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        activity.callMethod<void>("vibrateTouch", "(I)V", jint(durationMs));
    }
#else
    Q_UNUSED(durationMs);
#endif
}

bool SceneOrientation::hasAllFilesAccess()
{
#if defined(Q_OS_ANDROID)
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        return activity.callMethod<jboolean>("hasAllFilesAccess");
    }
#endif
    return true;
}

void SceneOrientation::requestAllFilesAccess()
{
#if defined(Q_OS_ANDROID)
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        activity.callMethod<void>("checkAndRequestAllFilesAccess");
    }
#endif
}

void SceneOrientation::moveToBack()
{
#if defined(Q_OS_ANDROID)
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        activity.callMethod<void>("moveToBack");
    }
#endif
}

bool SceneOrientation::applyPolicy(int androidOrientation, const QString& name)
{
#if defined(Q_OS_ANDROID)
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid()) {
        qWarning() << "SceneOrientation: no Android activity context; "
                      "cannot set requested orientation to" << androidOrientation;
        return false;
    }

    // Call RubyActivity helper methods which run on the Android UI looper
    if (androidOrientation == kOrientationLandscape) {
        activity.callMethod<void>("forceLandscape");
    } else if (androidOrientation == kOrientationSensorLandscape) {
        activity.callMethod<void>("setOrientationLandscape");
    } else if (androidOrientation == kOrientationPortrait) {
        activity.callMethod<void>("setOrientationPortrait");
    } else {
        activity.callMethod<void>("setOrientationAuto");
    }
#else
    Q_UNUSED(androidOrientation);
    qInfo() << "SceneOrientation: not on Android — orientation policy" << name
            << "recorded but not applied (the QML portrait guard still holds).";
#endif

    if (m_policyName != name) {
        m_policyName = name;
        emit orientationPolicyChanged();
    }
    return true;
}

bool SceneOrientation::lockLandscape()
{
    m_landscapeLocked = true;
    const bool ok = applyPolicy(kOrientationLandscape, QStringLiteral("landscape"));
    emit orientationPolicyChanged();
    emit insetsChanged();
    return ok;
}

bool SceneOrientation::lockSensorLandscape()
{
    m_landscapeLocked = true;
    const bool ok = applyPolicy(kOrientationSensorLandscape, QStringLiteral("sensor-landscape"));
    emit orientationPolicyChanged();
    emit insetsChanged();
    return ok;
}

bool SceneOrientation::lockPortrait()
{
    m_landscapeLocked = false;
    const bool ok = applyPolicy(kOrientationPortrait, QStringLiteral("portrait"));
    emit orientationPolicyChanged();
    emit insetsChanged();
    return ok;
}

bool SceneOrientation::unlock()
{
    m_landscapeLocked = false;
    const bool ok = applyPolicy(kOrientationUnspecified, QStringLiteral("unspecified"));
    emit orientationPolicyChanged();
    emit insetsChanged();
    return ok;
}

void SceneOrientation::handleFileOpen(const QString& path)
{
    emit fileOpenRequested(path);
}

void SceneOrientation::pickFolder(const QString& tag)
{
#if defined(Q_OS_ANDROID)
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        QJniObject jTag = QJniObject::fromString(tag);
        activity.callMethod<void>("pickFolder", "(Ljava/lang/String;)V", jTag.object<jstring>());
    }
#else
    Q_UNUSED(tag);
#endif
}

void SceneOrientation::pickFile(const QString& tag, const QString& filter)
{
#if defined(Q_OS_ANDROID)
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        QJniObject jTag = QJniObject::fromString(tag);
        QJniObject jFilter = QJniObject::fromString(filter);
        activity.callMethod<void>("pickFile", "(Ljava/lang/String;Ljava/lang/String;)V",
                                  jTag.object<jstring>(), jFilter.object<jstring>());
    }
#else
    Q_UNUSED(tag);
    Q_UNUSED(filter);
#endif
}

void SceneOrientation::openCodeEditor(const QString& filePath)
{
#if defined(Q_OS_ANDROID)
    QJniObject jPath = QJniObject::fromString(filePath);
    QJniObject::callStaticMethod<void>(
        "in/aevora/ruby/RubyActivity",
        "openCodeEditor",
        "(Ljava/lang/String;)V",
        jPath.object<jstring>()
    );
#else
    Q_UNUSED(filePath);
#endif
}

} // namespace ruby::android

#if defined(Q_OS_ANDROID)
extern "C" {

JNIEXPORT void JNICALL
Java_in_aevora_ruby_RubyActivity_nativeUpdateWindowInsets(
    JNIEnv* env, jclass clazz, jint top, jint bottom, jint left, jint right)
{
    Q_UNUSED(env);
    Q_UNUSED(clazz);
    if (auto* inst = ruby::android::SceneOrientation::instance()) {
        inst->updateInsets(top, bottom, left, right);
    }
}

JNIEXPORT void JNICALL
Java_in_aevora_ruby_RubyActivity_nativeOnFileOpened(
    JNIEnv* env, jclass clazz, jstring path)
{
    Q_UNUSED(clazz);
    if (!path) return;
    const char* str = env->GetStringUTFChars(path, nullptr);
    QString qpath = QString::fromUtf8(str);
    env->ReleaseStringUTFChars(path, str);

    QMetaObject::invokeMethod(qApp, [qpath]() {
        if (auto* inst = ruby::android::SceneOrientation::instance()) {
            inst->handleFileOpen(qpath);
        }
    }, Qt::QueuedConnection);
}

JNIEXPORT void JNICALL
Java_in_aevora_ruby_RubyActivity_nativeOnPickerResult(
    JNIEnv* env, jclass clazz, jstring tag, jstring path, jboolean isFolder)
{
    Q_UNUSED(clazz);
    if (!tag || !path) return;
    const char* strTag = env->GetStringUTFChars(tag, nullptr);
    const char* strPath = env->GetStringUTFChars(path, nullptr);
    QString qTag = QString::fromUtf8(strTag);
    QString qPath = QString::fromUtf8(strPath);
    env->ReleaseStringUTFChars(tag, strTag);
    env->ReleaseStringUTFChars(path, strPath);

    QMetaObject::invokeMethod(qApp, [qTag, qPath, isFolder]() {
        if (auto* inst = ruby::android::SceneOrientation::instance()) {
            if (isFolder) {
                emit inst->folderPicked(qTag, qPath);
            } else {
                emit inst->filePicked(qTag, qPath);
            }
        }
    }, Qt::QueuedConnection);
}

namespace {

// A code editor has no business opening a 200 MB asset: reading it, expanding
// it into a QString and then into Sora's buffer multiplies the footprint.
constexpr qint64 kMaxEditorFileBytes = 64ll * 1024 * 1024;

#if defined(__ANDROID__)
#  include <android/log.h>
#  define RUBY_SAVE_LOG(...) __android_log_print(ANDROID_LOG_INFO, "RubySave", __VA_ARGS__)
#else
#  define RUBY_SAVE_LOG(...) do {} while (0)
#endif

// Surface a failure to Java as an exception, which CodeEditorActivity already
// catches and displays in its "Load Error" dialog. Returning nullptr instead
// would only show "Unknown error".
jstring throw_editor_error(JNIEnv* env, const std::string& message) {
    if (jclass ex = env->FindClass("java/lang/IllegalStateException")) {
        env->ThrowNew(ex, message.c_str());
    }
    return nullptr;
}

} // namespace

JNIEXPORT jstring JNICALL
Java_in_aevora_ruby_CodeEditorActivity_nativeLoadFile(
    JNIEnv* env, jclass clazz, jstring jpath, jbooleanArray outIsFilerift, jobjectArray outFileType)
{
    Q_UNUSED(clazz);
    if (!jpath) return nullptr;

    const char* path_chars = env->GetStringUTFChars(jpath, nullptr);
    std::string file_path = path_chars ? path_chars : "";
    if (path_chars) env->ReleaseStringUTFChars(jpath, path_chars);

    if (file_path.empty()) return nullptr;

    QFile file(QString::fromStdString(file_path));
    if (!file.open(QIODevice::ReadOnly)) {
        return nullptr;
    }
    if (file.size() > kMaxEditorFileBytes) {
        file.close();
        return throw_editor_error(env, "File is larger than the " +
            std::to_string(kMaxEditorFileBytes / (1024 * 1024)) +
            " MB editor limit: " + file_path);
    }

    const QByteArray raw = file.readAll();
    file.close();
    if (raw.isEmpty()) {
        if (outIsFilerift && env->GetArrayLength(outIsFilerift) > 0) {
            jboolean false_val = JNI_FALSE;
            env->SetBooleanArrayRegion(outIsFilerift, 0, 1, &false_val);
        }
        return env->NewStringUTF("");
    }

    std::string bytes(raw.constData(), static_cast<size_t>(raw.size()));
    std::string detected_type = filerift::detect_filetype(file_path, bytes);
    const bool is_supported = filerift::is_supported_filetype(detected_type);

    // Sniff text vs binary by content, not by a short list of prefixes. A
    // decoded asset that starts with a preserved '# comment' used to look
    // binary, and real protobuf that happened to begin with "Object{" looked
    // like markup.
    const bool is_already_text = filerift::looks_like_markup_text(bytes);

    std::string result_text;
    bool is_filerift = false;

    if (is_supported) {
        if (is_already_text) {
            result_text = std::move(bytes);
            is_filerift = true;
        } else {
            // Binary protobuf -> decode via FileRift. Without a decoded view
            // there is nothing safe to show: handing the raw protobuf to the
            // text editor used to replace every NUL with U+FFFD, and the next
            // save wrote that mojibake back over the asset.
            std::string decode_error;
            std::string decoded = filerift::decode_protobuf(bytes, detected_type, &decode_error);
            if (decoded.empty()) {
                return throw_editor_error(env,
                    "Cannot decode " + QFileInfo(QString::fromStdString(file_path)).fileName().toStdString() +
                    " as " + detected_type + ": " +
                    (decode_error.empty() ? std::string("unknown decode error") : decode_error));
            }
            result_text = std::move(decoded);
            is_filerift = true;
        }
    } else if (!is_already_text) {
        // A binary asset the editor cannot represent as text.
        return throw_editor_error(env,
            "Not a text file and not a supported Swordigo format (" +
            (detected_type.empty() ? std::string("unknown extension") : detected_type) +
            "). Open it in the asset viewer instead.");
    } else {
        result_text = std::move(bytes);
        is_filerift = false;
    }

    if (outIsFilerift && env->GetArrayLength(outIsFilerift) > 0) {
        jboolean jb = is_filerift ? JNI_TRUE : JNI_FALSE;
        env->SetBooleanArrayRegion(outIsFilerift, 0, 1, &jb);
    }
    if (outFileType && env->GetArrayLength(outFileType) > 0) {
        jstring jtype = env->NewStringUTF(detected_type.c_str());
        env->SetObjectArrayElement(outFileType, 0, jtype);
        env->DeleteLocalRef(jtype);
    }

    // Convert result_text into a Java String safely via QString UTF-16
    QString qstr = QString::fromUtf8(result_text.data(), static_cast<int>(result_text.size()));
    return env->NewString(reinterpret_cast<const jchar*>(qstr.utf16()), static_cast<jsize>(qstr.length()));
}

JNIEXPORT jstring JNICALL
Java_in_aevora_ruby_CodeEditorActivity_nativeSaveFile(
    JNIEnv* env, jclass clazz, jstring jpath, jstring jcontent, jboolean transcodeFilerift)
{
    Q_UNUSED(clazz);
    if (!jpath || !jcontent) {
        return env->NewStringUTF("File path or content is null");
    }

    const char* path_chars = env->GetStringUTFChars(jpath, nullptr);
    std::string file_path = path_chars ? path_chars : "";
    if (path_chars) env->ReleaseStringUTFChars(jpath, path_chars);

    if (file_path.empty()) {
        return env->NewStringUTF("Target file path is empty");
    }

    // Extract content safely via JNI UTF-16 with zero reflection
    std::string content;
    const jchar* u16 = env->GetStringChars(jcontent, nullptr);
    jsize u16_len = env->GetStringLength(jcontent);
    if (u16 && u16_len > 0) {
        QString qcontent = QString::fromUtf16(reinterpret_cast<const char16_t*>(u16), u16_len);
        QByteArray utf8 = qcontent.toUtf8();
        content.assign(utf8.constData(), utf8.size());
    }
    if (u16) env->ReleaseStringChars(jcontent, u16);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    std::string detected_type = filerift::detect_filetype(file_path, content);
    if (detected_type.empty()) {
        size_t dot = file_path.find_last_of('.');
        if (dot != std::string::npos) {
            detected_type = filerift::normalize_filetype(file_path.substr(dot + 1));
        }
    }

    const bool supported = filerift::is_supported_filetype(detected_type);
    std::string bytes_to_write;
    RUBY_SAVE_LOG("nativeSaveFile: path=%s transcode=%d supported=%d content=%zu",
                  file_path.c_str(), (int)transcodeFilerift, (int)supported, content.size());

    if (transcodeFilerift && supported) {
        // `content` is Sora's text buffer. If it is not text at all, the
        // editor was showing something it could not represent; encoding it
        // would produce a file worse than the original.
        if (!filerift::looks_like_markup_text(content)) {
            return env->NewStringUTF("Editor buffer is not text; refusing to encode it over a binary asset");
        }
        try {
            bytes_to_write = filerift::recode_markup(content, detected_type);
            RUBY_SAVE_LOG("nativeSaveFile: recode -> %zu bytes", bytes_to_write.size());
            if (bytes_to_write.empty()) {
                return env->NewStringUTF("FileRift compilation produced empty output");
            }
        } catch (const std::exception& e) {
            RUBY_SAVE_LOG("nativeSaveFile: recode threw: %s", e.what());
            return env->NewStringUTF(e.what());
        } catch (...) {
            return env->NewStringUTF("Unknown FileRift encoding error");
        }
    } else {
        // Raw-text mode. Writing UTF-8 over a binary protobuf asset leaves a
        // file neither the engine nor the 3D view can load, so check what is
        // actually on disk before replacing it.
        if (supported) {
            QFile existing(QString::fromStdString(file_path));
            bool binary_on_disk = false;
            if (existing.open(QIODevice::ReadOnly)) {
                const QByteArray head = existing.read(4096);
                binary_on_disk = !filerift::looks_like_markup_text(
                    std::string(head.constData(), static_cast<size_t>(head.size())));
            }
            if (binary_on_disk) {
                return env->NewStringUTF(("Refusing to overwrite binary " + detected_type +
                    " asset with plain text: reopen the file so it is decoded first").c_str());
            }
        }
        bytes_to_write = std::move(content);
    }

    // QSaveFile writes a uniquely named sibling, flushes it and renames it over
    // the target: no predictable ".tmp_ruby_save" name an attacker can
    // pre-create as a symlink, and no truncate-then-fail window that leaves the
    // original asset empty.
    QSaveFile out(QString::fromStdString(file_path));
    RUBY_SAVE_LOG("nativeSaveFile: opening QSaveFile");
    if (!out.open(QIODevice::WriteOnly)) {
        RUBY_SAVE_LOG("nativeSaveFile: QSaveFile open FAILED: %s", out.errorString().toStdString().c_str());
        return env->NewStringUTF(("Failed to open target file for writing: " + file_path).c_str());
    }
    if (out.write(bytes_to_write.data(), static_cast<qint64>(bytes_to_write.size())) !=
            static_cast<qint64>(bytes_to_write.size()) || !out.commit()) {
        out.cancelWriting();
        RUBY_SAVE_LOG("nativeSaveFile: write/commit FAILED: %s", out.errorString().toStdString().c_str());
        return env->NewStringUTF(("Failed to write data to " + file_path + ": " +
                                  out.errorString().toStdString()).c_str());
    }

    RUBY_SAVE_LOG("nativeSaveFile: saved OK");
    return nullptr;
}

} // extern "C"
#endif

