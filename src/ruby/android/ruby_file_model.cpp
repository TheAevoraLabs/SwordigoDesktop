#include "ruby_file_model.h"
#include <QThreadPool>
#include <QGuiApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFile>
#include <QStandardPaths>
#include <QImage>
#include <QSettings>
#include "platform/zip_archive.h"
#include "platform/scl_parser.h"
#include "platform/pvr_loader.h"
#include "tools/filerift.h"
#include <algorithm>
#include <QDebug>
#if defined(Q_OS_ANDROID)
#  include <QJniObject>
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
    reload();
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
    return roles;
}

void RubyFileModel::navigateTo(const QString& path) {
    QDir dir(path);
    if (dir.exists()) {
        m_currentDir = dir;
        m_filter.clear();
        reload();
        emit currentPathChanged();
    }
}

void RubyFileModel::navigateUp() {
    if (m_currentDir.cdUp()) {
        m_filter.clear();
        reload();
        emit currentPathChanged();
    }
}

void RubyFileModel::refresh() {
    reload();
}

void RubyFileModel::setFilter(const QString& text) {
    m_filter = text.trimmed();
    reload();
}

bool RubyFileModel::deleteItem(const QString& path) {
    QFileInfo fi(path);
    bool success = false;
    if (fi.isDir()) {
        QDir dir(path);
        success = dir.removeRecursively();
    } else {
        success = QFile::remove(path);
    }
    if (success) {
        m_pinnedPaths.remove(path);
        reload();
    }
    return success;
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
        reload();
    }
    return success;
}

bool RubyFileModel::createDirectory(const QString& name) {
    if (name.trimmed().isEmpty()) return false;
    bool success = m_currentDir.mkdir(name.trimmed());
    if (success) {
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
    reload();
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
    reload();
    emit categoryChanged();
}

void RubyFileModel::setSortMode(const QString& mode) {
    const QString m = mode.toLower();
    if (m == m_sortMode) return;
    m_sortMode = m;
    reload();
    emit sortChanged();
}

void RubyFileModel::setSortAscending(bool ascending) {
    if (ascending == m_sortAscending) return;
    m_sortAscending = ascending;
    reload();
    emit sortChanged();
}

void RubyFileModel::setShowHidden(bool show) {
    if (show == m_showHidden) return;
    m_showHidden = show;
    reload();
    emit showHiddenChanged();
}

bool RubyFileModel::passesCategory(const QFileInfo& fi, const QString& cat) {
    if (cat.isEmpty() || cat == QLatin1String("all")) return true;

    const QString ext = fi.suffix().toLower();
    if (cat == QLatin1String("scene"))    return ext == QLatin1String("scene");
    if (cat == QLatin1String("scl"))      return ext == QLatin1String("scl");
    if (cat == QLatin1String("models"))   return ext == QLatin1String("pod") || ext == QLatin1String("glb");
    if (cat == QLatin1String("archives")) return ext == QLatin1String("zip") || ext == QLatin1String("apk");
    if (cat == QLatin1String("lua"))      return ext == QLatin1String("lua");
    if (cat == QLatin1String("audio"))    return ext == QLatin1String("wav") || ext == QLatin1String("ogg") || ext == QLatin1String("mp3");
    return true;
}

void RubyFileModel::reload() {
    uint64_t gen = ++m_scanGeneration;
    m_isLoading = true;
    emit isLoadingChanged();

    QDir targetDir = m_currentDir;
    QString filter = m_filter;
    QString cat = m_category;
    bool showHidden = m_showHidden;
    QSet<QString> pinned = m_pinnedPaths;
    QString sortMode = m_sortMode;
    bool sortAscending = m_sortAscending;

    QThreadPool::globalInstance()->start([this, gen, targetDir, filter, cat, showHidden, pinned, sortMode, sortAscending]() {
        QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
        if (showHidden) filters |= QDir::Hidden;

        QFileInfoList entries = targetDir.entryInfoList(filters);

        if (m_scanGeneration.load() != gen) return;

        QVector<FileItem> items;
        items.reserve(entries.size());
        int dCount = 0;
        int fCount = 0;

        for (const auto& fi : entries) {
            if (!filter.isEmpty()) {
                if (!fi.fileName().contains(filter, Qt::CaseInsensitive)) {
                    continue;
                }
            }

            if (!fi.isDir() && !passesCategory(fi, cat)) {
                continue;
            }

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

        if (m_scanGeneration.load() != gen) return;

        std::sort(items.begin(), items.end(), [sortMode, sortAscending](const FileItem& a, const FileItem& b) {
            if (a.isPinned != b.isPinned) return a.isPinned > b.isPinned;
            if (a.isDir != b.isDir) return a.isDir;

            bool less = false;
            if (sortMode == QLatin1String("date")) {
                less = a.modified < b.modified;
            } else if (sortMode == QLatin1String("size")) {
                less = a.bytes < b.bytes;
            } else if (sortMode == QLatin1String("type")) {
                less = a.suffix.localeAwareCompare(b.suffix) < 0;
            } else { // "name"
                less = a.fileName.localeAwareCompare(b.fileName) < 0;
            }
            return sortAscending ? less : !less;
        });

        bool isRes = checkSwordigoResourceRoot(targetDir.absolutePath());

        for (const auto& item : items) {
            if (item.isDir) ++dCount; else ++fCount;
        }

        if (m_scanGeneration.load() != gen) return;

        QMetaObject::invokeMethod(this, [this, gen, items = std::move(items), dCount, fCount, isRes]() mutable {
            if (m_scanGeneration.load() != gen) return;
            beginResetModel();
            m_items = std::move(items);
            m_dirCount = dCount;
            m_fileCount = fCount;
            endResetModel();
            m_isLoading = false;
            bool resChanged = (m_isResourceRoot != isRes);
            m_isResourceRoot = isRes;
            emit isLoadingChanged();
            emit countChanged();
            if (resChanged) emit resourceRootChanged();
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

void RubyFileModel::launchGame() {
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
    QFileInfo fi(scenePath);
    if (!canStartSceneInGame(scenePath)) {
        emit statusMessage(QStringLiteral("Not a scene file: ") + scenePath);
        return false;
    }

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

static bool zip_folder_recursive_impl(const QString& root_dir, const QString& current_dir, std::vector<zip::OutEntry>& entries) {
    QDir dir(current_dir);
    QFileInfoList list = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const auto& fi : list) {
        if (fi.isDir()) {
            if (!zip_folder_recursive_impl(root_dir, fi.absoluteFilePath(), entries)) {
                return false;
            }
        } else {
            QFile f(fi.absoluteFilePath());
            if (f.open(QIODevice::ReadOnly)) {
                QByteArray bytes = f.readAll();
                f.close();
                QString rel_path = QDir(root_dir).relativeFilePath(fi.absoluteFilePath());
                rel_path.replace(QLatin1Char('\\'), QLatin1Char('/'));
                zip::OutEntry e;
                e.name = rel_path.toStdString();
                e.data = std::string(bytes.constData(), bytes.size());
                e.method = 8; // Deflate
                entries.push_back(std::move(e));
            }
        }
    }
    return true;
}

void RubyFileModel::cutItem(const QString& path) {
    m_clipboardPath = path;
    m_clipboardIsCut = true;
    emit clipboardChanged();
    emit statusMessage(QStringLiteral("Cut: %1").arg(QFileInfo(path).fileName()));
}

void RubyFileModel::copyItem(const QString& path) {
    m_clipboardPath = path;
    m_clipboardIsCut = false;
    emit clipboardChanged();
    emit statusMessage(QStringLiteral("Copied: %1").arg(QFileInfo(path).fileName()));
}

void RubyFileModel::clearClipboard() {
    m_clipboardPath.clear();
    m_clipboardIsCut = false;
    emit clipboardChanged();
}

bool RubyFileModel::paste() {
    if (m_clipboardPath.isEmpty()) return false;

    QFileInfo src(m_clipboardPath);
    if (!src.exists()) {
        emit statusMessage(QStringLiteral("Source item no longer exists!"));
        clearClipboard();
        return false;
    }

    const QString dest = m_currentDir.filePath(src.fileName());
    if (dest == m_clipboardPath) {
        emit statusMessage(QStringLiteral("Cannot paste into same location!"));
        return false;
    }

    if (m_clipboardIsCut) {
        // Move / Rename
        bool moved = QFile::rename(m_clipboardPath, dest);
        if (!moved) {
            // Cross-filesystem move fallback
            if (src.isDir()) {
                if (copy_dir_recursive(m_clipboardPath, dest)) {
                    QDir(m_clipboardPath).removeRecursively();
                    moved = true;
                }
            } else {
                if (QFile::exists(dest)) QFile::remove(dest);
                if (QFile::copy(m_clipboardPath, dest)) {
                    QFile::remove(m_clipboardPath);
                    moved = true;
                }
            }
        }
        if (moved) {
            emit statusMessage(QStringLiteral("Moved: %1").arg(src.fileName()));
            clearClipboard();
            reload();
            return true;
        } else {
            emit statusMessage(QStringLiteral("Error moving item!"));
            return false;
        }
    } else {
        // Copy
        bool copied = false;
        if (src.isDir()) {
            copied = copy_dir_recursive(m_clipboardPath, dest);
        } else {
            if (QFile::exists(dest)) QFile::remove(dest);
            copied = QFile::copy(m_clipboardPath, dest);
        }
        if (copied) {
            emit statusMessage(QStringLiteral("Copied: %1").arg(src.fileName()));
            reload();
            return true;
        } else {
            emit statusMessage(QStringLiteral("Error copying item!"));
            return false;
        }
    }
}

bool RubyFileModel::compressToZip(const QString& targetPath, const QString& outZipPath) {
    QFileInfo fi(targetPath);
    if (!fi.exists()) {
        emit statusMessage(QStringLiteral("Target item does not exist: %1").arg(targetPath));
        return false;
    }

    QString destZip = outZipPath;
    if (destZip.isEmpty()) {
        destZip = fi.isDir() ? (targetPath + QStringLiteral(".zip")) : (fi.path() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral(".zip"));
    }

    std::vector<zip::OutEntry> entries;
    if (fi.isDir()) {
        if (!zip_folder_recursive_impl(targetPath, targetPath, entries)) {
            emit statusMessage(QStringLiteral("Error reading folder for compression"));
            return false;
        }
    } else {
        QFile f(targetPath);
        if (!f.open(QIODevice::ReadOnly)) {
            emit statusMessage(QStringLiteral("Cannot read file for compression"));
            return false;
        }
        QByteArray bytes = f.readAll();
        f.close();
        zip::OutEntry e;
        e.name = fi.fileName().toStdString();
        e.data = std::string(bytes.constData(), bytes.size());
        e.method = 8;
        entries.push_back(std::move(e));
    }

    emit statusMessage(QStringLiteral("Compressing to %1...").arg(QFileInfo(destZip).fileName()));
    bool ok = zip::write_archive(destZip.toStdString(), entries);
    if (ok) {
        emit statusMessage(QStringLiteral("Created zip: %1 (%2 entries)").arg(QFileInfo(destZip).fileName()).arg(entries.size()));
        reload();
    } else {
        emit statusMessage(QStringLiteral("Failed to write zip archive!"));
    }
    return ok;
}

bool RubyFileModel::extractArchive(const QString& archivePath, bool toSubfolder) {
    QFileInfo fi(archivePath);
    if (!fi.exists()) return false;

    QString destFolder = m_currentDir.absolutePath();
    if (toSubfolder) {
        destFolder = m_currentDir.filePath(fi.completeBaseName());
        QDir().mkpath(destFolder);
    }

    emit statusMessage(QStringLiteral("Extracting %1...").arg(fi.fileName()));
    auto result = zip::extract_all(archivePath.toStdString(), destFolder.toStdString());
    if (result.ok) {
        emit statusMessage(QStringLiteral("Extracted %1 files").arg(result.extracted));
        reload();
        return true;
    } else {
        emit statusMessage(QStringLiteral("Extraction failed: %1").arg(QString::fromStdString(result.error)));
        return false;
    }
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

} // namespace ruby::android

