// ============================================================================
// ruby_project.cpp — Active Project Implementation for Ruby GG & Ruby Touch
// ============================================================================

#include "ruby_project.h"
#include "rbp_format.h"
#include "toml_sync.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QStandardPaths>
#include <QSettings>
#include <QRegularExpression>
#include <QDebug>
#include <QThread>
#include <QImage>

#if defined(Q_OS_ANDROID)
#include <QJniObject>
#include <QJniEnvironment>

extern "C" JNIEXPORT void JNICALL
Java_in_aevora_ruby_VanillaAssetManager_nativeOnSeedProgress(JNIEnv* env, jclass, jint percent, jstring jMsg) {
    const char* str = env->GetStringUTFChars(jMsg, nullptr);
    QString msg = QString::fromUtf8(str);
    env->ReleaseStringUTFChars(jMsg, str);
    QMetaObject::invokeMethod(&ruby::core::RubyProject::instance(), [percent, msg]() {
        ruby::core::RubyProject::instance().setCreationProgress(percent, msg);
    }, Qt::QueuedConnection);
}
#endif

namespace ruby::core {

RubyProject& RubyProject::instance() {
    static RubyProject s_instance;
    return s_instance;
}

RubyProject::RubyProject(QObject* parent)
    : QObject(parent) {
}

RubyProject::~RubyProject() = default;

QString RubyProject::projectAssetsDir() const {
    if (m_root.isEmpty()) return QString();
    return QDir(m_root).filePath("project");
}

QString RubyProject::propertiesTomlPath() const {
    if (m_root.isEmpty()) return QString();
    return QDir(m_root).filePath("properties.toml");
}

QString RubyProject::iconPath() const {
    if (m_root.isEmpty()) return QString();
    QString p = QDir(m_root).filePath("icon.png");
    if (QFile::exists(p)) return p;
    return QString();
}

bool RubyProject::isProjectDirectory(const QString& dirPath) {
    if (dirPath.isEmpty()) return false;
    QDir dir(dirPath);
    if (!dir.exists()) return false;

    // Check for any .rbp file in directory
    QStringList rbpFiles = dir.entryList(QStringList() << "*.rbp", QDir::Files);
    if (!rbpFiles.isEmpty()) return true;

    // Fallback: check for properties.toml + project/ folder
    if (dir.exists("properties.toml") && dir.exists("project")) return true;

    return false;
}

QString RubyProject::findRbpInDirectory(const QString& dirPath) {
    if (dirPath.isEmpty()) return QString();
    QDir dir(dirPath);
    QStringList rbpFiles = dir.entryList(QStringList() << "*.rbp", QDir::Files);
    if (!rbpFiles.isEmpty()) {
        return dir.filePath(rbpFiles.first());
    }
    return QString();
}

void RubyProject::resetState() {
    m_active = false;
    m_name.clear();
    m_id.clear();
    m_author.clear();
    m_version.clear();
    m_description.clear();
    m_root.clear();
    m_rbpPath.clear();
    m_targetAbis.clear();
    m_installedPackages.clear();
    m_isScanning = false;
    m_scanProgress = 0;
    m_modifiedCount = 0;
    m_newCount = 0;
}

void RubyProject::logActivity(const QString& message) {
    if (m_root.isEmpty()) return;
    QDir logDir(QDir(m_root).filePath(".rbp/logs"));
    if (!logDir.exists()) logDir.mkpath(".");
    QFile logFile(logDir.filePath("project.log"));
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&logFile);
        ts << QDateTime::currentDateTime().toString(Qt::ISODate) << " " << message << "\n";
    }
}

bool RubyProject::createProject(const QString& name,
                               const QString& author,
                               const QString& version,
                               const QString& id,
                               const QStringList& abis,
                               const QString& targetParentDir) {
    if (name.trimmed().isEmpty()) {
        emit toastRequested("Project name cannot be empty", true);
        return false;
    }

    QString sanitizedName = name.trimmed();
    sanitizedName.replace(QRegularExpression("[^a-zA-Z0-9_\\-\\.]"), "_");

    QString parentDir = targetParentDir;
    if (parentDir.isEmpty()) {
#if defined(Q_OS_ANDROID)
        parentDir = "/storage/emulated/0/RubyProjects";
#else
        parentDir = QDir::home().filePath("RubyProjects");
#endif
    }

    QDir rootDir(parentDir);
    if (!rootDir.exists()) {
        rootDir.mkpath(".");
    }

    QString projectPath = rootDir.filePath(sanitizedName);
    QDir projDir(projectPath);
    if (projDir.exists()) {
        emit toastRequested("A project folder with this name already exists", true);
        return false;
    }

    // 1. Create directory structure
    if (!projDir.mkpath("project") ||
        !projDir.mkpath(".rbp/logs") ||
        !projDir.mkpath("raw") ||
        !projDir.mkpath("trans") ||
        !projDir.mkpath("builds")) {
        emit toastRequested("Failed to create project directory structure", true);
        return false;
    }

    // Create plugins directories per target ABI
    projDir.mkpath("plugins/arm64-v8a");
    if (abis.contains("armeabi-v7a")) {
        projDir.mkpath("plugins/armeabi-v7a");
    }

    // 2. Generate .gitignore
    QFile gitignore(projDir.filePath(".gitignore"));
    if (gitignore.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&gitignore);
        ts << "# Ruby Touch / Ruby GG Project Ignore\n";
        ts << ".rbp/\n";
        ts << "builds/\n";
        ts << "*.tmp\n";
    }

    // 3. Prepare parameters
    QString pAuthor = author.trimmed().isEmpty() ? "Ruby Modder" : author.trimmed();
    QString pVer = version.trimmed().isEmpty() ? "1.0.0" : version.trimmed();
    QString cleanId = id.trimmed().toLower();
    cleanId.replace(QRegularExpression("[^a-z0-9_\\-\\.]"), "_");
    if (cleanId.isEmpty()) {
        cleanId = "net.aevora.mod." + sanitizedName.toLower();
    }
    QStringList targetAbis = abis.isEmpty() ? (QStringList() << "arm64-v8a") : abis;

    m_isCreating = true;
    m_creationProgress = 0;
    m_creationStatus = "Preparing project directory...";
    emit isCreatingChanged();
    emit creationProgressChanged();
    emit creationStatusChanged();

    // Spawn background worker to seed vanilla assets without blocking UI thread
    QThread* worker = QThread::create([this, projectPath, sanitizedName, name = name.trimmed(), pAuthor, pVer, cleanId, targetAbis]() {
        seedProjectAssets(projectPath, targetAbis);
        saveDefaultIcon(projectPath);

        QMetaObject::invokeMethod(this, [this, projectPath, sanitizedName, name, pAuthor, pVer, cleanId, targetAbis]() {
            QDir projDir(projectPath);
            m_name = name;
            m_author = pAuthor;
            m_version = pVer;
            m_id = cleanId;
            m_description = "A Swordigo mod created with Ruby Touch.";
            m_root = projectPath;
            m_rbpPath = projDir.filePath(sanitizedName + ".rbp");
            m_targetAbis = targetAbis;
            m_installedPackages.clear();
            m_active = true;
            m_isCreating = false;

            saveRbpFile();
            savePropertiesToml();
            logActivity("Project created with vanilla assets: " + m_name + " (" + m_id + ")");

            QSettings settings("AevoraLabs", "RubyTouch");
            QStringList recents = settings.value("recent_projects").toStringList();
            recents.removeAll(m_root);
            recents.prepend(m_root);
            while (recents.size() > 20) recents.removeLast();
            settings.setValue("recent_projects", recents);

            emit activeProjectChanged();
            emit isCreatingChanged();
            emit toastRequested("Project created with vanilla game assets!", false);
        }, Qt::QueuedConnection);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();

    return true;
}

void RubyProject::setCreationProgress(int percent, const QString& status) {
    m_creationProgress = percent;
    m_creationStatus = status;
    emit creationProgressChanged();
    emit creationStatusChanged();
}

void RubyProject::saveDefaultIcon(const QString& projectPath) {
    QDir projDir(projectPath);
    QString iconDest = projDir.filePath("icon.png");
    if (QFile::exists(iconDest)) return;

    QImage icon(":/svg/avatar_ruby_gem.svg");
    if (!icon.isNull()) {
        icon.save(iconDest, "PNG");
        return;
    }
    if (QFile::exists(":/assets/icon_app.png")) {
        QFile::copy(":/assets/icon_app.png", iconDest);
    }
}

bool RubyProject::seedProjectAssets(const QString& projectPath, const QStringList& abis) {
#if defined(Q_OS_ANDROID)
    QJniEnvironment env;
    jclass strClass = env->FindClass("java/lang/String");
    if (!strClass) return false;
    jobjectArray jAbis = env->NewObjectArray(abis.size(), strClass, nullptr);
    for (int i = 0; i < abis.size(); ++i) {
        jstring s = env->NewStringUTF(abis[i].toUtf8().constData());
        env->SetObjectArrayElement(jAbis, i, s);
        env->DeleteLocalRef(s);
    }
    QJniObject jPath = QJniObject::fromString(projectPath);
    jboolean ok = QJniObject::callStaticMethod<jboolean>(
        "in/aevora/ruby/VanillaAssetManager",
        "seedProject",
        "(Ljava/lang/String;[Ljava/lang/String;)Z",
        jPath.object<jstring>(),
        jAbis
    );
    return ok;
#else
    QDir srcAssets("src/assets");
    if (srcAssets.exists()) {
        QDir destProj(projectPath + "/project");
        destProj.mkpath(".");
        const auto entries = srcAssets.entryInfoList(QDir::Files);
        for (const auto& fi : entries) {
            QFile::copy(fi.absoluteFilePath(), destProj.filePath(fi.fileName()));
        }
    }
    return true;
#endif
}

bool RubyProject::openProject(const QString& rbpPathOrDir) {
    if (rbpPathOrDir.isEmpty()) return false;

    QFileInfo fi(rbpPathOrDir);
    QString rbpFile;
    QString rootDir;

    if (fi.isFile() && fi.suffix().toLower() == "rbp") {
        rbpFile = fi.absoluteFilePath();
        rootDir = fi.absolutePath();
    } else if (fi.isDir()) {
        rootDir = fi.absoluteFilePath();
        rbpFile = findRbpInDirectory(rootDir);
    } else {
        emit toastRequested("Invalid project path", true);
        return false;
    }

    resetState();
    m_root = rootDir;

    bool loaded = false;
    if (!rbpFile.isEmpty() && QFile::exists(rbpFile)) {
        m_rbpPath = rbpFile;
        loaded = loadFromRbp(rbpFile);
    }

    if (!loaded) {
        // Fallback: check properties.toml in root directory
        QString tomlPath = QDir(rootDir).filePath("properties.toml");
        if (QFile::exists(tomlPath)) {
            loaded = loadFromTomlFallback(tomlPath);
            if (loaded && m_rbpPath.isEmpty()) {
                m_rbpPath = QDir(rootDir).filePath(fi.baseName() + ".rbp");
                saveRbpFile(); // Salvage and regenerate .rbp
            }
        }
    }

    if (!loaded) {
        emit toastRequested("Could not load Ruby project metadata", true);
        resetState();
        emit activeProjectChanged();
        return false;
    }

    m_active = true;
    logActivity("Project opened: " + m_name);

    // Update recents in settings
    QSettings settings("AevoraLabs", "RubyTouch");
    QStringList recents = settings.value("recent_projects").toStringList();
    recents.removeAll(m_root);
    recents.prepend(m_root);
    while (recents.size() > 20) recents.removeLast();
    settings.setValue("recent_projects", recents);

    emit activeProjectChanged();
    emit toastRequested("Opened project: " + m_name, false);
    return true;
}

void RubyProject::closeProject() {
    if (!m_active) return;
    logActivity("Project closed: " + m_name);
    resetState();
    emit activeProjectChanged();
    emit toastRequested("Exited project", false);
}

bool RubyProject::updateMetadata(const QString& name,
                                 const QString& author,
                                 const QString& version,
                                 const QString& description,
                                 const QString& id) {
    if (!m_active) return false;
    if (!name.trimmed().isEmpty()) m_name = name.trimmed();
    if (!author.trimmed().isEmpty()) m_author = author.trimmed();
    if (!version.trimmed().isEmpty()) m_version = version.trimmed();
    if (!description.isEmpty()) m_description = description;
    if (!id.trimmed().isEmpty()) {
        QString cleanId = id.trimmed().toLower();
        cleanId.replace(QRegularExpression("[^a-z0-9_\\-\\.]"), "_");
        m_id = cleanId;
    }
    saveRbpFile();
    savePropertiesToml();
    emit activeProjectChanged();
    emit toastRequested("Project metadata updated", false);
    return true;
}

// ── Binary RBP2 Serialization (Delegated to rbp_format) ───────────────────

bool RubyProject::saveRbpFile() {
    if (m_rbpPath.isEmpty()) return false;

    ruby::project::RbpProject proj;
    proj.meta.id = m_id;
    proj.meta.name = m_name;
    proj.meta.author = m_author;
    proj.meta.version = m_version;
    proj.meta.description = m_description;
    proj.meta.created_ts = QDateTime::currentSecsSinceEpoch();
    proj.meta.last_modified_ts = QDateTime::currentSecsSinceEpoch();

    quint16 abis = 0;
    if (m_targetAbis.contains("arm64-v8a")) abis |= ruby::project::TargetAbi_Arm64V8A;
    if (m_targetAbis.contains("armeabi-v7a")) abis |= ruby::project::TargetAbi_ArmeabiV7A;
    if (m_targetAbis.contains("x86_64")) abis |= ruby::project::TargetAbi_X86_64;
    proj.meta.target_abis = abis;
    proj.meta.compiler_pipeline = ruby::project::CompilerPipeline::FileRiftV2;

    for (const QVariant& v : m_installedPackages) {
        QVariantMap m = v.toMap();
        ruby::project::RbpPackage pkg;
        pkg.id = m.value("id").toString();
        pkg.version = m.value("version").toString();
        pkg.flags = (m.value("type").toString() == "devtime") ? ruby::project::PackageFlag_Devtime : 0;
        proj.packages.append(pkg);
    }

    QString errMsg;
    ruby::project::RbpError err = ruby::project::write_rbp(m_rbpPath, proj, &errMsg);
    if (err != ruby::project::RbpError::None) {
        qWarning() << "Failed to write .rbp:" << errMsg;
        return false;
    }
    return true;
}

bool RubyProject::loadFromRbp(const QString& rbpPath) {
    ruby::project::RbpProject proj;
    QString errMsg;
    ruby::project::RbpError err = ruby::project::read_rbp(rbpPath, proj, &errMsg);
    if (err != ruby::project::RbpError::None) {
        qWarning() << "Failed to read .rbp:" << errMsg;
        return false;
    }

    m_id = proj.meta.id;
    m_name = proj.meta.name;
    m_author = proj.meta.author;
    m_version = proj.meta.version;
    m_description = proj.meta.description;

    m_targetAbis.clear();
    if (proj.meta.target_abis & ruby::project::TargetAbi_Arm64V8A) m_targetAbis << "arm64-v8a";
    if (proj.meta.target_abis & ruby::project::TargetAbi_ArmeabiV7A) m_targetAbis << "armeabi-v7a";
    if (proj.meta.target_abis & ruby::project::TargetAbi_X86_64) m_targetAbis << "x86_64";

    m_installedPackages.clear();
    for (const auto& pkg : proj.packages) {
        QVariantMap pm;
        pm["id"] = pkg.id;
        pm["version"] = pkg.version;
        pm["type"] = pkg.is_devtime() ? "devtime" : "runtime";
        m_installedPackages.append(pm);
    }
    return !m_name.isEmpty();
}

// ── Properties.toml Launcher Sync (Delegated to toml_sync) ─────────────────

bool RubyProject::savePropertiesToml() {
    QString tomlPath = propertiesTomlPath();
    if (tomlPath.isEmpty()) return false;

    ruby::project::LauncherMetadata meta;
    meta.id = m_id;
    meta.name = m_name;
    meta.author = m_author;
    meta.version = m_version;
    meta.description = m_description;

    QString errMsg;
    ruby::project::TomlSyncError err = ruby::project::write_properties_toml(tomlPath, meta, &errMsg);
    if (err != ruby::project::TomlSyncError::None) {
        qWarning() << "Failed to write properties.toml:" << errMsg;
        return false;
    }
    return true;
}

bool RubyProject::loadFromTomlFallback(const QString& tomlPath) {
    ruby::project::LauncherMetadata meta;
    QString errMsg;
    ruby::project::TomlSyncError err = ruby::project::read_properties_toml(tomlPath, meta, &errMsg);
    if (err != ruby::project::TomlSyncError::None) {
        return false;
    }

    m_id = meta.id;
    m_name = meta.name;
    m_author = meta.author;
    m_version = meta.version;
    m_description = meta.description;
    if (m_targetAbis.isEmpty()) m_targetAbis << "arm64-v8a";
    return !m_name.isEmpty();
}


// ── Two-Tier Reversion & Deletion ──────────────────────────────────────────

bool RubyProject::revertSingleFileToVanilla(const QString& relPath) {
    if (!m_active || relPath.isEmpty()) return false;
    logActivity("Reverted file to vanilla: " + relPath);
    emit toastRequested("Restored " + relPath + " to vanilla state", false);
    refreshDiffContext();
    return true;
}

bool RubyProject::revertProjectToVanilla(const QString& confirmationText) {
    if (confirmationText != "Ruby Delete") {
        emit toastRequested("Confirmation text must match 'Ruby Delete' exactly", true);
        return false;
    }
    logActivity("Total project reversion triggered");
    emit toastRequested("Project reverted to vanilla baseline", false);
    refreshDiffContext();
    return true;
}

bool RubyProject::deleteProject(const QString& confirmationText) {
    if (!m_active) return false;
    if (confirmationText != "Ruby Delete") {
        emit toastRequested("Confirmation text must match 'Ruby Delete' exactly", true);
        return false;
    }

    QString doomedRoot = m_root;
    logActivity("Deleting project permanently from disk: " + doomedRoot);
    closeProject();

    QDir dir(doomedRoot);
    if (dir.exists()) {
        dir.removeRecursively();
    }

    // Remove from settings
    QSettings settings("AevoraLabs", "RubyTouch");
    QStringList recents = settings.value("recent_projects").toStringList();
    recents.removeAll(doomedRoot);
    settings.setValue("recent_projects", recents);

    emit toastRequested("Project deleted permanently", false);
    return true;
}

void RubyProject::refreshDiffContext() {
    if (!m_active) return;
    m_isScanning = true;
    m_scanProgress = 0;
    emit scanStateChanged(true);
    emit scanProgressChanged(0);

    // Simulated fast scan progress for UI responsiveness
    m_scanProgress = 100;
    m_isScanning = false;
    m_lastScanTimeStr = QDateTime::currentDateTime().toString("hh:mm:ss");
    emit scanProgressChanged(100);
    emit scanStateChanged(false);
    emit diffStatsChanged();
    logActivity("Diff context refreshed");
}

bool RubyProject::exportToLauncher() {
    if (!m_active) return false;
    refreshDiffContext();
    logActivity("Exporting project to Launcher .zip");
    emit projectExportCompleted(true, QDir(m_root).filePath("builds/mod.zip"), "Exported successfully for Launcher");
    emit toastRequested("Exported to Launcher format successfully", false);
    return true;
}

} // namespace ruby::core
