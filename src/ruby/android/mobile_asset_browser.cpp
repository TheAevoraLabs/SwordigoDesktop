// ============================================================================
// mobile_asset_browser.cpp — ZArchiver-Grade Mobile File & Asset Manager
// ============================================================================

#include "mobile_asset_browser.h"
#include "platform/zip_archive.h"

#include <QFileInfo>
#include <QMenu>
#include <QDialog>
#include <QCursor>
#include <QScrollArea>
#include <QInputDialog>
#include <QMessageBox>
#include <QDateTime>
#include <QStandardPaths>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <algorithm>

namespace ruby::android {

MobileAssetBrowser::MobileAssetBrowser(QWidget* parent)
    : QWidget(parent)
{
    // Default initial location: /sdcard / Primary external storage if exists, else assets or current dir
    QDir primary_storage(QStringLiteral("/storage/emulated/0"));
    if (primary_storage.exists()) {
        m_current_dir = primary_storage;
    } else {
        QDir sdcard(QStringLiteral("/sdcard"));
        if (sdcard.exists()) {
            m_current_dir = sdcard;
        } else {
            m_current_dir = QDir::current();
        }
    }

    setup_ui();
    populate_files();
}

void MobileAssetBrowser::setup_ui() {
    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(10, 10, 10, 6);
    root_layout->setSpacing(8);

    // 1. Top Navigation Bar: [Roots ≡] [Path Input Bar] [⬆ Up] [Menu ⁝]
    auto* nav_bar = new QHBoxLayout();
    nav_bar->setSpacing(6);

    m_btn_roots = new QPushButton(QStringLiteral("Drives"), this);
    m_btn_roots->setFixedHeight(36);
    m_btn_roots->setStyleSheet(
        QStringLiteral("background: #242938; color: #8AB4F8; border: 1px solid #3A4460; "
                       "border-radius: 6px; font-size: 12px; font-weight: bold; padding: 0 10px;"));

    m_path_edit = new QLineEdit(m_current_dir.absolutePath(), this);
    m_path_edit->setFixedHeight(36);
    m_path_edit->setStyleSheet(
        QStringLiteral("background: #141720; border: 1px solid #2B3349; border-radius: 6px; "
                       "color: #D8E2F5; padding: 2px 8px; font-size: 12px;"));

    m_btn_up = new QPushButton(QStringLiteral("Up"), this);
    m_btn_up->setFixedSize(55, 36);
    m_btn_up->setStyleSheet(
        QStringLiteral("background: #242938; color: #FFFFFF; border: 1px solid #3A4460; "
                       "border-radius: 6px; font-size: 12px; font-weight: bold;"));

    m_btn_menu = new QPushButton(QStringLiteral("Actions"), this);
    m_btn_menu->setFixedHeight(36);
    m_btn_menu->setStyleSheet(
        QStringLiteral("background: #2A4065; color: #FFFFFF; border: 1px solid #3C5A8C; "
                       "border-radius: 6px; font-size: 12px; font-weight: bold; padding: 0 10px;"));

    nav_bar->addWidget(m_btn_roots);
    nav_bar->addWidget(m_path_edit, 1);
    nav_bar->addWidget(m_btn_up);
    nav_bar->addWidget(m_btn_menu);
    root_layout->addLayout(nav_bar);

    // 2. Search & Filter Bar: [Search] [Paste (if clipboard)] [Sort By]
    auto* sub_bar = new QHBoxLayout();
    sub_bar->setSpacing(6);

    m_search_edit = new QLineEdit(this);
    m_search_edit->setPlaceholderText(QStringLiteral("Filter files..."));
    m_search_edit->setFixedHeight(34);
    m_search_edit->setStyleSheet(
        QStringLiteral("background: #141720; border: 1px solid #282E42; border-radius: 6px; "
                       "color: #FFFFFF; padding: 2px 8px; font-size: 12px;"));

    m_btn_paste = new QPushButton(QStringLiteral("Paste"), this);
    m_btn_paste->setFixedSize(70, 34);
    m_btn_paste->setEnabled(false);
    m_btn_paste->setStyleSheet(
        QStringLiteral("QPushButton { background: #1E4630; color: #7CE09A; border: 1px solid #2B6B48; "
                       "border-radius: 6px; font-size: 11px; font-weight: bold; } "
                       "QPushButton:disabled { background: #181D26; color: #4B5263; border: 1px solid #252A35; }"));

    m_btn_sort = new QPushButton(QStringLiteral("⇅ Sort"), this);
    m_btn_sort->setFixedSize(65, 34);
    m_btn_sort->setStyleSheet(
        QStringLiteral("background: #202636; color: #A0B5D8; border: 1px solid #303B52; "
                       "border-radius: 6px; font-size: 11px; font-weight: bold;"));

    sub_bar->addWidget(m_search_edit, 1);
    sub_bar->addWidget(m_btn_paste);
    sub_bar->addWidget(m_btn_sort);
    root_layout->addLayout(sub_bar);

    // 3. Category Filter Chips
    auto* chip_scroll = new QScrollArea(this);
    chip_scroll->setFixedHeight(38);
    chip_scroll->setWidgetResizable(true);
    chip_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chip_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    chip_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));

    auto* chip_container = new QWidget(chip_scroll);
    auto* chip_layout = new QHBoxLayout(chip_container);
    chip_layout->setContentsMargins(0, 0, 0, 0);
    chip_layout->setSpacing(6);

    auto make_chip = [this, chip_layout, chip_container](const QString& label, const QString& cat) {
        auto* b = new QPushButton(label, chip_container);
        b->setFixedHeight(28);
        b->setStyleSheet(
            QStringLiteral("background: #1B202D; color: #8CB4E6; border: 1px solid #2A3348; "
                           "border-radius: 14px; padding: 0 10px; font-size: 11px; font-weight: bold;"));
        connect(b, &QPushButton::clicked, this, [this, cat]() {
            m_filter_category = cat;
            populate_files();
        });
        chip_layout->addWidget(b);
    };

    make_chip(QStringLiteral("All Files"), QStringLiteral("all"));
    make_chip(QStringLiteral("Scenes (.scene)"), QStringLiteral("scene"));
    make_chip(QStringLiteral("Templates (.scl)"), QStringLiteral("scl"));
    make_chip(QStringLiteral("Models (.pod/.glb)"), QStringLiteral("models"));
    make_chip(QStringLiteral("Archives (.zip/.apk)"), QStringLiteral("archives"));
    make_chip(QStringLiteral("Scripts (.lua)"), QStringLiteral("lua"));
    chip_layout->addStretch();
    chip_scroll->setWidget(chip_container);
    root_layout->addWidget(chip_scroll);

    // 4. File List View
    m_file_list = new QListWidget(this);
    m_file_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_file_list->setStyleSheet(
        QStringLiteral("QListWidget { background: #12141C; border: 1px solid #222738; "
                       "border-radius: 8px; color: #D8E0F0; padding: 4px; } "
                       "QListWidget::item { height: 50px; padding: 4px 8px; border-bottom: 1px solid #1A1E2C; } "
                       "QListWidget::item:selected { background: #26466E; color: #FFFFFF; border-radius: 6px; }"));
    root_layout->addWidget(m_file_list, 1);

    // 5. Status / File Count Bar
    m_status_lbl = new QLabel(this);
    m_status_lbl->setStyleSheet(QStringLiteral("color: #72809C; font-size: 11px; padding: 0 4px;"));
    root_layout->addWidget(m_status_lbl);

    // Connections
    connect(m_btn_roots, &QPushButton::clicked, this, &MobileAssetBrowser::on_storage_roots_menu);
    connect(m_btn_up, &QPushButton::clicked, this, &MobileAssetBrowser::on_navigate_up);
    connect(m_path_edit, &QLineEdit::returnPressed, this, &MobileAssetBrowser::on_path_entered);
    connect(m_btn_menu, &QPushButton::clicked, this, [this]() {
        QMenu menu(this);
        menu.addAction(QStringLiteral("New Folder"), this, &MobileAssetBrowser::on_new_folder);
        menu.addAction(QStringLiteral("New Scene File (.scene)"), this, &MobileAssetBrowser::on_new_file);
        menu.addSeparator();
        auto* act_hidden = menu.addAction(QStringLiteral("Show Hidden Files"));
        act_hidden->setCheckable(true);
        act_hidden->setChecked(m_show_hidden);
        connect(act_hidden, &QAction::toggled, this, [this](bool checked) {
            m_show_hidden = checked;
            populate_files();
        });
        menu.addAction(QStringLiteral("Refresh"), this, &MobileAssetBrowser::refresh);
        menu.exec(m_btn_menu->mapToGlobal(QPoint(0, m_btn_menu->height())));
    });
    connect(m_btn_sort, &QPushButton::clicked, this, &MobileAssetBrowser::on_sort_menu);
    connect(m_btn_paste, &QPushButton::clicked, this, &MobileAssetBrowser::on_clipboard_paste);
    connect(m_search_edit, &QLineEdit::textChanged, this, &MobileAssetBrowser::populate_files);
    connect(m_file_list, &QListWidget::itemClicked, this, &MobileAssetBrowser::on_item_clicked);
    connect(m_file_list, &QListWidget::customContextMenuRequested, this, &MobileAssetBrowser::on_item_long_press);
}

bool MobileAssetBrowser::copy_dir_recursive(const QString& src, const QString& dst) {
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

bool MobileAssetBrowser::zip_folder_recursive(const QString& root_dir, const QString& current_dir, std::vector<zip::OutEntry>& entries) {
    QDir dir(current_dir);
    QFileInfoList list = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const auto& fi : list) {
        if (fi.isDir()) {
            if (!zip_folder_recursive(root_dir, fi.absoluteFilePath(), entries)) {
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
                e.method = 8;
                entries.push_back(std::move(e));
            }
        }
    }
    return true;
}

qint64 MobileAssetBrowser::calculate_dir_size(const QString& dir_path, int& out_files, int& out_dirs) {
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

std::vector<QString> MobileAssetBrowser::detect_storage_roots() {
    std::vector<QString> roots;

    // Standard Android Internal Storage
    if (QDir(QStringLiteral("/storage/emulated/0")).exists()) {
        roots.push_back(QStringLiteral("/storage/emulated/0"));
    }
    if (QDir(QStringLiteral("/sdcard")).exists() && !roots.empty() && roots.front() != QStringLiteral("/sdcard")) {
        roots.push_back(QStringLiteral("/sdcard"));
    }

    // Common Android storage directories
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

    // Secondary / Removable Storage Mounts (/storage/XXXX-XXXX)
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

    // Media RW removable mounts (OTG drives / SD)
    QDir media_rw(QStringLiteral("/mnt/media_rw"));
    if (media_rw.exists()) {
        QFileInfoList entries = media_rw.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto& entry : entries) {
            roots.push_back(entry.absoluteFilePath());
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

    // Current working directory / project root
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

    return unique_roots;
}

void MobileAssetBrowser::on_storage_roots_menu() {
    QMenu menu(this);
    auto roots = detect_storage_roots();

    for (const auto& r : roots) {
        QString label = r;
        if (r == QStringLiteral("/storage/emulated/0") || r == QStringLiteral("/sdcard")) {
            label = QStringLiteral("Internal Shared Storage (/sdcard)");
        } else if (r.endsWith(QStringLiteral("/Download"))) {
            label = QStringLiteral("Downloads Folder");
        } else if (r.endsWith(QStringLiteral("/Documents"))) {
            label = QStringLiteral("Documents Folder");
        } else if (r.startsWith(QStringLiteral("/storage/"))) {
            label = QStringLiteral("MicroSD / USB Storage: ") + QFileInfo(r).fileName();
        } else if (r == QStringLiteral("/")) {
            label = QStringLiteral("Root Filesystem (/)");
        }

        menu.addAction(label, this, [this, r]() {
            set_root_path(r);
        });
    }

    menu.exec(m_btn_roots->mapToGlobal(QPoint(0, m_btn_roots->height())));
}

void MobileAssetBrowser::on_sort_menu() {
    QMenu menu(this);
    menu.addAction(QStringLiteral("Name (A to Z)"), this, [this]() {
        m_sort_mode = SortMode::Name; m_sort_ascending = true; populate_files();
    });
    menu.addAction(QStringLiteral("Name (Z to A)"), this, [this]() {
        m_sort_mode = SortMode::Name; m_sort_ascending = false; populate_files();
    });
    menu.addAction(QStringLiteral("Date (Newest First)"), this, [this]() {
        m_sort_mode = SortMode::Date; m_sort_ascending = false; populate_files();
    });
    menu.addAction(QStringLiteral("Date (Oldest First)"), this, [this]() {
        m_sort_mode = SortMode::Date; m_sort_ascending = true; populate_files();
    });
    menu.addAction(QStringLiteral("Size (Largest First)"), this, [this]() {
        m_sort_mode = SortMode::Size; m_sort_ascending = false; populate_files();
    });
    menu.addAction(QStringLiteral("Type (.ext)"), this, [this]() {
        m_sort_mode = SortMode::Type; m_sort_ascending = true; populate_files();
    });
    menu.exec(m_btn_sort->mapToGlobal(QPoint(0, m_btn_sort->height())));
}

void MobileAssetBrowser::set_root_path(const QString& dir_path) {
    QDir d(dir_path);
    if (d.exists()) {
        m_current_dir = d;
        m_path_edit->setText(m_current_dir.absolutePath());
        populate_files();
    }
}

void MobileAssetBrowser::on_navigate_up() {
    if (m_current_dir.cdUp()) {
        m_path_edit->setText(m_current_dir.absolutePath());
        populate_files();
    }
}

void MobileAssetBrowser::on_path_entered() {
    const QString target = m_path_edit->text().trimmed();
    QDir d(target);
    if (d.exists()) {
        m_current_dir = d;
        populate_files();
    } else {
        emit statusMessage(QStringLiteral("Path does not exist: %1").arg(target));
        m_path_edit->setText(m_current_dir.absolutePath());
    }
}

void MobileAssetBrowser::refresh() {
    m_current_dir.refresh();
    m_path_edit->setText(m_current_dir.absolutePath());
    populate_files();
}

QString MobileAssetBrowser::format_file_size(qint64 bytes) const {
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QStringLiteral("%1 KB").arg(bytes / 1024);
    if (bytes < 1024 * 1024 * 1024) return QStringLiteral("%1 MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
    return QStringLiteral("%1 GB").arg(QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2));
}

void MobileAssetBrowser::populate_files() {
    m_file_list->clear();

    const QString search_query = m_search_edit->text().trimmed().toLower();
    QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
    if (m_show_hidden) filters |= QDir::Hidden;

    QFileInfoList entries = m_current_dir.entryInfoList(filters);

    // Sort entries
    std::sort(entries.begin(), entries.end(), [this](const QFileInfo& a, const QFileInfo& b) {
        if (a.isDir() != b.isDir()) return a.isDir(); // Dirs always on top

        const QFileInfo& first = m_sort_ascending ? a : b;
        const QFileInfo& second = m_sort_ascending ? b : a;

        if (m_sort_mode == SortMode::Name) {
            int cmp = first.fileName().localeAwareCompare(second.fileName());
            if (cmp != 0) return cmp < 0;
        } else if (m_sort_mode == SortMode::Date) {
            if (first.lastModified() != second.lastModified()) {
                return first.lastModified() < second.lastModified();
            }
        } else if (m_sort_mode == SortMode::Size) {
            if (first.size() != second.size()) {
                return first.size() < second.size();
            }
        } else if (m_sort_mode == SortMode::Type) {
            int cmp = first.suffix().localeAwareCompare(second.suffix());
            if (cmp != 0) return cmp < 0;
        }

        // Tie-breaker: always sort by absoluteFilePath ascending for deterministic strict weak ordering
        return a.absoluteFilePath() < b.absoluteFilePath();
    });

    int dir_count = 0;
    int file_count = 0;

    for (const auto& entry : entries) {
        const QString name = entry.fileName();
        if (!search_query.isEmpty() && !name.toLower().contains(search_query)) continue;

        if (entry.isDir()) {
            dir_count++;
            auto* item = new QListWidgetItem(QStringLiteral("%1/").arg(name), m_file_list);
            item->setData(Qt::UserRole, entry.absoluteFilePath());
            item->setData(Qt::UserRole + 1, QStringLiteral("dir"));
            item->setForeground(QColor(130, 185, 250));
        } else {
            const QString ext = entry.suffix().toLower();

            // Filter category
            if (m_filter_category == QStringLiteral("scene") && ext != QStringLiteral("scene")) continue;
            if (m_filter_category == QStringLiteral("scl") && ext != QStringLiteral("scl")) continue;
            if (m_filter_category == QStringLiteral("models") && ext != QStringLiteral("pod") && ext != QStringLiteral("glb")) continue;
            if (m_filter_category == QStringLiteral("archives") && ext != QStringLiteral("zip") && ext != QStringLiteral("apk")) continue;
            if (m_filter_category == QStringLiteral("lua") && ext != QStringLiteral("lua")) continue;

            file_count++;
            QColor color(220, 225, 235);

            if (ext == QStringLiteral("scene")) {
                color = QColor(255, 175, 80);
            } else if (ext == QStringLiteral("scl")) {
                color = QColor(140, 215, 110);
            } else if (ext == QStringLiteral("pod") || ext == QStringLiteral("glb")) {
                color = QColor(100, 195, 255);
            } else if (ext == QStringLiteral("zip") || ext == QStringLiteral("apk")) {
                color = QColor(245, 215, 95);
            } else if (ext == QStringLiteral("lua")) {
                color = QColor(220, 140, 255);
            } else if (ext == QStringLiteral("png") || ext == QStringLiteral("pvr")) {
                color = QColor(255, 125, 165);
            }

            const QString size_str = format_file_size(entry.size());
            const QString date_str = entry.lastModified().toString(QStringLiteral("MM/dd hh:mm"));
            auto* item = new QListWidgetItem(QStringLiteral("%1    [%2  •  %3]").arg(name).arg(size_str).arg(date_str), m_file_list);
            item->setData(Qt::UserRole, entry.absoluteFilePath());
            item->setData(Qt::UserRole + 1, ext);
            item->setForeground(color);
        }
    }

    m_status_lbl->setText(QStringLiteral("%1 Folders, %2 Files in %3").arg(dir_count).arg(file_count).arg(m_current_dir.dirName()));
}

void MobileAssetBrowser::on_item_clicked(QListWidgetItem* item) {
    if (!item) return;

    const QString path = item->data(Qt::UserRole).toString();
    const QString kind = item->data(Qt::UserRole + 1).toString();

    if (kind == QStringLiteral("dir")) {
        m_current_dir.cd(path);
        m_path_edit->setText(m_current_dir.absolutePath());
        populate_files();
        return;
    }

    if (kind == QStringLiteral("scene")) {
        emit sceneOpenRequested(path, true); // Visual 3D Mode
    } else if (kind == QStringLiteral("pod") || kind == QStringLiteral("glb")) {
        emit modelOpenRequested(path);
    } else if (kind == QStringLiteral("zip") || kind == QStringLiteral("apk")) {
        // ZArchiver archive actions
        QMenu menu(this);
        menu.setTitle(QFileInfo(path).fileName());
        menu.addAction(QStringLiteral("View Archive Contents"), this, [this, path]() {
            on_view_archive(path);
        });
        menu.addAction(QStringLiteral("Extract to Folder (./%1/)").arg(QFileInfo(path).baseName()), this, [this, path]() {
            on_extract_archive(path, true);
        });
        menu.addAction(QStringLiteral("Extract Archive Here"), this, [this, path]() {
            on_extract_archive(path, false);
        });
        menu.exec(QCursor::pos());
    } else {
        emit codeOpenRequested(path);
    }
}

void MobileAssetBrowser::on_item_long_press(const QPoint& pos) {
    auto* item = m_file_list->itemAt(pos);
    if (!item) return;

    const QString path = item->data(Qt::UserRole).toString();
    const QString kind = item->data(Qt::UserRole + 1).toString();
    const QFileInfo fi(path);

    QMenu menu(this);
    menu.setTitle(fi.fileName());

    if (kind == QStringLiteral("dir")) {
        menu.addAction(QStringLiteral("Open Folder"), this, [this, path]() {
            m_current_dir.cd(path);
            m_path_edit->setText(m_current_dir.absolutePath());
            populate_files();
        });
        menu.addAction(QStringLiteral("Compress to ZIP (Recursive)"), this, [this, path]() {
            on_compress_zip(path);
        });
    } else {
        if (kind == QStringLiteral("scene")) {
            menu.addAction(QStringLiteral("Open in 3D Viewport"), this, [this, path]() {
                emit sceneOpenRequested(path, true);
            });
            menu.addAction(QStringLiteral("Open in FileRift Code Editor"), this, [this, path]() {
                emit sceneOpenRequested(path, false);
            });
        } else if (kind == QStringLiteral("pod") || kind == QStringLiteral("glb")) {
            menu.addAction(QStringLiteral("View 3D Model"), this, [this, path]() {
                emit modelOpenRequested(path);
            });
        } else if (kind == QStringLiteral("zip") || kind == QStringLiteral("apk")) {
            menu.addAction(QStringLiteral("View Archive Contents"), this, [this, path]() {
                on_view_archive(path);
            });
            menu.addAction(QStringLiteral("Extract to Folder (./%1/)").arg(fi.baseName()), this, [this, path]() {
                on_extract_archive(path, true);
            });
            menu.addAction(QStringLiteral("Extract Archive Here"), this, [this, path]() {
                on_extract_archive(path, false);
            });
        } else {
            menu.addAction(QStringLiteral("Open in Code Editor"), this, [this, path]() {
                emit codeOpenRequested(path);
            });
        }
        menu.addAction(QStringLiteral("Compress to ZIP"), this, [this, path]() {
            on_compress_zip(path);
        });
    }

    menu.addSeparator();
    menu.addAction(QStringLiteral("Cut"), this, [this, path]() {
        m_clipboard.path = path;
        m_clipboard.is_cut = true;
        m_btn_paste->setEnabled(true);
        emit statusMessage(QStringLiteral("Cut: %1").arg(QFileInfo(path).fileName()));
    });
    menu.addAction(QStringLiteral("Copy"), this, [this, path]() {
        m_clipboard.path = path;
        m_clipboard.is_cut = false;
        m_btn_paste->setEnabled(true);
        emit statusMessage(QStringLiteral("Copied: %1").arg(QFileInfo(path).fileName()));
    });
    menu.addAction(QStringLiteral("Rename"), this, [this, path]() {
        on_rename_item(path);
    });
    menu.addAction(QStringLiteral("Delete"), this, [this, path]() {
        on_delete_item(path);
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Properties"), this, [this, path]() {
        on_show_properties(path);
    });

    menu.exec(m_file_list->mapToGlobal(pos));
}

void MobileAssetBrowser::on_new_folder() {
    bool ok = false;
    QString folder_name = QInputDialog::getText(
        this, QStringLiteral("New Folder"),
        QStringLiteral("Folder name:"), QLineEdit::Normal,
        QStringLiteral("New_Folder"), &ok);

    if (ok && !folder_name.trimmed().isEmpty()) {
        if (m_current_dir.mkdir(folder_name.trimmed())) {
            refresh();
            emit statusMessage(QStringLiteral("Created folder: %1").arg(folder_name));
        } else {
            emit statusMessage(QStringLiteral("Failed to create folder!"));
        }
    }
}

void MobileAssetBrowser::on_new_file() {
    bool ok = false;
    QString file_name = QInputDialog::getText(
        this, QStringLiteral("New File"),
        QStringLiteral("File name (e.g. level.scene, test.scl, main.lua, config.json):"),
        QLineEdit::Normal, QStringLiteral("custom_scene.scene"), &ok);

    if (ok && !file_name.trimmed().isEmpty()) {
        const QString target_path = m_current_dir.filePath(file_name.trimmed());
        QFile f(target_path);
        if (f.open(QIODevice::WriteOnly)) {
            f.close();
            refresh();
            emit statusMessage(QStringLiteral("Created file: %1").arg(file_name));
            if (file_name.endsWith(QStringLiteral(".scene"))) {
                emit sceneOpenRequested(target_path, true);
            } else {
                emit codeOpenRequested(target_path);
            }
        } else {
            emit statusMessage(QStringLiteral("Failed to create file!"));
        }
    }
}

void MobileAssetBrowser::on_clipboard_paste() {
    if (m_clipboard.path.isEmpty()) return;

    QFileInfo src_info(m_clipboard.path);
    if (!src_info.exists()) {
        emit statusMessage(QStringLiteral("Source item no longer exists!"));
        m_clipboard.path.clear();
        m_btn_paste->setEnabled(false);
        return;
    }

    const QString dest_path = m_current_dir.filePath(src_info.fileName());
    if (dest_path == m_clipboard.path) {
        emit statusMessage(QStringLiteral("Cannot paste into same location!"));
        return;
    }

    if (m_clipboard.is_cut) {
        // Move / Rename
        bool moved = QFile::rename(m_clipboard.path, dest_path);
        if (!moved) {
            // Cross-filesystem move
            if (src_info.isDir()) {
                if (copy_dir_recursive(m_clipboard.path, dest_path)) {
                    QDir(m_clipboard.path).removeRecursively();
                    moved = true;
                }
            } else {
                if (QFile::copy(m_clipboard.path, dest_path)) {
                    QFile::remove(m_clipboard.path);
                    moved = true;
                }
            }
        }

        if (moved) {
            emit statusMessage(QStringLiteral("Moved: %1").arg(src_info.fileName()));
            m_clipboard.path.clear();
            m_btn_paste->setEnabled(false);
            refresh();
        } else {
            emit statusMessage(QStringLiteral("Error moving file!"));
        }
    } else {
        // Copy
        bool copied = false;
        if (src_info.isDir()) {
            copied = copy_dir_recursive(m_clipboard.path, dest_path);
        } else {
            if (QFile::exists(dest_path)) QFile::remove(dest_path);
            copied = QFile::copy(m_clipboard.path, dest_path);
        }

        if (copied) {
            emit statusMessage(QStringLiteral("Copied: %1").arg(src_info.fileName()));
            refresh();
        } else {
            emit statusMessage(QStringLiteral("Error copying file!"));
        }
    }
}

void MobileAssetBrowser::on_extract_archive(const QString& archive_path, bool to_subfolder) {
    QFileInfo fi(archive_path);
    QString dest_folder = m_current_dir.absolutePath();
    if (to_subfolder) {
        dest_folder = m_current_dir.filePath(fi.baseName());
        QDir().mkpath(dest_folder);
    }

    emit statusMessage(QStringLiteral("Extracting %1...").arg(fi.fileName()));
    auto result = zip::extract_all(archive_path.toStdString(), dest_folder.toStdString());

    if (result.ok) {
        emit statusMessage(QStringLiteral("Extracted %1 files to %2").arg(result.extracted).arg(QFileInfo(dest_folder).fileName()));
        refresh();
    } else {
        emit statusMessage(QStringLiteral("Extraction failed: %1").arg(QString::fromStdString(result.error)));
    }
}

void MobileAssetBrowser::on_view_archive(const QString& archive_path) {
    std::vector<zip::Entry> entries;
    if (!zip::read_entries(archive_path.toStdString(), entries)) {
        QMessageBox::warning(this, QStringLiteral("Archive Error"), QStringLiteral("Could not read archive headers!"));
        return;
    }

    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("Archive: ") + QFileInfo(archive_path).fileName());
    dlg->resize(440, 520);
    dlg->setStyleSheet(QStringLiteral("QDialog { background: #12151D; color: #FFFFFF; }"));

    auto* layout = new QVBoxLayout(dlg);
    auto* summary_lbl = new QLabel(dlg);
    size_t total_uncomp = 0;
    size_t total_comp = 0;
    for (const auto& e : entries) {
        total_uncomp += e.uncomp_size;
        total_comp += e.comp_size;
    }
    summary_lbl->setText(QStringLiteral("<b>%1</b> items | Uncompressed: %2 | Compressed: %3")
                             .arg(entries.size())
                             .arg(format_file_size(total_uncomp))
                             .arg(format_file_size(total_comp)));
    summary_lbl->setStyleSheet(QStringLiteral("color: #9BB3DE; padding: 4px; font-size: 12px;"));
    layout->addWidget(summary_lbl);

    auto* list = new QListWidget(dlg);
    list->setStyleSheet(QStringLiteral("background: #181C26; border: 1px solid #283042; border-radius: 6px; color: #D8E0F0; font-size: 11px;"));
    for (const auto& e : entries) {
        QString item_text = QStringLiteral("%1   [%2  deflated to  %3]")
                                .arg(QString::fromStdString(e.name))
                                .arg(format_file_size(e.uncomp_size))
                                .arg(format_file_size(e.comp_size));
        list->addItem(item_text);
    }
    layout->addWidget(list, 1);

    auto* btn_box = new QHBoxLayout();
    auto* btn_extract = new QPushButton(QStringLiteral("Extract All Here"), dlg);
    btn_extract->setStyleSheet(QStringLiteral("background: #255A8A; color: #FFFFFF; font-weight: bold; padding: 8px; border-radius: 6px;"));
    connect(btn_extract, &QPushButton::clicked, dlg, [this, dlg, archive_path]() {
        dlg->accept();
        on_extract_archive(archive_path, false);
    });

    auto* btn_close = new QPushButton(QStringLiteral("Close"), dlg);
    btn_close->setStyleSheet(QStringLiteral("background: #242938; color: #FFFFFF; padding: 8px; border-radius: 6px;"));
    connect(btn_close, &QPushButton::clicked, dlg, &QDialog::reject);

    btn_box->addWidget(btn_extract);
    btn_box->addWidget(btn_close);
    layout->addLayout(btn_box);

    dlg->exec();
    dlg->deleteLater();
}

void MobileAssetBrowser::on_compress_zip(const QString& target_path) {
    QFileInfo fi(target_path);
    QString default_zip_name = fi.fileName() + QStringLiteral(".zip");
    bool ok = false;
    QString zip_name = QInputDialog::getText(
        this, QStringLiteral("Compress to ZIP"),
        QStringLiteral("Archive name:"), QLineEdit::Normal,
        default_zip_name, &ok);
    if (!ok || zip_name.trimmed().isEmpty()) return;
    if (!zip_name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
        zip_name += QStringLiteral(".zip");
    }

    const QString out_zip = m_current_dir.filePath(zip_name.trimmed());

    emit statusMessage(QStringLiteral("Compressing %1...").arg(fi.fileName()));
    std::vector<zip::OutEntry> entries;

    if (fi.isDir()) {
        if (!zip_folder_recursive(target_path, target_path, entries)) {
            emit statusMessage(QStringLiteral("Failed to read folder files for compression!"));
            return;
        }
    } else if (fi.isFile()) {
        QFile f(target_path);
        if (f.open(QIODevice::ReadOnly)) {
            QByteArray bytes = f.readAll();
            f.close();
            zip::OutEntry e;
            e.name = fi.fileName().toStdString();
            e.data = std::string(bytes.constData(), bytes.size());
            e.method = 8;
            entries.push_back(std::move(e));
        }
    }

    if (!entries.empty() && zip::write_archive(out_zip.toStdString(), entries)) {
        emit statusMessage(QStringLiteral("Created archive: %1 (%2 files)").arg(zip_name).arg(entries.size()));
        refresh();
    } else {
        emit statusMessage(QStringLiteral("Failed to create ZIP archive!"));
    }
}

void MobileAssetBrowser::on_rename_item(const QString& target_path) {
    QFileInfo fi(target_path);
    bool ok = false;
    QString new_name = QInputDialog::getText(
        this, QStringLiteral("Rename"),
        QStringLiteral("New name:"), QLineEdit::Normal,
        fi.fileName(), &ok);

    if (ok && !new_name.trimmed().isEmpty() && new_name.trimmed() != fi.fileName()) {
        const QString new_path = fi.dir().filePath(new_name.trimmed());
        if (QFile::rename(target_path, new_path)) {
            emit statusMessage(QStringLiteral("Renamed to: %1").arg(new_name));
            refresh();
        } else {
            emit statusMessage(QStringLiteral("Rename failed!"));
        }
    }
}

void MobileAssetBrowser::on_delete_item(const QString& target_path) {
    QFileInfo fi(target_path);
    auto res = QMessageBox::warning(
        this, QStringLiteral("Delete Confirmation"),
        QStringLiteral("Permanently delete '%1'?").arg(fi.fileName()),
        QMessageBox::Yes | QMessageBox::No);

    if (res == QMessageBox::Yes) {
        bool ok = false;
        if (fi.isDir()) {
            ok = QDir(target_path).removeRecursively();
        } else {
            ok = QFile::remove(target_path);
        }

        if (ok) {
            emit statusMessage(QStringLiteral("Deleted: %1").arg(fi.fileName()));
            refresh();
        } else {
            emit statusMessage(QStringLiteral("Failed to delete!"));
        }
    }
}

void MobileAssetBrowser::on_show_properties(const QString& target_path) {
    QFileInfo fi(target_path);
    QString info;
    info += QStringLiteral("<b>Name:</b> %1<br>").arg(fi.fileName());
    info += QStringLiteral("<b>Path:</b> %1<br>").arg(fi.absoluteFilePath());

    if (fi.isDir()) {
        int files = 0;
        int dirs = 0;
        qint64 total_size = calculate_dir_size(target_path, files, dirs);
        info += QStringLiteral("<b>Type:</b> Directory<br>");
        info += QStringLiteral("<b>Total Size:</b> %1 (%2 bytes)<br>").arg(format_file_size(total_size)).arg(total_size);
        info += QStringLiteral("<b>Contains:</b> %1 files, %2 subdirectories<br>").arg(files).arg(dirs);
    } else {
        info += QStringLiteral("<b>Type:</b> %1 file<br>").arg(fi.suffix().isEmpty() ? QStringLiteral("Binary") : fi.suffix().toUpper());
        info += QStringLiteral("<b>Size:</b> %1 (%2 bytes)<br>").arg(format_file_size(fi.size())).arg(fi.size());

        const QString ext = fi.suffix().toLower();
        if (ext == QStringLiteral("zip") || ext == QStringLiteral("apk")) {
            std::vector<zip::Entry> entries;
            if (zip::read_entries(target_path.toStdString(), entries)) {
                size_t uncomp = 0;
                for (const auto& e : entries) uncomp += e.uncomp_size;
                info += QStringLiteral("<b>Archive Entries:</b> %1 files<br>").arg(entries.size());
                info += QStringLiteral("<b>Uncompressed Size:</b> %1<br>").arg(format_file_size(uncomp));
                if (fi.size() > 0) {
                    double ratio = 100.0 * (1.0 - (double)fi.size() / (double)uncomp);
                    info += QStringLiteral("<b>Compression Ratio:</b> %1%<br>").arg(QString::number(ratio, 'f', 1));
                }
            }
        }
    }

    info += QStringLiteral("<b>Modified:</b> %1<br>").arg(fi.lastModified().toString(Qt::ISODate));
    info += QStringLiteral("<b>Readable:</b> %1 | <b>Writable:</b> %2 | <b>Executable:</b> %3")
                .arg(fi.isReadable() ? QStringLiteral("Yes") : QStringLiteral("No"))
                .arg(fi.isWritable() ? QStringLiteral("Yes") : QStringLiteral("No"))
                .arg(fi.isExecutable() ? QStringLiteral("Yes") : QStringLiteral("No"));

    QMessageBox::information(this, QStringLiteral("Properties"), info);
}

} // namespace ruby::android
