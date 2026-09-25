#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace ruby::android {

class RubySettingsBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString userName READ userName WRITE setUserName NOTIFY userNameChanged)
    Q_PROPERTY(QString userTitle READ userTitle WRITE setUserTitle NOTIFY userTitleChanged)
    Q_PROPERTY(bool showGrid READ showGrid WRITE setShowGrid NOTIFY showGridChanged)
    Q_PROPERTY(bool msaaEnabled READ msaaEnabled WRITE setMsaaEnabled NOTIFY msaaEnabledChanged)
    Q_PROPERTY(bool wireframeMode READ wireframeMode WRITE setWireframeMode NOTIFY wireframeModeChanged)
    Q_PROPERTY(bool snapCamera READ snapCamera WRITE setSnapCamera NOTIFY snapCameraChanged)
    Q_PROPERTY(bool autoCompileProtobuf READ autoCompileProtobuf WRITE setAutoCompileProtobuf NOTIFY autoCompileProtobufChanged)
    Q_PROPERTY(bool flipUvOnExport READ flipUvOnExport WRITE setFlipUvOnExport NOTIFY flipUvOnExportChanged)
    Q_PROPERTY(bool lineNumbers READ lineNumbers WRITE setLineNumbers NOTIFY lineNumbersChanged)
    Q_PROPERTY(bool syntaxHighlighting READ syntaxHighlighting WRITE setSyntaxHighlighting NOTIFY syntaxHighlightingChanged)
    Q_PROPERTY(int editorFontSize READ editorFontSize WRITE setEditorFontSize NOTIFY editorFontSizeChanged)
    Q_PROPERTY(bool hapticsEnabled READ hapticsEnabled WRITE setHapticsEnabled NOTIFY hapticsEnabledChanged)
    // Ground-mesh generator choice for the in-scene mesh editor. Persisted, and
    // pushed into boulderx straight away, so the picker in Settings changes the
    // next regenerated mesh rather than the next launch. Values are the stable
    // ids "boulder" and "boulderx"; anything else reads back as "boulder".
    Q_PROPERTY(QString groundGenerator READ groundGenerator WRITE setGroundGenerator
                   NOTIFY groundGeneratorChanged)
    Q_PROPERTY(int nameDatabaseCount READ nameDatabaseCount CONSTANT)
    Q_PROPERTY(QString appName READ appName CONSTANT)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    // Provenance shown on the About card.  Kept here rather than hard-coded in
    // QML so the desktop and mobile About panels cannot drift apart.
    Q_PROPERTY(QString copyrightHolder READ copyrightHolder CONSTANT)
    Q_PROPERTY(QString licenseName READ licenseName CONSTANT)
    Q_PROPERTY(QString licenseUrl READ licenseUrl CONSTANT)
    Q_PROPERTY(QString vendorName READ vendorName CONSTANT)
    Q_PROPERTY(QVariantList customMeshPresets READ customMeshPresets NOTIFY customMeshPresetsChanged)

public:
    explicit RubySettingsBridge(QObject* parent = nullptr);

    QString userName() const;
    void setUserName(const QString& name);

    QString userTitle() const;
    void setUserTitle(const QString& title);

    bool showGrid() const;
    void setShowGrid(bool val);

    bool msaaEnabled() const;
    void setMsaaEnabled(bool val);

    bool wireframeMode() const;
    void setWireframeMode(bool val);

    bool snapCamera() const;
    void setSnapCamera(bool val);

    bool autoCompileProtobuf() const;
    void setAutoCompileProtobuf(bool val);

    bool flipUvOnExport() const;
    void setFlipUvOnExport(bool val);

    bool lineNumbers() const;
    void setLineNumbers(bool val);

    bool syntaxHighlighting() const;
    void setSyntaxHighlighting(bool val);

    int editorFontSize() const;
    void setEditorFontSize(int size);

    bool hapticsEnabled() const;
    void setHapticsEnabled(bool val);

    QString groundGenerator() const;
    void setGroundGenerator(const QString& id);

    int nameDatabaseCount() const;
    QString appName() const { return QStringLiteral("Ruby Touch (Ruby Mobile)"); }
#ifdef RUBY_BUILD_VERSION
    QString appVersion() const { return QStringLiteral(RUBY_BUILD_VERSION); }
#else
    QString appVersion() const { return QStringLiteral("v1.4 (Ruby Mobile)"); }
#endif

    QString copyrightHolder() const { return QStringLiteral("MrSinup"); }
    QString licenseName() const { return QStringLiteral("GNU General Public License v3.0"); }
    QString licenseUrl() const { return QStringLiteral("https://www.gnu.org/licenses/gpl-3.0.html"); }
    QString vendorName() const { return QStringLiteral("Aevora Labs"); }

    Q_INVOKABLE QString randomizeUserName();
    Q_INVOKABLE QString getRandomName() const;
    Q_INVOKABLE QStringList getPresetNames() const;
    Q_INVOKABLE void resetToDefaults();

    QVariantList customMeshPresets() const;
    Q_INVOKABLE void saveCustomMeshPreset(const QString& name, const QString& topTex, const QString& groundTex);
    Q_INVOKABLE void deleteCustomMeshPreset(int index);

signals:
    void userNameChanged(const QString& name);
    void userTitleChanged(const QString& title);
    void showGridChanged(bool val);
    void msaaEnabledChanged(bool val);
    void wireframeModeChanged(bool val);
    void snapCameraChanged(bool val);
    void autoCompileProtobufChanged(bool val);
    void flipUvOnExportChanged(bool val);
    void lineNumbersChanged(bool val);
    void syntaxHighlightingChanged(bool val);
    void editorFontSizeChanged(int size);
    void hapticsEnabledChanged(bool val);
    void groundGeneratorChanged(const QString& id);
    void customMeshPresetsChanged();
};

} // namespace ruby::android
