#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QVector>
#include <QSet>
#include <QString>
#include <QPointer>
#include <atomic>

namespace ruby::android {

struct FileItem {
    QString fileName;
    QString filePath;
    QString fileType;
    bool isDir = false;
    QString fileSizeStr;
    QString fileDateStr;
    bool isPinned = false;

    // Sort keys, kept alongside the display strings so the model can re-sort
    // without re-stat()ing the filesystem (mirrors MobileAssetBrowser's use of
    // QFileInfo::lastModified()/size()/suffix() in its comparator).
    qint64 bytes = 0;
    QDateTime modified;
    QString suffix;
};

class RubyFileModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)
    Q_PROPERTY(QString folderName READ folderName NOTIFY currentPathChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY countChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // ── Added for the QML asset browser ──────────────────────────────────
    // Sort / category / hidden filtering previously lived only in
    // MobileAssetBrowser::populate_files(); the QML Files view needs the same
    // controls, so the model carries them now. Extension sets and the
    // dirs-always-on-top rule are copied from the legacy widget verbatim.
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY categoryChanged)
    Q_PROPERTY(QString sortMode READ sortMode WRITE setSortMode NOTIFY sortChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending WRITE setSortAscending NOTIFY sortChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(int dirCount READ dirCount NOTIFY countChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY countChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
    Q_PROPERTY(QString clipboardPath READ clipboardPath NOTIFY clipboardChanged)
    Q_PROPERTY(bool clipboardIsCut READ clipboardIsCut NOTIFY clipboardChanged)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
    Q_PROPERTY(bool isResourceRoot READ isResourceRoot NOTIFY resourceRootChanged)
    Q_PROPERTY(QVariantList recentFiles READ recentFiles NOTIFY recentFilesChanged)

public:
    enum FileRoles {
        FileNameRole = Qt::UserRole + 1,
        FilePathRole,
        FileTypeRole,
        IsDirRole,
        FileSizeStrRole,
        FileDateStrRole,
        IsPinnedRole
    };

    explicit RubyFileModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString currentPath() const { return m_currentDir.absolutePath(); }
    QString folderName() const { return m_currentDir.dirName(); }
    int count() const { return m_items.size(); }
    int dirCount() const { return m_dirCount; }
    int fileCount() const { return m_fileCount; }

    /// Legacy wording, verbatim: "<n> Folders, <n> Files in <dirname>".
    QString statusText() const;

    QString category() const { return m_category; }
    QString sortMode() const { return m_sortMode; }
    bool sortAscending() const { return m_sortAscending; }
    bool showHidden() const { return m_showHidden; }

    bool canPaste() const { return !m_clipboardPath.isEmpty(); }
    QString clipboardPath() const { return m_clipboardPath; }
    bool clipboardIsCut() const { return m_clipboardIsCut; }
    bool isLoading() const { return m_isLoading; }

    Q_INVOKABLE void setCategory(const QString& category);
    Q_INVOKABLE void setSortMode(const QString& mode);
    Q_INVOKABLE void setSortAscending(bool ascending);
    Q_INVOKABLE void setShowHidden(bool show);

    Q_INVOKABLE void navigateTo(const QString& path);
    Q_INVOKABLE void navigateUp();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setFilter(const QString& text);
    Q_INVOKABLE bool deleteItem(const QString& path);
    Q_INVOKABLE bool renameItem(const QString& path, const QString& newName);
    Q_INVOKABLE bool createDirectory(const QString& name);
    Q_INVOKABLE bool createFile(const QString& name, const QString& ext);
    Q_INVOKABLE QString createFileFromTemplate(const QString& name, const QString& templateId, bool compileBinary);
    Q_INVOKABLE QString getTemplateBoilerplate(const QString& templateId) const;
    Q_INVOKABLE void togglePin(const QString& path);
    Q_INVOKABLE void copyToClipboard(const QString& text);

    // ── Clipboard (Cut / Copy / Paste) ──────────────────────────────────
    Q_INVOKABLE void cutItem(const QString& path);
    Q_INVOKABLE void copyItem(const QString& path);
    Q_INVOKABLE void clearClipboard();
    Q_INVOKABLE bool paste();

    // ── ZArchiver-Grade Archive Operations ──────────────────────────────
    Q_INVOKABLE bool compressToZip(const QString& targetPath, const QString& outZipPath = QString());
    Q_INVOKABLE bool extractArchive(const QString& archivePath, bool toSubfolder = false);
    Q_INVOKABLE QVariantList getArchiveEntries(const QString& archivePath);

    // ── Asset Data Extraction & Conversion ──────────────────────────────
    Q_INVOKABLE QString extractLuaFromScene(const QString& scenePath);
    Q_INVOKABLE bool exportTextureToPng(const QString& texPath, const QString& outPngPath = QString());

    // ── Storage Roots & Item Properties ─────────────────────────────────
    Q_INVOKABLE QVariantList detectStorageRoots();
    Q_INVOKABLE QVariantMap getItemProperties(const QString& path);

    // ── Persistent Recent Files ─────────────────────────────────────────
    QVariantList recentFiles() const;
    Q_INVOKABLE void addRecentFile(const QString& path);
    Q_INVOKABLE void removeRecentFile(const QString& path);
    Q_INVOKABLE void clearRecentFiles();

    // ── Direct Game Engine Launch ───────────────────────────────────────
    // False in FOSS-compliant builds: play-in-game needs the proprietary
    // GlossHook engine, which FOSS builds exclude. QML binds button/menu
    // visibility to this.
    Q_PROPERTY(bool playInGameAvailable READ isPlayInGameAvailable CONSTANT)
    bool isPlayInGameAvailable() const;
    bool isResourceRoot() const { return m_isResourceRoot; }
    Q_INVOKABLE void launchGame();

    // Boot the vanilla engine straight into a scene, skipping the main menu. The engine
    // creates a throwaway "testplayer" profile whose current level is this scene, then
    // loads it through its normal path (hero spawns at the scene's "spawn_default").
    Q_INVOKABLE bool startSceneInGame(const QString& scenePath);

    // True when a file can be used as a direct-boot target (i.e. a .scene).
    Q_INVOKABLE bool canStartSceneInGame(const QString& scenePath) const;

    static bool checkSwordigoResourceRoot(const QString& dirPath);

signals:
    void currentPathChanged();
    void countChanged();
    void categoryChanged();
    void sortChanged();
    void showHiddenChanged();
    void clipboardChanged();
    void isLoadingChanged();
    void resourceRootChanged();
    void statusMessage(const QString& message);
    void recentFilesChanged();

private:
    void reload();
    void applyFilterAndSort();
    // Nearest ancestor of `startDir` that looks like a Swordigo resource root, or an empty
    // string. Used to hand GameActivity a resource directory the runner's VFS can serve.
    static QString findResourceRootFor(const QString& startDir);
    static QString detectFileType(const QFileInfo& fi);
    static QString formatSize(qint64 bytes);
    static bool passesCategory(const QFileInfo& fi, const QString& cat);
    static bool passesCategorySuffix(const QString& ext, const QString& cat);

    static bool copy_dir_recursive(const QString& src, const QString& dst);
    static qint64 calculate_dir_size(const QString& dir_path, int& out_files, int& out_dirs);

    QDir m_currentDir;
    QString m_filter;
    QVector<FileItem> m_items;
    QVector<FileItem> m_allItems;
    QDir m_lastLoadedDir;
    bool m_lastShowHidden = false;
    QSet<QString> m_pinnedPaths;

    QString m_category = QStringLiteral("all");
    QString m_sortMode = QStringLiteral("name");
    bool m_sortAscending = true;
    bool m_showHidden = false;
    int m_dirCount = 0;
    int m_fileCount = 0;

    QString m_clipboardPath;
    bool m_clipboardIsCut = false;
    bool m_isLoading = false;
    bool m_isResourceRoot = false;
    std::atomic<uint64_t> m_scanGeneration{0};
};

} // namespace ruby::android

