#pragma once
// ============================================================================
// scene_orientation.h — runtime screen-orientation control.
//
// WHY
//   The scene editor is a landscape-only studio: two NavPads, a gizmo pad, a
//   full-height outliner and a full-height inspector simply do not fit a phone
//   in portrait. The activity is declared portrait in AndroidManifest.xml
//   (correct for the rest of the app), so the editor has to ask for landscape
//   at runtime and hand the policy back on the way out.
//
//   Activity.setRequestedOrientation() overrides the manifest value for the
//   lifetime of the activity instance, and the manifest already declares
//   configChanges="orientation|screenSize|...", so the rotation happens without
//   an activity restart.
//
// PORTABILITY
//   The Android branch is compiled only under Q_OS_ANDROID. Everywhere else
//   (desktop dev builds, and the qmllint/QML preview workflows) these methods
//   are honest no-ops that report failure, and the QML layer falls back to its
//   own portrait guard so the landscape-only UX still holds.
// ============================================================================

#include <QObject>
#include <QString>

namespace ruby::android {

class SceneOrientation : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool landscapeLocked READ landscapeLocked NOTIFY orientationPolicyChanged)
    Q_PROPERTY(bool supported READ isSupported CONSTANT)

    // Android Context Safe Insets (values in logical dp)
    Q_PROPERTY(qreal safeInsetTop READ safeInsetTop NOTIFY insetsChanged)
    Q_PROPERTY(qreal safeInsetBottom READ safeInsetBottom NOTIFY insetsChanged)
    Q_PROPERTY(qreal safeInsetLeft READ safeInsetLeft NOTIFY insetsChanged)
    Q_PROPERTY(qreal safeInsetRight READ safeInsetRight NOTIFY insetsChanged)
    Q_PROPERTY(qreal statusBarHeight READ statusBarHeight NOTIFY insetsChanged)
    Q_PROPERTY(qreal navBarHeight READ navBarHeight NOTIFY insetsChanged)
    Q_PROPERTY(bool isLandscape READ isLandscape NOTIFY insetsChanged)

public:
    explicit SceneOrientation(QObject* parent = nullptr);
    ~SceneOrientation() override;

    static SceneOrientation* instance() { return s_instance; }

    bool landscapeLocked() const { return m_landscapeLocked; }
    bool isLandscape() const;

    qreal safeInsetTop() const { return m_safeInsetTop; }
    qreal safeInsetBottom() const { return m_safeInsetBottom; }
    qreal safeInsetLeft() const { return m_safeInsetLeft; }
    qreal safeInsetRight() const { return m_safeInsetRight; }
    qreal statusBarHeight() const { return m_safeInsetTop; }
    qreal navBarHeight() const { return m_safeInsetBottom; }

    /// Hard-forced landscape (ignores the user's rotation lock). This is the
    /// policy the scene editor wants.
    Q_INVOKABLE bool lockLandscape();
    Q_INVOKABLE bool forceLandscape() { return lockLandscape(); }

    /// Landscape, but the sensor chooses which of the two landscape
    /// orientations — better ergonomics when the user flips the device.
    Q_INVOKABLE bool lockSensorLandscape();

    /// Restore the app's normal portrait policy.
    Q_INVOKABLE bool lockPortrait();

    /// Hand orientation back to the system default.
    Q_INVOKABLE bool unlock();

    /// Query the requested orientation name ("landscape" / "portrait" /
    /// "sensor-landscape" / "unspecified"). Diagnostic + QML display.
    Q_INVOKABLE QString policyName() const { return m_policyName; }

    /// True only where the platform can actually change orientation at
    /// runtime (i.e. Android).
    bool isSupported() const;

    /// Device haptic feedback
    Q_INVOKABLE void vibrateTouch(int durationMs = 25);

    /// All-files access queries
    Q_INVOKABLE bool hasAllFilesAccess();
    Q_INVOKABLE void requestAllFilesAccess();

    /// Gracefully move task to back (minimize without killing)
    Q_INVOKABLE void moveToBack();

    /// Android Storage Access Framework folder & file pickers
    Q_INVOKABLE void pickFolder(const QString& tag);
    Q_INVOKABLE void pickFile(const QString& tag, const QString& filter = QStringLiteral("*/*"));

    /// Open standalone native code editor activity
    Q_INVOKABLE void openCodeEditor(const QString& filePath);

    void updateInsets(int top, int bottom, int left, int right);
    void handleFileOpen(const QString& path);

signals:
    void orientationPolicyChanged();
    void insetsChanged();
    void fileOpenRequested(const QString& path);
    void folderPicked(const QString& tag, const QString& path);
    void filePicked(const QString& tag, const QString& path);

private:
    bool applyPolicy(int androidOrientation, const QString& name);

    static SceneOrientation* s_instance;

    bool m_landscapeLocked = false;
    QString m_policyName = QStringLiteral("unspecified");

    qreal m_safeInsetTop = 0.0;
    qreal m_safeInsetBottom = 0.0;
    qreal m_safeInsetLeft = 0.0;
    qreal m_safeInsetRight = 0.0;
};

} // namespace ruby::android
