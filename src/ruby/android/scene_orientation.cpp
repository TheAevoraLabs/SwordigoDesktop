#include "scene_orientation.h"
#include "tools/filerift.h"

#include <QGuiApplication>
#include <QScreen>
#include <QDebug>
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
    const bool ok = applyPolicy(kOrientationLandscape, QStringLiteral("landscape"));
    m_landscapeLocked = true;
    return ok;
}

bool SceneOrientation::lockSensorLandscape()
{
    const bool ok = applyPolicy(kOrientationSensorLandscape, QStringLiteral("sensor-landscape"));
    m_landscapeLocked = true;
    return ok;
}

bool SceneOrientation::lockPortrait()
{
    const bool ok = applyPolicy(kOrientationPortrait, QStringLiteral("portrait"));
    m_landscapeLocked = false;
    return ok;
}

bool SceneOrientation::unlock()
{
    const bool ok = applyPolicy(kOrientationUnspecified, QStringLiteral("unspecified"));
    m_landscapeLocked = false;
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

    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return nullptr;
    }
    const auto size = file.tellg();
    if (size <= 0) {
        file.close();
        if (outIsFilerift && env->GetArrayLength(outIsFilerift) > 0) {
            jboolean false_val = JNI_FALSE;
            env->SetBooleanArrayRegion(outIsFilerift, 0, 1, &false_val);
        }
        return env->NewStringUTF("");
    }

    std::string bytes(static_cast<size_t>(size), '\0');
    file.seekg(0, std::ios::beg);
    file.read(&bytes[0], size);
    file.close();

    std::string detected_type = filerift::detect_filetype(file_path, bytes);
    bool is_supported = filerift::is_supported_filetype(detected_type);

    bool is_already_text = false;
    if (bytes.size() >= 4) {
        if (bytes.rfind("## FileRift", 0) == 0 ||
            bytes.rfind("syntax =", 0) == 0 ||
            bytes.rfind("scene ", 0) == 0 ||
            bytes.rfind("scl ", 0) == 0 ||
            bytes.rfind("Object{", 0) == 0) {
            is_already_text = true;
        }
    }

    std::string result_text;
    bool is_filerift = false;

    if (is_supported) {
        if (is_already_text) {
            result_text = std::move(bytes);
            is_filerift = true;
        } else {
            // Binary protobuf -> decode via FileRift
            try {
                std::string decoded = filerift::decode_protobuf(bytes, detected_type);
                if (!decoded.empty()) {
                    result_text = std::move(decoded);
                    is_filerift = true;
                } else {
                    result_text = std::move(bytes);
                    is_filerift = false;
                }
            } catch (const std::exception& e) {
                result_text = std::move(bytes);
                is_filerift = false;
            }
        }
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

    // Convert result_text into a Java String safely using UTF-8 constructor
    jbyteArray jbytes = env->NewByteArray(result_text.size());
    if (!jbytes) return nullptr;
    env->SetByteArrayRegion(jbytes, 0, result_text.size(), reinterpret_cast<const jbyte*>(result_text.data()));
    jclass strClass = env->FindClass("java/lang/String");
    jstring utf8Charset = env->NewStringUTF("UTF-8");
    jmethodID strCtor = env->GetMethodID(strClass, "<init>", "([BLjava/lang/String;)V");
    jstring resStr = static_cast<jstring>(env->NewObject(strClass, strCtor, jbytes, utf8Charset));
    env->DeleteLocalRef(jbytes);
    env->DeleteLocalRef(utf8Charset);
    env->DeleteLocalRef(strClass);
    return resStr;
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

    // Extract content bytes as UTF-8
    jclass strClass = env->GetObjectClass(jcontent);
    jmethodID getBytesMethod = env->GetMethodID(strClass, "getBytes", "(Ljava/lang/String;)[B");
    jstring utf8Charset = env->NewStringUTF("UTF-8");
    jbyteArray jbytes = static_cast<jbyteArray>(env->CallObjectMethod(jcontent, getBytesMethod, utf8Charset));
    env->DeleteLocalRef(utf8Charset);
    env->DeleteLocalRef(strClass);

    std::string content;
    if (jbytes) {
        jsize len = env->GetArrayLength(jbytes);
        content.resize(len);
        env->GetByteArrayRegion(jbytes, 0, len, reinterpret_cast<jbyte*>(&content[0]));
        env->DeleteLocalRef(jbytes);
    }

    std::string detected_type = filerift::detect_filetype(file_path, content);
    if (detected_type.empty()) {
        size_t dot = file_path.find_last_of('.');
        if (dot != std::string::npos) {
            detected_type = filerift::normalize_filetype(file_path.substr(dot + 1));
        }
    }

    std::string bytes_to_write;

    if (transcodeFilerift && filerift::is_supported_filetype(detected_type)) {
        try {
            bytes_to_write = filerift::recode_markup(content, detected_type);
            if (bytes_to_write.empty()) {
                return env->NewStringUTF("FileRift compilation produced empty output");
            }
        } catch (const std::exception& e) {
            return env->NewStringUTF(e.what());
        } catch (...) {
            return env->NewStringUTF("Unknown FileRift encoding error");
        }
    } else {
        bytes_to_write = std::move(content);
    }

    // Atomic write to avoid data loss / corruption: write to temp file then rename
    std::string temp_path = file_path + ".tmp_ruby_save";
    std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return env->NewStringUTF(("Failed to create temporary file: " + temp_path).c_str());
    }
    out.write(bytes_to_write.data(), bytes_to_write.size());
    out.flush();
    if (!out.good()) {
        out.close();
        std::remove(temp_path.c_str());
        return env->NewStringUTF("Failed to write data to disk");
    }
    out.close();

    // Rename temp file to target path
    if (std::rename(temp_path.c_str(), file_path.c_str()) != 0) {
        std::ifstream src(temp_path, std::ios::binary);
        std::ofstream dst(file_path, std::ios::binary | std::ios::trunc);
        if (src.is_open() && dst.is_open()) {
            dst << src.rdbuf();
            dst.flush();
            src.close();
            dst.close();
            std::remove(temp_path.c_str());
        } else {
            std::remove(temp_path.c_str());
            return env->NewStringUTF("Failed to replace target file with saved data");
        }
    }

    return nullptr;
}

} // extern "C"
#endif

