#pragma once
// ============================================================================
// ruby_project.h — Active Project State & Model for Ruby GG & Ruby Touch
// ============================================================================

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QDateTime>

namespace ruby::core {

class RubyProject : public QObject {
    Q_OBJECT

    // Active project state
    Q_PROPERTY(bool hasActiveProject READ hasActiveProject NOTIFY activeProjectChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY activeProjectChanged)
    Q_PROPERTY(QString projectId READ projectId NOTIFY activeProjectChanged)
    Q_PROPERTY(QString projectAuthor READ projectAuthor NOTIFY activeProjectChanged)
    Q_PROPERTY(QString projectVersion READ projectVersion NOTIFY activeProjectChanged)
    Q_PROPERTY(QString projectDescription READ projectDescription NOTIFY activeProjectChanged)
    Q_PROPERTY(QString projectRoot READ projectRoot NOTIFY activeProjectChanged)
    Q_PROPERTY(QString projectAssetsDir READ projectAssetsDir NOTIFY activeProjectChanged)
    Q_PROPERTY(QString rbpFilePath READ rbpFilePath NOTIFY activeProjectChanged)
    Q_PROPERTY(QString propertiesTomlPath READ propertiesTomlPath NOTIFY activeProjectChanged)
    Q_PROPERTY(QString iconPath READ iconPath NOTIFY activeProjectChanged)
    Q_PROPERTY(QStringList targetAbis READ targetAbis NOTIFY activeProjectChanged)
    Q_PROPERTY(QVariantList installedPackages READ installedPackages NOTIFY activeProjectChanged)

    // Diff & Scan state
    Q_PROPERTY(bool isScanning READ isScanning NOTIFY scanStateChanged)
    Q_PROPERTY(int scanProgress READ scanProgress NOTIFY scanProgressChanged)
    Q_PROPERTY(int modifiedFileCount READ modifiedFileCount NOTIFY diffStatsChanged)
    Q_PROPERTY(int newFileCount READ newFileCount NOTIFY diffStatsChanged)
    Q_PROPERTY(QString lastScanTimeStr READ lastScanTimeStr NOTIFY diffStatsChanged)

    // Project creation & seeding state
    Q_PROPERTY(bool isCreating READ isCreating NOTIFY isCreatingChanged)
    Q_PROPERTY(int creationProgress READ creationProgress NOTIFY creationProgressChanged)
    Q_PROPERTY(QString creationStatus READ creationStatus NOTIFY creationStatusChanged)

public:
    static RubyProject& instance();

    explicit RubyProject(QObject* parent = nullptr);
    ~RubyProject() override;

    // Getters
    bool hasActiveProject() const { return m_active; }
    QString projectName() const { return m_name; }
    QString projectId() const { return m_id; }
    QString projectAuthor() const { return m_author; }
    QString projectVersion() const { return m_version; }
    QString projectDescription() const { return m_description; }
    QString projectRoot() const { return m_root; }
    QString projectAssetsDir() const;
    QString rbpFilePath() const { return m_rbpPath; }
    QString propertiesTomlPath() const;
    QString iconPath() const;
    QStringList targetAbis() const { return m_targetAbis; }
    QVariantList installedPackages() const { return m_installedPackages; }

    bool isScanning() const { return m_isScanning; }
    int scanProgress() const { return m_scanProgress; }
    int modifiedFileCount() const { return m_modifiedCount; }
    int newFileCount() const { return m_newCount; }
    QString lastScanTimeStr() const { return m_lastScanTimeStr; }

    bool isCreating() const { return m_isCreating; }
    int creationProgress() const { return m_creationProgress; }
    QString creationStatus() const { return m_creationStatus; }

    // Check if path is root of a project
    Q_INVOKABLE static bool isProjectDirectory(const QString& dirPath);
    Q_INVOKABLE static QString findRbpInDirectory(const QString& dirPath);

    // Lifecycle
    Q_INVOKABLE bool createProject(const QString& name,
                                  const QString& author,
                                  const QString& version,
                                  const QString& id,
                                  const QStringList& abis,
                                  const QString& targetParentDir);

    Q_INVOKABLE bool openProject(const QString& rbpPathOrDir);
    Q_INVOKABLE void closeProject();
    Q_INVOKABLE bool updateMetadata(const QString& name,
                                    const QString& author,
                                    const QString& version,
                                    const QString& description,
                                    const QString& id = QString());

    // Project Actions
    Q_INVOKABLE void refreshDiffContext();
    Q_INVOKABLE bool exportToLauncher();

    // Two-Tier Safety System
    Q_INVOKABLE bool revertSingleFileToVanilla(const QString& relPath);
    Q_INVOKABLE bool revertProjectToVanilla(const QString& confirmationText);
    Q_INVOKABLE bool deleteProject(const QString& confirmationText);

    // Logging helper
    void logActivity(const QString& message);
    void setCreationProgress(int percent, const QString& status);
    bool seedProjectAssets(const QString& projectPath, const QStringList& abis);
    void saveDefaultIcon(const QString& projectPath);

signals:
    void activeProjectChanged();
    void scanStateChanged(bool scanning);
    void scanProgressChanged(int progress);
    void diffStatsChanged();
    void isCreatingChanged();
    void creationProgressChanged();
    void creationStatusChanged();
    void projectExportCompleted(bool success, const QString& outputPath, const QString& message);
    void toastRequested(const QString& message, bool isError);

private:
    void resetState();
    bool saveRbpFile();
    bool savePropertiesToml();
    bool loadFromRbp(const QString& rbpPath);
    bool loadFromTomlFallback(const QString& tomlPath);

    bool m_active = false;
    QString m_name;
    QString m_id;
    QString m_author;
    QString m_version;
    QString m_description;
    QString m_root;
    QString m_rbpPath;
    QStringList m_targetAbis;
    QVariantList m_installedPackages;

    bool m_isScanning = false;
    int m_scanProgress = 0;
    int m_modifiedCount = 0;
    int m_newCount = 0;
    QString m_lastScanTimeStr = "Never";

    bool m_isCreating = false;
    int m_creationProgress = 0;
    QString m_creationStatus;
};

} // namespace ruby::core
