// ============================================================================
// mobile_code_editor.cpp — Single-Active-File Mobile Code & Markup Editor
// ============================================================================

#include "mobile_code_editor.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QFontDatabase>
#include <QScrollArea>
#include "tools/filerift.h"

namespace ruby::android {

MobileCodeEditor::MobileCodeEditor(QWidget* parent)
    : QWidget(parent)
{
    setup_ui();
}

void MobileCodeEditor::setup_ui() {
    auto* root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(10, 10, 10, 6);
    root_layout->setSpacing(8);

    // 1. Top Header Bar
    auto* header = new QHBoxLayout();
    header->setSpacing(8);

    auto* btn_back = new QPushButton(QStringLiteral("◀ Hub"), this);
    btn_back->setFixedSize(65, 36);
    btn_back->setStyleSheet(
        QStringLiteral("background: #242938; color: #FFFFFF; border: 1px solid #3A4259; "
                       "border-radius: 6px; font-size: 12px; font-weight: bold;"));

    m_lbl_title = new QLabel(QStringLiteral("No Document"), this);
    m_lbl_title->setStyleSheet(QStringLiteral("color: #E2E8F5; font-size: 14px; font-weight: bold;"));

    m_lbl_dirty = new QLabel(QStringLiteral(""), this);
    m_lbl_dirty->setStyleSheet(QStringLiteral("color: #FFB347; font-size: 16px; font-weight: bold;"));

    m_btn_visual = new QPushButton(QStringLiteral("3D View"), this);
    m_btn_visual->setFixedSize(85, 36);
    m_btn_visual->setStyleSheet(
        QStringLiteral("background: #2B5585; color: #FFFFFF; border: 1px solid #4075B5; "
                       "border-radius: 6px; font-size: 12px; font-weight: bold;"));

    m_btn_save = new QPushButton(QStringLiteral("Save"), this);
    m_btn_save->setFixedSize(65, 36);
    m_btn_save->setStyleSheet(
        QStringLiteral("background: #2D7A4D; color: #FFFFFF; border: 1px solid #3FA669; "
                       "border-radius: 6px; font-size: 12px; font-weight: bold;"));

    header->addWidget(btn_back);
    header->addWidget(m_lbl_title);
    header->addWidget(m_lbl_dirty);
    header->addStretch();
    header->addWidget(m_btn_visual);
    header->addWidget(m_btn_save);
    root_layout->addLayout(header);

    // 2. Editor Body
    m_text_edit = new QPlainTextEdit(this);
    QFont mono_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono_font.setPointSize(11);
    m_text_edit->setFont(mono_font);
    m_text_edit->setStyleSheet(
        QStringLiteral("QPlainTextEdit { background: #13151D; color: #D8E0F0; border: 1px solid #252A3B; "
                       "border-radius: 8px; padding: 8px; font-family: monospace; }"));
    root_layout->addWidget(m_text_edit, 1);

    // 3. Quick Symbol Bar for Mobile Soft Keyboards
    setup_quick_symbol_bar(root_layout);

    // Connections
    connect(btn_back, &QPushButton::clicked, this, &MobileCodeEditor::backToHubRequested);
    connect(m_btn_save, &QPushButton::clicked, this, &MobileCodeEditor::save_file);
    connect(m_btn_visual, &QPushButton::clicked, this, [this]() {
        emit switchToVisualRequested(m_file_path);
    });
    connect(m_text_edit, &QPlainTextEdit::textChanged, this, &MobileCodeEditor::on_text_changed);
}

void MobileCodeEditor::setup_quick_symbol_bar(QVBoxLayout* root_layout) {
    auto* symbol_scroll = new QScrollArea(this);
    symbol_scroll->setFixedHeight(44);
    symbol_scroll->setWidgetResizable(true);
    symbol_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    symbol_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    symbol_scroll->setStyleSheet(QStringLiteral("QScrollArea { background: #1A1D28; border: 1px solid #2B3145; border-radius: 6px; }"));

    auto* bar_widget = new QWidget(symbol_scroll);
    auto* bar_layout = new QHBoxLayout(bar_widget);
    bar_layout->setContentsMargins(6, 4, 6, 4);
    bar_layout->setSpacing(6);

    const QStringList symbols = {
        QStringLiteral("{"), QStringLiteral("}"),
        QStringLiteral("["), QStringLiteral("]"),
        QStringLiteral("\""), QStringLiteral(":"),
        QStringLiteral("$"), QStringLiteral("_"),
        QStringLiteral("="), QStringLiteral(","),
        QStringLiteral("("), QStringLiteral(")"),
        QStringLiteral("    ") // Tab
    };

    for (const auto& sym : symbols) {
        auto* b = new QPushButton(sym == QStringLiteral("    ") ? QStringLiteral("⇥ Tab") : sym, bar_widget);
        b->setFixedSize(38, 32);
        b->setStyleSheet(
            QStringLiteral("background: #252B3C; color: #7DB5F5; border: 1px solid #38425C; "
                           "border-radius: 4px; font-size: 13px; font-weight: bold;"));
        connect(b, &QPushButton::clicked, this, [this, sym]() { insert_symbol(sym); });
        bar_layout->addWidget(b);
    }
    bar_layout->addStretch();
    symbol_scroll->setWidget(bar_widget);
    root_layout->addWidget(symbol_scroll);
}

void MobileCodeEditor::insert_symbol(const QString& symbol) {
    m_text_edit->textCursor().insertText(symbol);
    m_text_edit->setFocus();
}

bool MobileCodeEditor::load_file(const QString& file_path) {
    m_is_loading = true;
    m_file_path = file_path;
    QFileInfo fi(file_path);
    m_lbl_title->setText(fi.fileName());

    const QString ext = fi.suffix().toLower();
    const bool is_scl_or_scene = (ext == QStringLiteral("scene") || ext == QStringLiteral("scl"));
    m_btn_visual->setVisible(is_scl_or_scene);

    QFile f(file_path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_is_loading = false;
        emit statusMessage(QStringLiteral("Error opening file: %1").arg(fi.fileName()));
        return false;
    }

    QByteArray bytes = f.readAll();
    f.close();

    // Check if FileRift transcoding is needed for binary .scene/.scl
    bool is_already_text = bytes.startsWith("## FileRift") ||
                           bytes.startsWith("syntax =") ||
                           bytes.startsWith("scl ") ||
                           bytes.startsWith("scene ") ||
                           bytes.startsWith("Object{");

    if (is_scl_or_scene) {
        const std::string schema_type = (ext == QStringLiteral("scene")) ? "scene" : "scl";
        if (is_already_text) {
            m_text_edit->setPlainText(QString::fromUtf8(bytes));
            m_is_filerift_transcoded = true;
        } else {
            try {
                std::string decoded_text = filerift::decode_protobuf(std::string(bytes.constData(), bytes.size()), schema_type);
                if (!decoded_text.empty()) {
                    m_text_edit->setPlainText(QString::fromStdString(decoded_text));
                    m_is_filerift_transcoded = true;
                } else {
                    m_text_edit->setPlainText(QString::fromUtf8(bytes));
                    m_is_filerift_transcoded = false;
                }
            } catch (...) {
                m_text_edit->setPlainText(QString::fromUtf8(bytes));
                m_is_filerift_transcoded = false;
            }
        }
    } else {
        m_text_edit->setPlainText(QString::fromUtf8(bytes));
        m_is_filerift_transcoded = false;
    }

    m_is_dirty = false;
    m_lbl_dirty->setText(QStringLiteral(""));
    m_is_loading = false;
    return true;
}

bool MobileCodeEditor::save_file() {
    if (m_file_path.isEmpty()) return false;

    QSaveFile save_file(m_file_path);
    if (!save_file.open(QIODevice::WriteOnly)) {
        emit statusMessage(QStringLiteral("Error opening save file!"));
        return false;
    }

    const QString text = m_text_edit->toPlainText();

    if (m_is_filerift_transcoded) {
        // Encode FileRift markup back to binary
        QFileInfo fi(m_file_path);
        const std::string schema_type = (fi.suffix().toLower() == QStringLiteral("scene")) ? "scene" : "scl";
        try {
            std::string binary_str = filerift::recode_markup(text.toStdString(), schema_type);
            if (binary_str.empty()) {
                emit statusMessage(QStringLiteral("FileRift encode error: syntax error in markup!"));
                save_file.cancelWriting();
                return false;
            }
            save_file.write(binary_str.data(), binary_str.size());
        } catch (const std::exception& e) {
            emit statusMessage(QStringLiteral("FileRift syntax error: ") + QString::fromUtf8(e.what()));
            save_file.cancelWriting();
            return false;
        }
    } else {
        save_file.write(text.toUtf8());
    }

    if (!save_file.commit()) {
        emit statusMessage(QStringLiteral("Failed to commit save!"));
        return false;
    }

    m_is_dirty = false;
    m_lbl_dirty->setText(QStringLiteral(""));
    emit fileDirtyStateChanged(false);
    emit statusMessage(QStringLiteral("File saved."));
    return true;
}

void MobileCodeEditor::on_text_changed() {
    if (m_is_loading) return;
    if (!m_is_dirty) {
        m_is_dirty = true;
        m_lbl_dirty->setText(QStringLiteral("●"));
        emit fileDirtyStateChanged(true);
    }
}

} // namespace ruby::android
