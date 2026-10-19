#include "ruby_file_model.h"
#include "file_operations.h"
#include <QThreadPool>
#include <QGuiApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFile>
#include <QStandardPaths>
#include <QImage>
#include <QSettings>
#include <QStorageInfo>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QTextStream>
#include "platform/zip_archive.h"
#include "platform/scl_parser.h"
#include "platform/pvr_loader.h"
#include "tools/filerift.h"
#include "ruby_quick_viewport.h"
#include <algorithm>
#include <QDebug>
#if defined(Q_OS_ANDROID)
#  include <QJniObject>
#  include <QJniEnvironment>
#  include <QtCore/qnativeinterface.h>
#endif


namespace ruby::android {

RubyFileModel::RubyFileModel(QObject* parent)
    : QAbstractListModel(parent)
{
    // Try primary external storage on Android, else fallback
    QDir primaryStorage(QStringLiteral("/storage/emulated/0"));
    if (primaryStorage.exists()) {
        m_currentDir = primaryStorage;
    } else {
        QDir sdcard(QStringLiteral("/sdcard"));
        if (sdcard.exists()) {
            m_currentDir = sdcard;
        } else {
            m_currentDir = QDir::current();
        }
    }
    m_operations = std::make_unique<FileOperations>();
    connect(m_operations.get(), &FileOperations::stateChanged,
            this, &RubyFileModel::operationStateChanged);
    connect(m_operations.get(), &FileOperations::finished, this,
            [this](int kind, bool ok, bool cancelled, const QString& summary,
                   const QString&) {
        // A successful cut/paste is the only time the clipboard is consumed.
        if (ok && kind == int(FileOperations::MoveJob)) clearClipboard();
        // Reload even after a partial failure or cancel: some items may have
        // been copied/deleted before the stop, and the list must show reality.
        m_allItems.clear();
        // Deleting the folder you are standing in leaves the model on a dead
        // path; step up once instead of showing an empty ghost view.
        if (!QFileInfo(m_currentDir.absolutePath()).isDir()) {
            QDir up = m_currentDir;
            if (up.cdUp()) {
                m_currentDir = up;
                emit currentPathChanged();
            }
        }
        reload();
        if (!summary.isEmpty()) emit statusMessage(summary);
        emit operationFinished(kind, ok, cancelled, summary);
    });

    refreshStorageInfo();
    reload();
}

RubyFileModel::~RubyFileModel() = default;

// ─── Async operation facade ────────────────────────────────────────────────

bool RubyFileModel::operationBusy() const {
    return m_operations && m_operations->busy();
}

QString RubyFileModel::operationTitle() const {
    return m_operations ? m_operations->title() : QString();
}

QString RubyFileModel::operationDetail() const {
    return m_operations ? m_operations->detail() : QString();
}

qreal RubyFileModel::operationProgress() const {
    return m_operations ? m_operations->progress() : -1.0;
}

QString RubyFileModel::operationBytesStr() const {
    return m_operations ? m_operations->bytesText() : QString();
}

QString RubyFileModel::operationItemsStr() const {
    return m_operations ? m_operations->itemsText() : QString();
}

bool RubyFileModel::conflictActive() const {
    return m_operations && m_operations->conflictActive();
}

QString RubyFileModel::conflictSourceName() const {
    return m_operations ? QFileInfo(m_operations->conflictSource()).fileName() : QString();
}

QString RubyFileModel::conflictTargetName() const {
    return m_operations ? QFileInfo(m_operations->conflictTarget()).fileName() : QString();
}

QString RubyFileModel::conflictTargetDir() const {
    return m_operations ? QFileInfo(m_operations->conflictTarget()).absolutePath() : QString();
}

QString RubyFileModel::clipboardSummary() const {
    if (m_clipboardPaths.isEmpty()) return QString();
    if (m_clipboardPaths.size() == 1) {
        return QFileInfo(m_clipboardPaths.first()).fileName();
    }
    return QStringLiteral("%1 items").arg(m_clipboardPaths.size());
}

void RubyFileModel::cancelFileOperation() {
    if (m_operations) m_operations->cancel();
}

void RubyFileModel::resolveFileConflict(int decision, bool applyToAll) {
    if (m_operations) m_operations->resolveConflict(decision, applyToAll);
}

int RubyFileModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_items.size();
}

QVariant RubyFileModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return QVariant();
    }

    const auto& item = m_items.at(index.row());
    switch (role) {
    case FileNameRole: return item.fileName;
    case FilePathRole: return item.filePath;
    case FileTypeRole: return item.fileType;
    case IsDirRole: return item.isDir;
    case FileSizeStrRole: return item.fileSizeStr;
    case FileDateStrRole: return item.fileDateStr;
    case IsPinnedRole: return item.isPinned;
    case IsSelectedRole: return m_selectedPaths.contains(item.filePath);
    default: return QVariant();
    }
}

QHash<int, QByteArray> RubyFileModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[FileNameRole] = "fileName";
    roles[FilePathRole] = "filePath";
    roles[FileTypeRole] = "fileType";
    roles[IsDirRole] = "isDir";
    roles[FileSizeStrRole] = "fileSizeStr";
    roles[FileDateStrRole] = "fileDateStr";
    roles[IsPinnedRole] = "isPinned";
    roles[IsSelectedRole] = "isSelected";
    return roles;
}

void RubyFileModel::navigateTo(const QString& path) {
    QDir dir(path);
    if (dir.exists()) {
        m_currentDir = dir;
        m_filter.clear();
        m_allItems.clear();
        clearSelection();
        cancelSearch();
        m_isSearchMode = false;
        emit isSearchModeChanged();
        refreshStorageInfo();
        reload();
        emit currentPathChanged();
    }
}

void RubyFileModel::navigateUp() {
    if (m_currentDir.cdUp()) {
        m_filter.clear();
        m_allItems.clear();
        clearSelection();
        cancelSearch();
        m_isSearchMode = false;
        emit isSearchModeChanged();
        refreshStorageInfo();
        reload();
        emit currentPathChanged();
    }
}

void RubyFileModel::refresh() {
    m_allItems.clear();
    reload();
}

void RubyFileModel::setFilter(const QString& text) {
    QString trimmed = text.trimmed();
    if (trimmed == m_filter) return;
    m_filter = trimmed;
    if (!m_allItems.isEmpty() && m_lastLoadedDir == m_currentDir) {
        applyFilterAndSort();
    } else {
        reload();
    }
}

bool RubyFileModel::deleteItem(const QString& path) {
    if (path.isEmpty()) return false;
    // Queue the recursive delete on the worker; the model is reloaded by the
    // finished() handler once the tree is actually gone. Never walk/remove a
    // directory on the GUI thread.
    m_pinnedPaths.remove(path);
    m_selectedPaths.remove(path);
    m_clipboardPaths.removeAll(path);
    if (m_clipboardPath == path) m_clipboardPath.clear();
    if (m_clipboardPaths.isEmpty()) m_clipboardIsCut = false;
    emit selectionChanged();
    emit clipboardChanged();
    return m_operations && m_operations->remove(QStringList{path});
}

bool RubyFileModel::renameItem(const QString& path, const QString& newName) {
    QFileInfo fi(path);
    QString newPath = fi.dir().filePath(newName.trimmed());
    bool success = QFile::rename(path, newPath);
    if (success) {
        if (m_pinnedPaths.contains(path)) {
            m_pinnedPaths.remove(path);
            m_pinnedPaths.insert(newPath);
        }
        m_allItems.clear();
        reload();
    }
    return success;
}

bool RubyFileModel::createDirectory(const QString& name) {
    if (name.trimmed().isEmpty()) return false;
    bool success = m_currentDir.mkdir(name.trimmed());
    if (success) {
        m_allItems.clear();
        reload();
    }
    return success;
}

bool RubyFileModel::createFile(const QString& name, const QString& ext) {
    QString finalName = name.trimmed();
    if (!ext.isEmpty() && !finalName.endsWith(ext, Qt::CaseInsensitive)) {
        finalName += ext;
    }
    QString fullPath = m_currentDir.filePath(finalName);
    QFile file(fullPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.close();
        m_allItems.clear();
        reload();
        return true;
    }
    return false;
}

QString RubyFileModel::getTemplateBoilerplate(const QString& templateId) const {
    const QString key = templateId.toLower();
    if (key == QStringLiteral("scene")) {
        return QString::fromUtf8(
            "## FileRift decoded Swordigo file type: scene\n\n"
            "Object{\n"
            "    TemplateName : 'Template 1'\n"
            "    Identifier : 'Background'\n"
            "    Component{\n"
            "        ClassName : 'Background'\n"
            "        Identifier : 101\n"
            "        BackgroundComponent{\n"
            "            TextureName : 'grassbg_night'\n"
            "        }\n"
            "    }\n"
            "    Position{\n"
            "        X : 0\n"
            "        Y : 0\n"
            "    }\n"
            "    Depth : 2\n"
            "    Rotation : 0\n"
            "    Scaling : 1\n"
            "    LocalAabb{\n"
            "        X : -400\n"
            "        Y : -300\n"
            "        Width : 800\n"
            "        Height : 600\n"
            "    }\n"
            "    Hidden : 0\n"
            "}\n"
            "Object{\n"
            "    TemplateName : 'Template 1'\n"
            "    Identifier : 'DirectionalLight'\n"
            "    Component{\n"
            "        ClassName : 'Light'\n"
            "        Identifier : 102\n"
            "        LightComponent{\n"
            "            Type : 2\n"
            "            Intensity : 0.8\n"
            "            Color{\n"
            "                R : 1\n"
            "                G : 0.95\n"
            "                B : 0.9\n"
            "                A : 1\n"
            "            }\n"
            "        }\n"
            "    }\n"
            "}\n"
            "Object{\n"
            "    TemplateName : 'Template 1'\n"
            "    Identifier : 'AmbientLight'\n"
            "    Component{\n"
            "        ClassName : 'Light'\n"
            "        Identifier : 103\n"
            "        LightComponent{\n"
            "            Type : 1\n"
            "            Intensity : 0.4\n"
            "            Color{\n"
            "                R : 0.6\n"
            "                G : 0.7\n"
            "                B : 0.9\n"
            "                A : 1\n"
            "            }\n"
            "        }\n"
            "    }\n"
            "}\n"
            "Object{\n"
            "    TemplateName : 'SceneObject'\n"
            "    Identifier : 'Ground_Platform'\n"
            "    Component{\n"
            "        ClassName : 'GroundPolygon'\n"
            "        Identifier : 104\n"
            "        GroundPolygonComponent{\n"
            "            Polygon{\n"
            "                Vertex{\n"
            "                    X : -300\n"
            "                    Y : 0\n"
            "                }\n"
            "                Vertex{\n"
            "                    X : 300\n"
            "                    Y : 0\n"
            "                }\n"
            "                Vertex{\n"
            "                    X : 300\n"
            "                    Y : 80\n"
            "                }\n"
            "                Vertex{\n"
            "                    X : -300\n"
            "                    Y : 80\n"
            "                }\n"
            "                Convex : 0\n"
            "                Closed : 1\n"
            "            }\n"
            "            Collides : 1\n"
            "            MinDepth : -45\n"
            "            MaxDepth : 45\n"
            "        }\n"
            "    }\n"
            "    Position{\n"
            "        X : 0\n"
            "        Y : 0\n"
            "    }\n"
            "    Depth : 0\n"
            "    Rotation : 0\n"
            "    Scaling : 1\n"
            "    LocalAabb{\n"
            "        X : -300\n"
            "        Y : 0\n"
            "        Width : 600\n"
            "        Height : 80\n"
            "    }\n"
            "    Hidden : 0\n"
            "}\n"
        );
    } else if (key == QStringLiteral("scl")) {
        return QString::fromUtf8(
            "## FileRift decoded Swordigo file type: scl\n\n"
            "Name : 'custom_objects'\n"
            "Template{\n"
            "    Object{\n"
            "        Identifier : 'example_pickup'\n"
            "        Component{\n"
            "            ClassName : 'CollectableItem'\n"
            "            Identifier : 1\n"
            "            CollectableItemComponent{\n"
            "                Type : 0\n"
            "                Value : 1\n"
            "                OnCollect{\n"
            "                }\n"
            "                Identifier : ''\n"
            "                ItemName : 'shard'\n"
            "                RequiresPickup : 1\n"
            "            }\n"
            "        }\n"
            "        Component{\n"
            "            ClassName : 'Model'\n"
            "            Identifier : 101\n"
            "            ModelComponent{\n"
            "                Name : 'item_heart'\n"
            "                YRotation : 0\n"
            "                EmissionFactor : 2\n"
            "                XRotation : 0\n"
            "                ShatterColor{\n"
            "                    R : 1\n"
            "                    G : 0.2\n"
            "                    B : 0.2\n"
            "                    A : 1\n"
            "                }\n"
            "                Origin{\n"
            "                    X : 0\n"
            "                    Y : 0\n"
            "                    Z : 0\n"
            "                }\n"
            "                Transparent : 0\n"
            "            }\n"
            "        }\n"
            "    }\n"
            "}\n"
        );
    } else if (key == QStringLiteral("lua")) {
        return QString::fromUtf8(
            "-- ============================================================================\n"
            "-- Swordigo Lua Script Chunk\n"
            "-- ============================================================================\n"
            "local self, target = ...;\n\n"
            "-- 'self'   : The SceneObject executing or owning this script/collision shape\n"
            "-- 'target' : The colliding or interacting SceneObject (e.g. hero)\n\n"
            "if target and target:identifier() == \"hero\" then\n"
            "    -- Example: Focus camera, show text bubble, or trigger scene logic\n"
            "    -- Camera.FocusAtShape(self);\n"
            "    -- SoundLibrary.PlayEffect(\"item_pickup\");\n"
            "end\n"
        );
    } else if (key == QStringLiteral("cpp")) {
        return QString::fromUtf8(
            "// ============================================================================\n"
            "// Custom Game Module\n"
            "// ============================================================================\n\n"
            "#include <iostream>\n"
            "#include <vector>\n"
            "#include <string>\n\n"
            "namespace custom {\n\n"
            "void initialize() {\n"
            "    // Initialization logic\n"
            "}\n\n"
            "} // namespace custom\n"
        );
    } else if (key == QStringLiteral("h")) {
        return QString::fromUtf8(
            "#pragma once\n"
            "// ============================================================================\n"
            "// Custom Game Module Header\n"
            "// ============================================================================\n\n"
            "#include <string>\n\n"
            "namespace custom {\n\n"
            "void initialize();\n\n"
            "} // namespace custom\n"
        );
    } else if (key == QStringLiteral("vert")) {
        return QString::fromUtf8(
            "attribute vec3 a_position;\n"
            "attribute vec2 a_texCoord;\n"
            "attribute vec4 a_color;\n\n"
            "uniform mat4 u_mvpMatrix;\n\n"
            "varying vec2 v_texCoord;\n"
            "varying vec4 v_color;\n\n"
            "void main() {\n"
            "    gl_Position = u_mvpMatrix * vec4(a_position, 1.0);\n"
            "    v_texCoord = a_texCoord;\n"
            "    v_color = a_color;\n"
            "}\n"
        );
    } else if (key == QStringLiteral("frag")) {
        return QString::fromUtf8(
            "precision mediump float;\n\n"
            "varying vec2 v_texCoord;\n"
            "varying vec4 v_color;\n\n"
            "uniform sampler2D u_texture;\n\n"
            "void main() {\n"
            "    gl_FragColor = texture2D(u_texture, v_texCoord) * v_color;\n"
            "}\n"
        );
    } else if (key == QStringLiteral("json")) {
        return QString::fromUtf8(
            "{\n"
            "  \"name\": \"SwordigoConfig\",\n"
            "  \"version\": \"1.0.0\",\n"
            "  \"settings\": {\n"
            "    \"enabled\": true,\n"
            "    \"volume\": 1.0\n"
            "  }\n"
            "}\n"
        );
    } else if (key == QStringLiteral("xml")) {
        return QString::fromUtf8(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<root>\n"
            "    <property name=\"title\">Swordigo</property>\n"
            "</root>\n"
        );
    } else if (key == QStringLiteral("fnt")) {
        return QString::fromUtf8(
            "info face=\"SwordigoFont\" size=32 bold=0 italic=0 charset=\"\" unicode=1 stretchH=100 smooth=1 aa=1 padding=0,0,0,0 spacing=1,1 outline=0\n"
            "common lineHeight=32 base=26 scaleW=512 scaleH=512 pages=1 packed=0 alphaChnl=1 redChnl=0 greenChnl=0 blueChnl=0\n"
            "page id=0 file=\"font_custom.tex.png\"\n"
            "chars count=95\n"
            "char id=32   x=0     y=0     width=0     height=0     xoffset=0     yoffset=0     xadvance=10    page=0  chnl=15\n"
            "char id=33   x=2     y=2     width=6     height=22    xoffset=2     yoffset=5     xadvance=10    page=0  chnl=15\n"
            "char id=65   x=10    y=2     width=18    height=22    xoffset=1     yoffset=5     xadvance=20    page=0  chnl=15\n"
        );
    }
    return QString::fromUtf8(
        "Ruby GG Document\n"
        "================\n\n"
    );
}

QString RubyFileModel::createFileFromTemplate(const QString& name, const QString& templateId, bool compileBinary) {
    QString key = templateId.toLower();
    QString ext = QStringLiteral(".") + key;
    if (key == QStringLiteral("scene")) ext = QStringLiteral(".scene");
    else if (key == QStringLiteral("scl")) ext = QStringLiteral(".scl");
    else if (key == QStringLiteral("lua")) ext = QStringLiteral(".lua");
    else if (key == QStringLiteral("cpp")) ext = QStringLiteral(".cpp");
    else if (key == QStringLiteral("h")) ext = QStringLiteral(".h");
    else if (key == QStringLiteral("vert")) ext = QStringLiteral(".vert");
    else if (key == QStringLiteral("frag")) ext = QStringLiteral(".frag");
    else if (key == QStringLiteral("json")) ext = QStringLiteral(".json");
    else if (key == QStringLiteral("xml")) ext = QStringLiteral(".xml");
    else if (key == QStringLiteral("fnt")) ext = QStringLiteral(".fnt");
    else ext = QStringLiteral(".txt");

    QString finalName = name.trimmed();
    if (finalName.isEmpty()) {
        finalName = QStringLiteral("new_file") + ext;
    } else if (!finalName.endsWith(ext, Qt::CaseInsensitive) && !finalName.contains(QLatin1Char('.'))) {
        finalName += ext;
    }

    QString fullPath = m_currentDir.filePath(finalName);
    QString content = getTemplateBoilerplate(key);

    if (compileBinary && (key == QStringLiteral("scene") || key == QStringLiteral("scl"))) {
        try {
            QString clean_markup = content;
            const QString prefix = QStringLiteral("## FileRift decoded Swordigo file type: ") + key;
            if (clean_markup.startsWith(prefix)) {
                clean_markup = clean_markup.mid(clean_markup.indexOf(QLatin1Char('\n')) + 1).trimmed();
            }
            std::string binary_bytes = ::filerift::recode_markup(
                clean_markup.toStdString(), key.toStdString());

            QFile out_file(fullPath);
            if (out_file.open(QIODevice::WriteOnly)) {
                out_file.write(binary_bytes.data(), static_cast<qint64>(binary_bytes.size()));
                out_file.close();
                m_allItems.clear();
                reload();
                return fullPath;
            }
        } catch (...) {
            // fallback to writing text
        }
    }

    QFile out_file(fullPath);
    if (out_file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        out_file.write(content.toUtf8());
        out_file.close();
        m_allItems.clear();
        reload();
        return fullPath;
    }
    return QString();
}

void RubyFileModel::togglePin(const QString& path) {
    if (m_pinnedPaths.contains(path)) {
        m_pinnedPaths.remove(path);
    } else {
        m_pinnedPaths.insert(path);
    }
    for (auto& item : m_allItems) {
        if (item.filePath == path) {
            item.isPinned = m_pinnedPaths.contains(path);
            break;
        }
    }
    if (!m_allItems.isEmpty() && m_lastLoadedDir == m_currentDir) {
        applyFilterAndSort();
    } else {
        reload();
    }
}

void RubyFileModel::copyToClipboard(const QString& text) {
    if (auto* cb = QGuiApplication::clipboard()) {
        cb->setText(text);
    }
}

QString RubyFileModel::statusText() const {
    // Legacy wording, verbatim from MobileAssetBrowser::populate_files().
    return QStringLiteral("%1 Folders, %2 Files in %3")
        .arg(m_dirCount)
        .arg(m_fileCount)
        .arg(m_currentDir.dirName());
}

void RubyFileModel::setCategory(const QString& category) {
    const QString c = category.toLower();
    if (c == m_category) return;
    m_category = c;
    if (!m_allItems.isEmpty() && m_lastLoadedDir == m_currentDir) {
        applyFilterAndSort();
    } else {
        reload();
    }
    emit categoryChanged();
}

void RubyFileModel::setSortMode(const QString& mode) {
    const QString m = mode.toLower();
    if (m == m_sortMode) return;
    m_sortMode = m;
    if (!m_allItems.isEmpty() && m_lastLoadedDir == m_currentDir) {
        applyFilterAndSort();
    } else {
        reload();
    }
    emit sortChanged();
}

void RubyFileModel::setSortAscending(bool ascending) {
    if (ascending == m_sortAscending) return;
    m_sortAscending = ascending;
    if (!m_allItems.isEmpty() && m_lastLoadedDir == m_currentDir) {
        applyFilterAndSort();
    } else {
        reload();
    }
    emit sortChanged();
}

void RubyFileModel::setShowHidden(bool show) {
    if (show == m_showHidden) return;
    m_showHidden = show;
    m_allItems.clear();
    reload();
    emit showHiddenChanged();
}

bool RubyFileModel::passesCategorySuffix(const QString& ext, const QString& cat) {
    if (cat.isEmpty() || cat == QLatin1String("all")) return true;

    if (cat == QLatin1String("scene"))    return ext == QLatin1String("scene");
    if (cat == QLatin1String("scl"))      return ext == QLatin1String("scl");
    if (cat == QLatin1String("models"))   return ext == QLatin1String("pod") || ext == QLatin1String("glb");
    if (cat == QLatin1String("archives")) return ext == QLatin1String("zip") || ext == QLatin1String("apk");
    if (cat == QLatin1String("lua"))      return ext == QLatin1String("lua");
    if (cat == QLatin1String("audio"))    return ext == QLatin1String("wav") || ext == QLatin1String("ogg") || ext == QLatin1String("mp3");
    return true;
}

bool RubyFileModel::passesCategory(const QFileInfo& fi, const QString& cat) {
    return passesCategorySuffix(fi.suffix().toLower(), cat);
}

void RubyFileModel::applyFilterAndSort() {
    QVector<FileItem> filtered;
    filtered.reserve(m_allItems.size());
    int dCount = 0;
    int fCount = 0;

    const QString filter = m_filter;
    const QString cat = m_category;

    for (const auto& item : m_allItems) {
        if (!filter.isEmpty() && !item.fileName.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }

        if (!item.isDir && !passesCategorySuffix(item.suffix.toLower(), cat)) {
            continue;
        }

        filtered.append(item);
    }

    const QString sortMode = m_sortMode;
    const bool sortAscending = m_sortAscending;

    std::sort(filtered.begin(), filtered.end(), [sortMode, sortAscending](const FileItem& a, const FileItem& b) {
        if (a.isPinned != b.isPinned) return a.isPinned > b.isPinned;
        if (a.isDir != b.isDir) return a.isDir;

        const FileItem& first = sortAscending ? a : b;
        const FileItem& second = sortAscending ? b : a;

        if (sortMode == QLatin1String("date")) {
            if (first.modified != second.modified) {
                return first.modified < second.modified;
            }
        } else if (sortMode == QLatin1String("size")) {
            if (first.bytes != second.bytes) {
                return first.bytes < second.bytes;
            }
        } else if (sortMode == QLatin1String("type")) {
            int cmp = first.suffix.localeAwareCompare(second.suffix);
            if (cmp != 0) return cmp < 0;
        } else { // "name"
            int cmp = first.fileName.localeAwareCompare(second.fileName);
            if (cmp != 0) return cmp < 0;
        }

        // Tie-breaker: always sort by filePath ascending for deterministic strict weak ordering
        return a.filePath < b.filePath;
    });

    for (const auto& item : filtered) {
        if (item.isDir) ++dCount; else ++fCount;
    }

    beginResetModel();
    m_items = std::move(filtered);
    m_dirCount = dCount;
    m_fileCount = fCount;
    endResetModel();
    emit countChanged();
}

void RubyFileModel::reload() {
    uint64_t gen = ++m_scanGeneration;
    m_isLoading = true;
    emit isLoadingChanged();

    QPointer<RubyFileModel> self(this);
    QDir targetDir = m_currentDir;
    bool showHidden = m_showHidden;
    QSet<QString> pinned = m_pinnedPaths;

    QThreadPool::globalInstance()->start([self, gen, targetDir, showHidden, pinned]() {
        if (!self || self->m_scanGeneration.load() != gen) return;

        QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
        if (showHidden) filters |= QDir::Hidden;

        QFileInfoList entries = targetDir.entryInfoList(filters);

        if (!self || self->m_scanGeneration.load() != gen) return;

        QVector<FileItem> items;
        items.reserve(entries.size());

        for (const auto& fi : entries) {
            FileItem item;
            item.fileName = fi.fileName();
            item.filePath = fi.absoluteFilePath();
            item.isDir = fi.isDir();
            item.fileType = detectFileType(fi);
            item.isPinned = pinned.contains(item.filePath);
            item.bytes = fi.isDir() ? 0 : fi.size();
            item.modified = fi.lastModified();
            item.suffix = fi.suffix();

            if (item.isDir) {
                // Instant non-blocking folder indicator: eliminate synchronous disk-thrashing
                item.fileSizeStr = QStringLiteral("Folder");
            } else {
                item.fileSizeStr = formatSize(fi.size());
            }

            item.fileDateStr = fi.lastModified().toString(QStringLiteral("MMM dd, yyyy"));
            items.append(item);
        }

        bool isRes = checkSwordigoResourceRoot(targetDir.absolutePath());

        if (!self || self->m_scanGeneration.load() != gen) return;

        QMetaObject::invokeMethod(self, [self, gen, targetDir, showHidden, items = std::move(items), isRes]() mutable {
            if (!self || self->m_scanGeneration.load() != gen) return;

            self->m_allItems = std::move(items);
            self->m_lastLoadedDir = targetDir;
            self->m_lastShowHidden = showHidden;

            self->applyFilterAndSort();

            self->m_isLoading = false;
            bool resChanged = (self->m_isResourceRoot != isRes);
            self->m_isResourceRoot = isRes;
            emit self->isLoadingChanged();
            if (resChanged) emit self->resourceRootChanged();
        }, Qt::QueuedConnection);
    });
}

bool RubyFileModel::checkSwordigoResourceRoot(const QString& dirPath) {
    if (dirPath.isEmpty()) return false;
    static const QStringList signatures = {
        QStringLiteral("hiro.POD"),
        QStringLiteral("hiro_run.POD"),
        QStringLiteral("hero.scene"),
        QStringLiteral("swordigo_title_2x.pvr"),
        QStringLiteral("worldsen.scl")
    };
    int hits = 0;
    for (const QString& sig : signatures) {
        if (QFile::exists(dirPath + QLatin1Char('/') + sig)) {
            hits++;
        }
    }
    if (hits < 3) {
        QString sub = dirPath + QStringLiteral("/resources");
        int subHits = 0;
        for (const QString& sig : signatures) {
            if (QFile::exists(sub + QLatin1Char('/') + sig)) {
                subHits++;
            }
        }
        if (subHits >= 3) return true;
    }
    return (hits >= 3);
}

bool RubyFileModel::isPlayInGameAvailable() const {
#ifdef RUBY_FOSS_COMPLIANT
    return false; // play-in-game needs the proprietary GlossHook engine
#else
    return true;
#endif
}

void RubyFileModel::launchGame() {
#ifdef RUBY_FOSS_COMPLIANT
    // FOSS flavor: the in-engine runner needs the proprietary GlossHook
    // library, which this build excludes. The play button is hidden in QML
    // via playInGameAvailable; this is a backstop.
    emit statusMessage(QStringLiteral("Play in game needs the full release — this FOSS build excludes the proprietary GlossHook engine."));
    return;
#endif

    RubyQuickViewport* vp = RubyQuickViewport::activeInstance();
    if (vp) {
        if (vp->meshEditActive()) {
            qDebug() << "[RubyFileModel] Active viewport has active mesh edit; applying edit before game launch";
            vp->applyMeshEdit();
        }
        if (vp->isDirty()) {
            qDebug() << "[RubyFileModel] Active viewport is dirty; saving scene before game launch:" << vp->filePath();
            vp->saveScene();
        }
    }

    QString resDir = m_currentDir.absolutePath();
    if (!QFile::exists(resDir + QStringLiteral("/hiro.POD")) &&
        QFile::exists(resDir + QStringLiteral("/resources/hiro.POD"))) {
        resDir = resDir + QStringLiteral("/resources");
    }

#if defined(Q_OS_ANDROID)
    QJniObject jPath = QJniObject::fromString(resDir);
    QJniObject::callStaticMethod<void>(
        "in/aevora/ruby/RubyActivity",
        "launchGameActivity",
        "(Ljava/lang/String;)V",
        jPath.object<jstring>()
    );
#else
    qDebug() << "[RubyFileModel] launchGame for" << resDir;
    emit statusMessage(QStringLiteral("Launching game with resources: ") + resDir);
#endif
}

QString RubyFileModel::findResourceRootFor(const QString& startDir) {
    QDir dir(startDir);
    // Walk up a few levels: mod folders are usually either the resource root itself, a
    // sibling of it, or nested one level inside the assets tree.
    for (int i = 0; i < 4 && dir.exists(); ++i) {
        const QString here = dir.absolutePath();
        if (checkSwordigoResourceRoot(here)) {
            if (!QFile::exists(here + QStringLiteral("/hiro.POD")) &&
                QFile::exists(here + QStringLiteral("/resources/hiro.POD"))) {
                return here + QStringLiteral("/resources");
            }
            return here;
        }
        if (!dir.cdUp()) break;
    }
    return QString();
}

bool RubyFileModel::canStartSceneInGame(const QString& scenePath) const {
    QFileInfo fi(scenePath);
    return fi.isFile() && fi.suffix().compare(QStringLiteral("scene"), Qt::CaseInsensitive) == 0;
}

bool RubyFileModel::startSceneInGame(const QString& scenePath) {
#ifdef RUBY_FOSS_COMPLIANT
    // FOSS flavor: direct scene boot needs the proprietary GlossHook engine.
    // The context-menu entry is hidden in QML via playInGameAvailable.
    Q_UNUSED(scenePath);
    emit statusMessage(QStringLiteral("Start scene in game needs the full release — this FOSS build excludes the proprietary GlossHook engine."));
    return false;
#endif
    QFileInfo fi(scenePath);
    if (!canStartSceneInGame(scenePath)) {
        emit statusMessage(QStringLiteral("Not a scene file: ") + scenePath);
        return false;
    }

    // Flush any pending mesh edits or unwritten scene changes
    RubyQuickViewport* vp = RubyQuickViewport::activeInstance();
    if (vp) {
        if (vp->meshEditActive()) {
            qDebug() << "[RubyFileModel] Active viewport has active mesh edit; applying edit before scene launch";
            vp->applyMeshEdit();
        }
        if (vp->isDirty()) {
            qDebug() << "[RubyFileModel] Active viewport is dirty; saving scene before scene launch:" << vp->filePath();
            vp->saveScene();
        }
    }

    // Refresh file info to log accurate on-disk size and mtime
    fi.refresh();
    qDebug() << "[RubyFileModel] Scene on disk before launch:" << fi.absoluteFilePath()
             << "size:" << fi.size() << "bytes, mtime:" << fi.lastModified().toString(Qt::ISODate);

    // The engine appends ".scene" to whatever we hand it, so strip the extension here.
    const QString sceneName = fi.completeBaseName();
    if (sceneName.isEmpty()) {
        emit statusMessage(QStringLiteral("Scene has no usable name: ") + scenePath);
        return false;
    }

    // Prefer the nearest real resource root so the scene's assets resolve; otherwise hand
    // over the scene's own directory and let the runner's VFS serve the scene from disk
    // while everything else falls back to the vanilla APK assets.
    QString resDir = findResourceRootFor(fi.absolutePath());
    if (resDir.isEmpty()) resDir = fi.absolutePath();

#if defined(Q_OS_ANDROID)
    QJniObject jRes = QJniObject::fromString(resDir);
    QJniObject jScene = QJniObject::fromString(sceneName);
    QJniObject::callStaticMethod<void>(
        "in/aevora/ruby/RubyActivity",
        "launchGameActivity",
        "(Ljava/lang/String;Ljava/lang/String;)V",
        jRes.object<jstring>(),
        jScene.object<jstring>()
    );
    emit statusMessage(QStringLiteral("Booting ") + sceneName + QStringLiteral(" from ") + resDir);
#else
    qDebug() << "[RubyFileModel] startSceneInGame" << sceneName << "from" << resDir;
    emit statusMessage(QStringLiteral("Direct scene boot requires Android (scene: ")
                       + sceneName + QLatin1Char(')'));
#endif
    return true;
}

QString RubyFileModel::detectFileType(const QFileInfo& fi) {
    if (fi.isDir()) return QStringLiteral("dir");
    QString ext = fi.suffix().toLower();
    if (ext == QStringLiteral("scene")) return QStringLiteral("scene");
    if (ext == QStringLiteral("scl"))   return QStringLiteral("scl");
    if (ext == QStringLiteral("pod") || ext == QStringLiteral("glb") || ext == QStringLiteral("fbx") || ext == QStringLiteral("obj")) return QStringLiteral("model");
    if (ext == QStringLiteral("pvr") || ext == QStringLiteral("tex") || ext == QStringLiteral("png") || ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg")) return QStringLiteral("texture");
    if (ext == QStringLiteral("wav") || ext == QStringLiteral("ogg") || ext == QStringLiteral("mp3")) return QStringLiteral("audio");
    if (ext == QStringLiteral("lua") || ext == QStringLiteral("filerift") || ext == QStringLiteral("txt") || ext == QStringLiteral("json") || ext == QStringLiteral("boulder")) return QStringLiteral("code");
    if (ext == QStringLiteral("zip") || ext == QStringLiteral("apk") || ext == QStringLiteral("tar") || ext == QStringLiteral("gz")) return QStringLiteral("archive");
    return QStringLiteral("generic");
}

QString RubyFileModel::formatSize(qint64 bytes) {
    if (bytes < 1024) return QString::number(bytes) + QStringLiteral(" B");
    double kb = bytes / 1024.0;
    if (kb < 1024) return QString::number(kb, 'f', 1) + QStringLiteral(" KB");
    double mb = kb / 1024.0;
    if (mb < 1024) return QString::number(mb, 'f', 1) + QStringLiteral(" MB");
    double gb = mb / 1024.0;
    return QString::number(gb, 'f', 2) + QStringLiteral(" GB");
}

bool RubyFileModel::copy_dir_recursive(const QString& src, const QString& dst) {
    QDir src_dir(src);
    if (!src_dir.exists()) return false;
    QDir dst_dir(dst);
    if (!dst_dir.exists()) {
        if (!QDir().mkpath(dst)) return false;
    }
    QFileInfoList list = src_dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const auto& fi : list) {
        QString s = fi.absoluteFilePath();
        QString d = dst + QLatin1Char('/') + fi.fileName();
        if (fi.isDir()) {
            if (!copy_dir_recursive(s, d)) return false;
        } else {
            if (QFile::exists(d)) QFile::remove(d);
            if (!QFile::copy(s, d)) return false;
        }
    }
    return true;
}

qint64 RubyFileModel::calculate_dir_size(const QString& dir_path, int& out_files, int& out_dirs) {
    qint64 total_bytes = 0;
    QDir dir(dir_path);
    QFileInfoList list = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const auto& fi : list) {
        if (fi.isDir()) {
            out_dirs++;
            total_bytes += calculate_dir_size(fi.absoluteFilePath(), out_files, out_dirs);
        } else {
            out_files++;
            total_bytes += fi.size();
        }
    }
    return total_bytes;
}

void RubyFileModel::cutItem(const QString& path) {
    m_clipboardPath = path;
    m_clipboardPaths = { path };
    m_clipboardIsCut = true;
    emit clipboardChanged();
    emit statusMessage(QStringLiteral("Cut: %1").arg(QFileInfo(path).fileName()));
}

void RubyFileModel::copyItem(const QString& path) {
    m_clipboardPath = path;
    m_clipboardPaths = { path };
    m_clipboardIsCut = false;
    emit clipboardChanged();
    emit statusMessage(QStringLiteral("Copied: %1").arg(QFileInfo(path).fileName()));
}

void RubyFileModel::clearClipboard() {
    m_clipboardPath.clear();
    m_clipboardPaths.clear();
    m_clipboardIsCut = false;
    emit clipboardChanged();
}

bool RubyFileModel::paste() {
    if (!m_operations) return false;
    if (m_clipboardPaths.isEmpty() && !m_clipboardPath.isEmpty()) {
        m_clipboardPaths.append(m_clipboardPath);
    }
    if (m_clipboardPaths.isEmpty()) return false;

    if (m_operations->busy()) {
        emit statusMessage(QStringLiteral("Another file operation is still running"));
        return false;
    }

    QStringList sources = m_clipboardPaths;
    sources.removeAll(QString());
    sources.removeDuplicates();
    if (sources.isEmpty()) return false;

    const QString dest = m_currentDir.absolutePath();
    emit statusMessage(m_clipboardIsCut
        ? QStringLiteral("Moving %1 item(s)…").arg(sources.size())
        : QStringLiteral("Copying %1 item(s)…").arg(sources.size()));
    return m_clipboardIsCut ? m_operations->move(sources, dest)
                            : m_operations->copy(sources, dest);
}

bool RubyFileModel::compressToZip(const QString& targetPath, const QString& outZipPath) {
    if (!m_operations || targetPath.isEmpty()) return false;
    if (!QFileInfo::exists(targetPath)) {
        emit statusMessage(QStringLiteral("Target item does not exist: %1").arg(targetPath));
        return false;
    }
    return m_operations->archive(QStringList{targetPath}, outZipPath,
                                 m_currentDir.absolutePath());
}

bool RubyFileModel::extractArchive(const QString& archivePath, bool toSubfolder) {
    if (!m_operations || archivePath.isEmpty()) return false;
    const QFileInfo fi(archivePath);
    if (!fi.exists()) return false;

    QString destFolder = m_currentDir.absolutePath();
    if (toSubfolder) {
        destFolder = m_currentDir.filePath(fi.completeBaseName());
    }
    // extractAll() creates the destination (including the subfolder) itself.
    return m_operations->extract(archivePath, destFolder);
}

QVariantList RubyFileModel::getArchiveEntries(const QString& archivePath) {
    QVariantList list;
    std::vector<zip::Entry> entries;
    if (zip::read_entries(archivePath.toStdString(), entries)) {
        for (const auto& e : entries) {
            QVariantMap map;
            map[QStringLiteral("name")] = QString::fromStdString(e.name);
            map[QStringLiteral("uncompSize")] = static_cast<qint64>(e.uncomp_size);
            map[QStringLiteral("compSize")] = static_cast<qint64>(e.comp_size);
            map[QStringLiteral("uncompSizeStr")] = formatSize(e.uncomp_size);
            map[QStringLiteral("compSizeStr")] = formatSize(e.comp_size);
            map[QStringLiteral("method")] = (e.method == 8) ? QStringLiteral("Deflate") : QStringLiteral("Store");
            list.append(map);
        }
    }
    return list;
}

QVariantList RubyFileModel::detectStorageRoots() {
    QVariantList result;
    std::vector<QString> roots;

    if (QDir(QStringLiteral("/storage/emulated/0")).exists()) {
        roots.push_back(QStringLiteral("/storage/emulated/0"));
    }
    if (QDir(QStringLiteral("/sdcard")).exists() && !roots.empty() && roots.front() != QStringLiteral("/sdcard")) {
        roots.push_back(QStringLiteral("/sdcard"));
    }

    const QString primary = roots.empty() ? QStringLiteral("/storage/emulated/0") : roots.front();
    const QStringList common_folders = {
        QStringLiteral("/Download"),
        QStringLiteral("/Documents"),
        QStringLiteral("/DCIM"),
        QStringLiteral("/Pictures"),
        QStringLiteral("/Music"),
        QStringLiteral("/Android/data"),
        QStringLiteral("/Android/media")
    };
    for (const auto& sub : common_folders) {
        if (QDir(primary + sub).exists()) {
            roots.push_back(primary + sub);
        }
    }

    // Removable / USB mounts
    QDir storage_dir(QStringLiteral("/storage"));
    if (storage_dir.exists()) {
        QFileInfoList entries = storage_dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto& entry : entries) {
            const QString name = entry.fileName();
            if (name != QStringLiteral("emulated") && name != QStringLiteral("self")) {
                roots.push_back(entry.absoluteFilePath());
            }
        }
    }

    // App data locations
    QString app_data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!app_data.isEmpty() && QDir(app_data).exists()) {
        roots.push_back(app_data);
    }
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (!docs.isEmpty() && QDir(docs).exists()) {
        roots.push_back(docs);
    }

    // Current working directory
    roots.push_back(QDir::currentPath());

    // Root filesystem
    roots.push_back(QStringLiteral("/"));

    // Remove duplicates while preserving order
    std::vector<QString> unique_roots;
    for (const auto& r : roots) {
        if (std::find(unique_roots.begin(), unique_roots.end(), r) == unique_roots.end()) {
            unique_roots.push_back(r);
        }
    }

    for (const auto& r : unique_roots) {
        QVariantMap item;
        item[QStringLiteral("path")] = r;
        if (r == QStringLiteral("/storage/emulated/0") || r == QStringLiteral("/sdcard")) {
            item[QStringLiteral("label")] = QStringLiteral("Internal Shared Storage");
            item[QStringLiteral("type")] = QStringLiteral("internal");
        } else if (r.endsWith(QStringLiteral("/Download"))) {
            item[QStringLiteral("label")] = QStringLiteral("Downloads Folder");
            item[QStringLiteral("type")] = QStringLiteral("downloads");
        } else if (r.endsWith(QStringLiteral("/Documents"))) {
            item[QStringLiteral("label")] = QStringLiteral("Documents Folder");
            item[QStringLiteral("type")] = QStringLiteral("documents");
        } else if (r.startsWith(QStringLiteral("/storage/"))) {
            item[QStringLiteral("label")] = QStringLiteral("MicroSD / USB: ") + QFileInfo(r).fileName();
            item[QStringLiteral("type")] = QStringLiteral("removable");
        } else if (r == QStringLiteral("/")) {
            item[QStringLiteral("label")] = QStringLiteral("Root Filesystem (/)");
            item[QStringLiteral("type")] = QStringLiteral("root");
        } else {
            item[QStringLiteral("label")] = QFileInfo(r).fileName();
            item[QStringLiteral("type")] = QStringLiteral("folder");
        }
        result.append(item);
    }
    return result;
}

QVariantMap RubyFileModel::getItemProperties(const QString& path) {
    QVariantMap map;
    QFileInfo fi(path);
    if (!fi.exists()) return map;

    map[QStringLiteral("name")] = fi.fileName();
    map[QStringLiteral("path")] = fi.absoluteFilePath();
    map[QStringLiteral("isDir")] = fi.isDir();
    map[QStringLiteral("dateStr")] = fi.lastModified().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    map[QStringLiteral("permissions")] = QString::number(static_cast<uint>(fi.permissions()), 8);

    if (fi.isDir()) {
        int files = 0, dirs = 0;
        qint64 totalBytes = calculate_dir_size(path, files, dirs);
        map[QStringLiteral("bytes")] = totalBytes;
        map[QStringLiteral("sizeStr")] = formatSize(totalBytes);
        map[QStringLiteral("filesInside")] = files;
        map[QStringLiteral("dirsInside")] = dirs;
    } else {
        map[QStringLiteral("bytes")] = fi.size();
        map[QStringLiteral("sizeStr")] = formatSize(fi.size());
        map[QStringLiteral("filesInside")] = 0;
        map[QStringLiteral("dirsInside")] = 0;
    }
    return map;
}

QString RubyFileModel::extractLuaFromScene(const QString& scenePath) {
    if (scenePath.isEmpty()) return QString();
    std::string lua_code = scl::extract_lua(scenePath.toStdString());
    if (lua_code.empty()) {
        emit statusMessage(QStringLiteral("No embedded Lua script found in scene"));
        return QString();
    }

    QFileInfo fi(scenePath);
    QString baseName = fi.completeBaseName();
    QString outExt = QStringLiteral(".lua");
    if (lua_code.size() >= 4 && lua_code[0] == '\x1b' && lua_code[1] == 'L' && lua_code[2] == 'u' && lua_code[3] == 'a') {
        outExt = QStringLiteral(".luac");
    }
    QString outPath = fi.absolutePath() + QLatin1Char('/') + baseName + QStringLiteral(".extracted") + outExt;

    QFile outFile(outPath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        emit statusMessage(QStringLiteral("Failed to write extracted script: ") + outFile.errorString());
        return QString();
    }
    outFile.write(lua_code.data(), static_cast<qint64>(lua_code.size()));
    outFile.close();

    emit statusMessage(QStringLiteral("Extracted Lua to: ") + QFileInfo(outPath).fileName());
    refresh();
    return outPath;
}

bool RubyFileModel::exportTextureToPng(const QString& texPath, const QString& outPngPath) {
    if (texPath.isEmpty()) return false;
    QFileInfo fi(texPath);
    QString out = outPngPath;
    if (out.isEmpty()) {
        out = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".png");
    }

    QFile input(texPath);
    if (!input.open(QIODevice::ReadOnly)) {
        emit statusMessage(QStringLiteral("Cannot read texture file"));
        return false;
    }
    QByteArray bytes = input.readAll();
    input.close();

    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    QImage img;
    if (pvr_decode_to_rgba(reinterpret_cast<const uint8_t*>(bytes.constData()),
                           static_cast<size_t>(bytes.size()), rgba, w, h) && w > 0 && h > 0) {
        img = QImage(rgba.data(), w, h, QImage::Format_RGBA8888).copy();
    } else {
        img.loadFromData(bytes);
    }

    if (img.isNull()) {
        emit statusMessage(QStringLiteral("Failed to decode texture"));
        return false;
    }

    if (!img.save(out, "PNG")) {
        emit statusMessage(QStringLiteral("Failed to save PNG"));
        return false;
    }

    emit statusMessage(QStringLiteral("Exported PNG: ") + QFileInfo(out).fileName());
    refresh();
    return true;
}

bool RubyFileModel::sanitizeAudioForEngine(const QString& audioPath, bool replaceOriginal) {
    if (audioPath.isEmpty()) return false;
    QFileInfo fi(audioPath);
    if (!fi.exists() || fi.isDir()) {
        emit statusMessage(QStringLiteral("Audio file does not exist: ") + fi.fileName());
        return false;
    }

    const QString ext = fi.suffix().toLower();
    const QString targetWav = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".wav");
    const QString tempWav = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".tmp_clean.wav");

    bool success = false;

    // Fast-path for WAV files: parse chunks, discard metadata (LIST, ID3, JUNK), and output pure canonical 44-byte WAV
    if (ext == QStringLiteral("wav")) {
        QFile in(audioPath);
        if (in.open(QIODevice::ReadOnly)) {
            QByteArray data = in.readAll();
            in.close();

            if (data.size() >= 44 && std::memcmp(data.constData(), "RIFF", 4) == 0 && std::memcmp(data.constData() + 8, "WAVE", 4) == 0) {
                const char* raw = data.constData();
                size_t offset = 12;
                uint16_t audio_format = 0;
                uint16_t num_channels = 0;
                uint32_t sample_rate = 0;
                uint16_t bits_per_sample = 0;
                const char* pcm_data = nullptr;
                size_t pcm_size = 0;

                while (offset + 8 <= static_cast<size_t>(data.size())) {
                    char cid[5] = {0};
                    std::memcpy(cid, raw + offset, 4);
                    uint32_t csize = 0;
                    std::memcpy(&csize, raw + offset + 4, 4);
                    offset += 8;

                    if (std::memcmp(cid, "fmt ", 4) == 0 && csize >= 16 && offset + 16 <= static_cast<size_t>(data.size())) {
                        std::memcpy(&audio_format, raw + offset, 2);
                        std::memcpy(&num_channels, raw + offset + 2, 2);
                        std::memcpy(&sample_rate, raw + offset + 4, 4);
                        std::memcpy(&bits_per_sample, raw + offset + 14, 2);
                    } else if (std::memcmp(cid, "data", 4) == 0) {
                        pcm_data = raw + offset;
                        pcm_size = std::min<size_t>(csize, data.size() - offset);
                        break;
                    }
                    offset += csize;
                    if (csize & 1) offset += 1;
                }

                if (pcm_data && pcm_size > 0 && num_channels > 0 && sample_rate > 0) {
                    QByteArray converted_pcm;
                    if (audio_format == 3 && bits_per_sample == 32) {
                        // IEEE 32-bit float to 16-bit PCM conversion
                        size_t num_floats = pcm_size / sizeof(float);
                        converted_pcm.resize(static_cast<int>(num_floats * sizeof(int16_t)));
                        const float* fptr = reinterpret_cast<const float*>(pcm_data);
                        int16_t* sptr = reinterpret_cast<int16_t*>(converted_pcm.data());
                        for (size_t i = 0; i < num_floats; ++i) {
                            float v = std::clamp(fptr[i], -1.0f, 1.0f);
                            sptr[i] = static_cast<int16_t>(v * 32767.0f);
                        }
                        pcm_data = converted_pcm.constData();
                        pcm_size = converted_pcm.size();
                        bits_per_sample = 16;
                    }

                    // Build canonical 44-byte WAV header strictly matching Caver::GetAudioFileData
                    QByteArray outData;
                    outData.resize(44 + static_cast<int>(pcm_size));
                    char* h = outData.data();
                    std::memcpy(h, "RIFF", 4);
                    uint32_t riff_size = static_cast<uint32_t>(36 + pcm_size);
                    std::memcpy(h + 4, &riff_size, 4);
                    std::memcpy(h + 8, "WAVE", 4);
                    std::memcpy(h + 12, "fmt ", 4);
                    uint32_t fmt_size = 16;
                    std::memcpy(h + 16, &fmt_size, 4);
                    uint16_t fmt_tag = 1; // PCM
                    std::memcpy(h + 20, &fmt_tag, 2);
                    std::memcpy(h + 22, &num_channels, 2);
                    std::memcpy(h + 24, &sample_rate, 4);
                    uint32_t byte_rate = sample_rate * num_channels * (bits_per_sample / 8);
                    std::memcpy(h + 28, &byte_rate, 4);
                    uint16_t block_align = num_channels * (bits_per_sample / 8);
                    std::memcpy(h + 32, &block_align, 2);
                    std::memcpy(h + 34, &bits_per_sample, 2);
                    std::memcpy(h + 36, "data", 4);
                    uint32_t dsize = static_cast<uint32_t>(pcm_size);
                    std::memcpy(h + 40, &dsize, 4);
                    std::memcpy(h + 44, pcm_data, pcm_size);

                    QFile out(tempWav);
                    if (out.open(QIODevice::WriteOnly)) {
                        out.write(outData);
                        out.flush();
                        out.close();
                        success = true;
                    }
                }
            }
        }
    }

    // If not a standard WAV or if parsing failed, try Android platform MediaCodec transcoder
#if defined(Q_OS_ANDROID)
    if (!success) {
        QJniObject jIn = QJniObject::fromString(audioPath);
        QJniObject jOut = QJniObject::fromString(tempWav);
        jboolean ok = QJniObject::callStaticMethod<jboolean>(
            "in/aevora/ruby/RubyActivity",
            "decodeAudioToCanonicalWav",
            "(Ljava/lang/String;Ljava/lang/String;)Z",
            jIn.object<jstring>(),
            jOut.object<jstring>()
        );
        success = (ok == JNI_TRUE);
    }
#endif

    if (!success) {
        emit statusMessage(QStringLiteral("Audio sanitization failed for: ") + fi.fileName());
        return false;
    }

    // Replace original or write target canonical WAV
    if (replaceOriginal && ext == QStringLiteral("wav")) {
        QFile::remove(audioPath);
        if (!QFile::rename(tempWav, audioPath)) {
            QFile::copy(tempWav, audioPath);
            QFile::remove(tempWav);
        }
        emit statusMessage(QStringLiteral("Fixed WAV for game engine (stripped tags): ") + fi.fileName());
    } else {
        if (replaceOriginal && ext != QStringLiteral("wav")) {
            QFile::remove(audioPath); // remove non-compliant .ogg/.mp3
        }
        QFile::remove(targetWav);
        if (!QFile::rename(tempWav, targetWav)) {
            QFile::copy(tempWav, targetWav);
            QFile::remove(tempWav);
        }
        emit statusMessage(QStringLiteral("Transcoded to engine canonical WAV: ") + QFileInfo(targetWav).fileName());
    }

    refresh();
    return true;
}

void RubyFileModel::addRecentFile(const QString& path) {
    if (path.isEmpty()) return;
    QFileInfo fi(path);
    if (!fi.exists() || fi.isDir()) return;

    QSettings settings;
    QStringList recents = settings.value(QStringLiteral("ruby_mobile/recent_files")).toStringList();
    recents.removeAll(fi.absoluteFilePath());
    recents.prepend(fi.absoluteFilePath());
    while (recents.size() > 20) {
        recents.removeLast();
    }
    settings.setValue(QStringLiteral("ruby_mobile/recent_files"), recents);
    emit recentFilesChanged();
}

void RubyFileModel::removeRecentFile(const QString& path) {
    QSettings settings;
    QStringList recents = settings.value(QStringLiteral("ruby_mobile/recent_files")).toStringList();
    recents.removeAll(path);
    settings.setValue(QStringLiteral("ruby_mobile/recent_files"), recents);
    emit recentFilesChanged();
}

void RubyFileModel::clearRecentFiles() {
    QSettings settings;
    settings.remove(QStringLiteral("ruby_mobile/recent_files"));
    emit recentFilesChanged();
}

QVariantList RubyFileModel::recentFiles() const {
    QSettings settings;
    QStringList recents = settings.value(QStringLiteral("ruby_mobile/recent_files")).toStringList();
    QVariantList list;
    for (const QString& path : recents) {
        QFileInfo fi(path);
        if (!fi.exists()) continue;
        QVariantMap map;
        map[QStringLiteral("fileName")] = fi.fileName();
        map[QStringLiteral("filePath")] = fi.absoluteFilePath();
        map[QStringLiteral("fileType")] = detectFileType(fi);
        map[QStringLiteral("fileSizeStr")] = formatSize(fi.size());
        map[QStringLiteral("fileDateStr")] = fi.lastModified().toString(QStringLiteral("MMM d, yyyy"));
        list.append(map);
        if (list.size() >= 10) break;
    }
    return list;
}

// ── Multi-Selection & Batch Operations (Index FM SelectionBar) ─────────
void RubyFileModel::setItemSelected(const QString& path, bool selected) {
    if (path.isEmpty()) return;
    bool changed = false;
    if (selected) {
        if (!m_selectedPaths.contains(path)) {
            m_selectedPaths.insert(path);
            changed = true;
        }
    } else {
        if (m_selectedPaths.contains(path)) {
            m_selectedPaths.remove(path);
            changed = true;
        }
    }
    if (changed) {
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items[i].filePath == path) {
                m_items[i].isSelected = selected;
                QModelIndex idx = index(i, 0);
                emit dataChanged(idx, idx, {IsSelectedRole});
                break;
            }
        }
        emit selectionChanged();
    }
}

void RubyFileModel::toggleItemSelected(const QString& path) {
    bool current = m_selectedPaths.contains(path);
    setItemSelected(path, !current);
}

bool RubyFileModel::isItemSelected(const QString& path) const {
    return m_selectedPaths.contains(path);
}

void RubyFileModel::selectAll() {
    bool changed = false;
    for (int i = 0; i < m_items.size(); ++i) {
        if (!m_selectedPaths.contains(m_items[i].filePath)) {
            m_selectedPaths.insert(m_items[i].filePath);
            m_items[i].isSelected = true;
            changed = true;
        }
    }
    if (changed) {
        if (!m_items.isEmpty()) {
            emit dataChanged(index(0, 0), index(m_items.size() - 1, 0), {IsSelectedRole});
        }
        emit selectionChanged();
    }
}

void RubyFileModel::invertSelection() {
    for (auto& item : m_items) {
        if (m_selectedPaths.contains(item.filePath)) {
            m_selectedPaths.remove(item.filePath);
            item.isSelected = false;
        } else {
            m_selectedPaths.insert(item.filePath);
            item.isSelected = true;
        }
    }
    if (!m_items.isEmpty()) {
        emit dataChanged(index(0, 0), index(m_items.size() - 1, 0), {IsSelectedRole});
    }
    emit selectionChanged();
}

void RubyFileModel::removeSelectedPath(const QString& path) {
    if (!m_selectedPaths.contains(path)) return;
    m_selectedPaths.remove(path);
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].filePath == path) {
            m_items[i].isSelected = false;
            emit dataChanged(index(i, 0), index(i, 0), {IsSelectedRole});
            break;
        }
    }
    emit selectionChanged();
}

void RubyFileModel::clearSelection() {
    if (m_selectedPaths.isEmpty()) return;
    m_selectedPaths.clear();
    for (int i = 0; i < m_items.size(); ++i) {
        m_items[i].isSelected = false;
    }
    if (!m_items.isEmpty()) {
        emit dataChanged(index(0, 0), index(m_items.size() - 1, 0), {IsSelectedRole});
    }
    emit selectionChanged();
}

QStringList RubyFileModel::getSelectedPaths() const {
    return m_selectedPaths.values();
}

QVariantList RubyFileModel::getSelectedItems() const {
    QVariantList list;
    list.reserve(m_selectedPaths.size());
    for (const QString& path : m_selectedPaths) {
        QFileInfo fi(path);
        if (!fi.exists()) continue;
        QVariantMap m;
        m[QStringLiteral("name")] = fi.fileName();
        m[QStringLiteral("path")] = fi.absoluteFilePath();
        m[QStringLiteral("isDir")] = fi.isDir();
        m[QStringLiteral("fileType")] = detectFileType(fi);
        m[QStringLiteral("sizeStr")] = fi.isDir() ? QStringLiteral("Folder") : formatSize(fi.size());
        m[QStringLiteral("bytes")] = fi.size();
        list.append(m);
    }
    return list;
}

QString RubyFileModel::getSelectedTotalBytesStr() const {
    qint64 total = 0;
    for (const auto& item : m_items) {
        if (m_selectedPaths.contains(item.filePath)) {
            total += item.bytes;
        }
    }
    return formatSize(total);
}

void RubyFileModel::shareSelected() {
    shareFiles(getSelectedPaths());
}

void RubyFileModel::shareFiles(const QStringList& paths) {
    if (paths.isEmpty()) return;
#if defined(Q_OS_ANDROID)
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (activity.isValid()) {
        QJniEnvironment env;
        jclass stringClass = env->FindClass("java/lang/String");
        jobjectArray array = env->NewObjectArray(paths.size(), stringClass, nullptr);
        for (int i = 0; i < paths.size(); ++i) {
            jstring str = env->NewStringUTF(paths[i].toUtf8().constData());
            env->SetObjectArrayElement(array, i, str);
            env->DeleteLocalRef(str);
        }
        activity.callMethod<void>("shareFiles", "([Ljava/lang/String;)V", array);
        env->DeleteLocalRef(array);
    }
#else
    emit statusMessage(QStringLiteral("Sharing %1 file(s)").arg(paths.size()));
#endif
}

void RubyFileModel::sanitizeSelectedAudio() {
    int fixed = 0;
    for (const QString& p : m_selectedPaths) {
        QFileInfo fi(p);
        QString ext = fi.suffix().toLower();
        if (ext == QLatin1String("wav") || ext == QLatin1String("ogg") || ext == QLatin1String("mp3")) {
            if (sanitizeAudioForEngine(p, true)) {
                fixed++;
            }
        }
    }
    emit statusMessage(QStringLiteral("Sanitized %1 audio file(s) for Swordigo engine").arg(fixed));
}

bool RubyFileModel::deleteMultiple(const QStringList& paths) {
    if (paths.isEmpty() || !m_operations) return false;
    for (const QString& p : paths) {
        m_pinnedPaths.remove(p);
        m_selectedPaths.remove(p);
        m_clipboardPaths.removeAll(p);
        if (m_clipboardPath == p) m_clipboardPath.clear();
    }
    if (m_clipboardPaths.isEmpty()) m_clipboardIsCut = false;
    emit selectionChanged();
    emit clipboardChanged();
    return m_operations->remove(paths);
}

void RubyFileModel::copyMultiple(const QStringList& paths) {
    if (paths.isEmpty()) return;
    m_clipboardPaths = paths;
    m_clipboardPath = paths.first();
    m_clipboardIsCut = false;
    emit clipboardChanged();
    emit statusMessage(QStringLiteral("Copied %1 item(s) to clipboard").arg(paths.size()));
}

void RubyFileModel::cutMultiple(const QStringList& paths) {
    if (paths.isEmpty()) return;
    m_clipboardPaths = paths;
    m_clipboardPath = paths.first();
    m_clipboardIsCut = true;
    emit clipboardChanged();
    emit statusMessage(QStringLiteral("Cut %1 item(s) to clipboard").arg(paths.size()));
}

bool RubyFileModel::compressMultiple(const QStringList& paths, const QString& outZipPath) {
    if (paths.isEmpty() || !m_operations) return false;
    const bool queued = m_operations->archive(paths, outZipPath, m_currentDir.absolutePath());
    if (queued) clearSelection();
    return queued;
}

// ── Deep Recursive Search (Index & HyprFM SearchEngine) ────────────────
void RubyFileModel::searchRecursive(const QString& query) {
    const QString q = query.trimmed();
    if (q.isEmpty()) {
        clearSearch();
        return;
    }

    uint64_t gen = ++m_searchGeneration;
    m_isSearching = true;
    m_isSearchMode = true;
    m_searchResultCount = 0;
    emit isSearchingChanged();
    emit isSearchModeChanged();
    emit searchResultCountChanged();

    QPointer<RubyFileModel> self(this);
    QString rootPath = m_currentDir.absolutePath();
    bool showHidden = m_showHidden;
    QString cat = m_category;

    QThreadPool::globalInstance()->start([self, gen, rootPath, q, showHidden, cat]() {
        if (!self || self->m_searchGeneration.load() != gen) return;

        QDir::Filters filters = QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot;
        if (showHidden) filters |= QDir::Hidden;

        QDirIterator it(rootPath, filters, QDirIterator::Subdirectories);
        QVector<FileItem> matches;
        matches.reserve(120);

        bool isWildcard = q.contains(QLatin1Char('*')) || q.contains(QLatin1Char('?'));
        QRegularExpression regex;
        if (isWildcard) {
            regex = QRegularExpression(QRegularExpression::wildcardToRegularExpression(q), QRegularExpression::CaseInsensitiveOption);
        }

        int count = 0;
        const int maxResults = 1000;

        while (it.hasNext() && count < maxResults) {
            if (!self || self->m_searchGeneration.load() != gen) return;

            it.next();
            QFileInfo fi = it.fileInfo();
            const QString fileName = fi.fileName();

            bool match = false;
            if (isWildcard) {
                match = regex.match(fileName).hasMatch();
            } else {
                match = fileName.contains(q, Qt::CaseInsensitive);
            }

            if (!match) continue;

            if (!fi.isDir() && !passesCategorySuffix(fi.suffix().toLower(), cat)) {
                continue;
            }

            FileItem item;
            item.fileName = fileName;
            item.filePath = fi.absoluteFilePath();
            item.isDir = fi.isDir();
            item.fileType = detectFileType(fi);
            item.isPinned = self->m_pinnedPaths.contains(item.filePath);
            item.isSelected = self->m_selectedPaths.contains(item.filePath);
            item.bytes = fi.isDir() ? 0 : fi.size();
            item.modified = fi.lastModified();
            item.suffix = fi.suffix();

            if (item.isDir) {
                item.fileSizeStr = QStringLiteral("Folder");
            } else {
                item.fileSizeStr = formatSize(fi.size());
            }

            QString relParent = QDir(rootPath).relativeFilePath(fi.path());
            if (relParent.isEmpty() || relParent == QLatin1String(".")) {
                item.fileDateStr = fi.lastModified().toString(QStringLiteral("MMM dd, yyyy"));
            } else {
                item.fileDateStr = relParent;
            }

            matches.append(item);
            count++;
        }

        if (!self || self->m_searchGeneration.load() != gen) return;

        QMetaObject::invokeMethod(self, [self, gen, matches = std::move(matches)]() mutable {
            if (!self || self->m_searchGeneration.load() != gen) return;

            self->beginResetModel();
            self->m_items = std::move(matches);
            self->m_dirCount = 0;
            self->m_fileCount = self->m_items.size();
            self->endResetModel();

            self->m_isSearching = false;
            self->m_searchResultCount = self->m_items.size();
            emit self->isSearchingChanged();
            emit self->searchResultCountChanged();
            emit self->countChanged();
        }, Qt::QueuedConnection);
    });
}

void RubyFileModel::cancelSearch() {
    m_searchGeneration++;
    if (m_isSearching) {
        m_isSearching = false;
        emit isSearchingChanged();
    }
}

void RubyFileModel::clearSearch() {
    m_searchGeneration++;
    m_isSearching = false;
    m_isSearchMode = false;
    m_searchResultCount = 0;
    emit isSearchingChanged();
    emit isSearchModeChanged();
    emit searchResultCountChanged();
    applyFilterAndSort();
}

// ── Inspection, Storage Metrics & File Preview (Index FilePreviewer) ───
void RubyFileModel::refreshStorageInfo() {
    QStorageInfo storage(m_currentDir);
    if (storage.isValid() && storage.isReady()) {
        qint64 total = storage.bytesTotal();
        qint64 avail = storage.bytesAvailable();
        m_availableStorageStr = formatSize(avail);
        m_totalStorageStr = formatSize(total);
        if (total > 0) {
            m_storageUsagePercent = static_cast<qreal>(total - avail) / static_cast<qreal>(total);
        } else {
            m_storageUsagePercent = 0.0;
        }
    } else {
        m_availableStorageStr = QStringLiteral("Unknown");
        m_totalStorageStr = QStringLiteral("Unknown");
        m_storageUsagePercent = 0.0;
    }
    emit storageInfoChanged();
}

QString RubyFileModel::calculateMd5(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash hash(QCryptographicHash::Md5);
    char buf[65536];
    while (!f.atEnd()) {
        qint64 bytesRead = f.read(buf, sizeof(buf));
        if (bytesRead > 0) hash.addData(QByteArrayView(buf, bytesRead));
    }
    return QString::fromLatin1(hash.result().toHex());
}

QString RubyFileModel::readFileSnippet(const QString& path, int maxLines) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    QTextStream in(&f);
    QString result;
    int lines = 0;
    while (!in.atEnd() && lines < maxLines) {
        result.append(in.readLine());
        result.append(QLatin1Char('\n'));
        lines++;
    }
    if (!in.atEnd()) {
        result.append(QStringLiteral("\n--- [Truncated: File has more lines] ---"));
    }
    return result;
}

QVariantMap RubyFileModel::getDetailedFileProperties(const QString& path) {
    QVariantMap map;
    QFileInfo fi(path);
    if (!fi.exists()) return map;

    map[QStringLiteral("name")] = fi.fileName();
    map[QStringLiteral("path")] = fi.absoluteFilePath();
    map[QStringLiteral("dir")] = fi.absolutePath();
    map[QStringLiteral("isDir")] = fi.isDir();
    map[QStringLiteral("fileType")] = detectFileType(fi);
    map[QStringLiteral("sizeBytes")] = fi.size();
    map[QStringLiteral("sizeStr")] = fi.isDir() ? QStringLiteral("Directory") : formatSize(fi.size());
    map[QStringLiteral("modifiedStr")] = fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    map[QStringLiteral("createdStr")] = fi.birthTime().isValid() ? fi.birthTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) : fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    map[QStringLiteral("isPinned")] = m_pinnedPaths.contains(fi.absoluteFilePath());

    QFile::Permissions p = fi.permissions();
    QString perm;
    perm += fi.isDir() ? 'd' : '-';
    perm += (p & QFile::ReadOwner) ? 'r' : '-';
    perm += (p & QFile::WriteOwner) ? 'w' : '-';
    perm += (p & QFile::ExeOwner) ? 'x' : '-';
    perm += (p & QFile::ReadGroup) ? 'r' : '-';
    perm += (p & QFile::WriteGroup) ? 'w' : '-';
    perm += (p & QFile::ExeGroup) ? 'x' : '-';
    perm += (p & QFile::ReadOther) ? 'r' : '-';
    perm += (p & QFile::WriteOther) ? 'w' : '-';
    perm += (p & QFile::ExeOther) ? 'x' : '-';
    map[QStringLiteral("permissions")] = perm;

    int oct = 0;
    if (p & QFile::ReadOwner)  oct |= 0400;
    if (p & QFile::WriteOwner) oct |= 0200;
    if (p & QFile::ExeOwner)   oct |= 0100;
    if (p & QFile::ReadGroup)  oct |= 0040;
    if (p & QFile::WriteGroup) oct |= 0020;
    if (p & QFile::ExeGroup)   oct |= 0010;
    if (p & QFile::ReadOther)  oct |= 0004;
    if (p & QFile::WriteOther) oct |= 0002;
    if (p & QFile::ExeOther)   oct |= 0001;
    map[QStringLiteral("permissionsOctal")] = QString::asprintf("%04o", oct);

    map[QStringLiteral("ownerRead")] = (p & QFile::ReadOwner) != 0;
    map[QStringLiteral("ownerWrite")] = (p & QFile::WriteOwner) != 0;
    map[QStringLiteral("ownerExecute")] = (p & QFile::ExeOwner) != 0;
    map[QStringLiteral("groupRead")] = (p & QFile::ReadGroup) != 0;
    map[QStringLiteral("groupWrite")] = (p & QFile::WriteGroup) != 0;
    map[QStringLiteral("groupExecute")] = (p & QFile::ExeGroup) != 0;
    map[QStringLiteral("otherRead")] = (p & QFile::ReadOther) != 0;
    map[QStringLiteral("otherWrite")] = (p & QFile::WriteOther) != 0;
    map[QStringLiteral("otherExecute")] = (p & QFile::ExeOther) != 0;

    map[QStringLiteral("owner")] = fi.owner();
    map[QStringLiteral("group")] = fi.group();

    if (fi.isDir()) {
        int files = 0;
        int dirs = 0;
        qint64 total = calculate_dir_size(fi.absoluteFilePath(), files, dirs);
        map[QStringLiteral("filesCount")] = files;
        map[QStringLiteral("dirsCount")] = dirs;
        map[QStringLiteral("totalItemsCount")] = files + dirs;
        map[QStringLiteral("recursiveSize")] = total;
        map[QStringLiteral("recursiveSizeStr")] = formatSize(total);
    }

    return map;
}

bool RubyFileModel::setFilePermission(const QString& path, const QString& user, const QString& perm, bool enable) {
    QFile file(path);
    if (!file.exists()) return false;
    QFile::Permissions p = file.permissions();
    QFile::Permission targetFlag = QFile::ReadOwner;

    QString u = user.trimmed().toLower();
    QString t = perm.trimmed().toLower();

    if (u == QLatin1String("owner")) {
        if (t == QLatin1String("read")) targetFlag = QFile::ReadOwner;
        else if (t == QLatin1String("write")) targetFlag = QFile::WriteOwner;
        else if (t == QLatin1String("execute")) targetFlag = QFile::ExeOwner;
    } else if (u == QLatin1String("group")) {
        if (t == QLatin1String("read")) targetFlag = QFile::ReadGroup;
        else if (t == QLatin1String("write")) targetFlag = QFile::WriteGroup;
        else if (t == QLatin1String("execute")) targetFlag = QFile::ExeGroup;
    } else if (u == QLatin1String("other") || u == QLatin1String("everyone")) {
        if (t == QLatin1String("read")) targetFlag = QFile::ReadOther;
        else if (t == QLatin1String("write")) targetFlag = QFile::WriteOther;
        else if (t == QLatin1String("execute")) targetFlag = QFile::ExeOther;
    }

    if (enable) {
        p |= targetFlag;
    } else {
        p &= ~targetFlag;
    }

    bool ok = file.setPermissions(p);
    if (ok) {
        emit statusMessage(QStringLiteral("Updated permissions: ") + QFileInfo(path).fileName());
    } else {
        emit statusMessage(QStringLiteral("Failed to update permissions (insufficient rights)"));
    }
    return ok;
}

} // namespace ruby::android

