#include "ruby_highlighter_bridge.h"
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QTextStream>
#include "tools/filerift.h"

namespace ruby::android {

// ─── DiagnosticsListModel Implementation ───────────────────────────────────

DiagnosticsListModel::DiagnosticsListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int DiagnosticsListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_list.size();
}

QVariant DiagnosticsListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_list.size()) {
        return QVariant();
    }

    const auto& diag = m_list.at(index.row());
    switch (role) {
    case LineRole: return diag.line;
    case SeverityRole: return diag.severity;
    case MessageRole: return diag.message;
    default: return QVariant();
    }
}

QHash<int, QByteArray> DiagnosticsListModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[LineRole] = "line";
    roles[SeverityRole] = "severity";
    roles[MessageRole] = "message";
    return roles;
}

void DiagnosticsListModel::setDiagnostics(const std::vector<ruby::filerift::Diagnostic>& diags) {
    beginResetModel();
    m_list.clear();
    for (const auto& d : diags) {
        QmlDiagnostic item;
        item.line = d.line;
        item.severity = (d.severity == ruby::filerift::Diagnostic::Error) ? QStringLiteral("error") : QStringLiteral("warning");
        item.message = QString::fromStdString(d.message);
        m_list.append(item);
    }
    endResetModel();
}

void DiagnosticsListModel::clear() {
    beginResetModel();
    m_list.clear();
    endResetModel();
}

// ─── RubyHighlighterBridge Implementation ──────────────────────────────────

RubyHighlighterBridge::RubyHighlighterBridge(QObject* parent)
    : QObject(parent)
{
}

void RubyHighlighterBridge::setDocument(QQuickTextDocument* doc) {
    if (m_quickDocument == doc) return;
    m_quickDocument = doc;

    if (m_quickDocument && m_quickDocument->textDocument()) {
        m_highlighter = std::make_unique<ruby::filerift::FileRiftHighlighter>(m_quickDocument->textDocument());
        applyMode();
    } else {
        m_highlighter.reset();
    }

    emit documentChanged();
}

void RubyHighlighterBridge::setFilePath(const QString& path) {
    if (m_filePath == path) return;
    m_filePath = path;
    applyMode();
    emit filePathChanged();
}

void RubyHighlighterBridge::applyMode() {
    if (!m_highlighter) return;
    QFileInfo fi(m_filePath);
    QString ext = fi.suffix().toLower();
    m_highlighter->set_is_pure_lua(ext == QStringLiteral("lua"));
}

// ─── RubyCodeBridge Implementation ─────────────────────────────────────────

RubyCodeBridge::RubyCodeBridge(QObject* parent)
    : QObject(parent), m_diagnosticsModel(this)
{
}

int RubyCodeBridge::diagnosticCount() const {
    return m_diagnosticsModel.rowCount();
}

bool RubyCodeBridge::loadFile(const QString& path) {
    m_isLoading = true;
    emit loadingChanged();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_isLoading = false;
        emit loadingChanged();
        emit statusMessage(QStringLiteral("Failed to open file: %1").arg(QFileInfo(path).fileName()));
        return false;
    }

    QByteArray bytes = file.readAll();
    file.close();

    m_path = path;
    QFileInfo fi(path);
    QString ext = fi.suffix().toLower();
    const bool is_scl_or_scene = (ext == QStringLiteral("scene") || ext == QStringLiteral("scl"));

    // Check if FileRift binary protobuf transcoding is needed for .scene / .scl
    bool is_already_text = bytes.startsWith("## FileRift") ||
                           bytes.startsWith("syntax =") ||
                           bytes.startsWith("scl ") ||
                           bytes.startsWith("scene ") ||
                           bytes.startsWith("Object{");

    if (is_scl_or_scene) {
        const std::string schema_type = (ext == QStringLiteral("scene")) ? "scene" : "scl";
        if (is_already_text) {
            m_content = QString::fromUtf8(bytes);
            m_isFileriftTranscoded = true;
        } else {
            try {
                std::string decoded_text = ::filerift::decode_protobuf(std::string(bytes.constData(), bytes.size()), schema_type);
                if (!decoded_text.empty()) {
                    m_content = QString::fromStdString(decoded_text);
                    m_isFileriftTranscoded = true;
                } else {
                    m_content = QString::fromUtf8(bytes);
                    m_isFileriftTranscoded = false;
                }
            } catch (...) {
                m_content = QString::fromUtf8(bytes);
                m_isFileriftTranscoded = false;
            }
        }
    } else {
        m_content = QString::fromUtf8(bytes);
        m_isFileriftTranscoded = false;
    }

    if (ext == QStringLiteral("lua")) {
        m_formatName = QStringLiteral("Lua Script");
    } else if (ext == QStringLiteral("scl")) {
        m_formatName = m_isFileriftTranscoded ? QStringLiteral("Swordigo Template (FileRift)") : QStringLiteral("Swordigo Template");
    } else if (ext == QStringLiteral("scene")) {
        m_formatName = m_isFileriftTranscoded ? QStringLiteral("Swordigo Scene (FileRift)") : QStringLiteral("Swordigo Scene");
    } else if (ext == QStringLiteral("filerift")) {
        m_formatName = QStringLiteral("FileRift Schema");
    } else if (ext == QStringLiteral("json")) {
        m_formatName = QStringLiteral("JSON Document");
    } else {
        m_formatName = QStringLiteral("Plain Text");
    }

    analyzeText(m_content);

    m_isDirty = false;
    emit dirtyChanged();

    m_isLoading = false;
    emit loadingChanged();
    emit fileChanged();
    return true;
}

bool RubyCodeBridge::saveFile(const QString& newContent) {
    if (m_path.isEmpty()) return false;

    QSaveFile save_file(m_path);
    if (!save_file.open(QIODevice::WriteOnly)) {
        emit statusMessage(QStringLiteral("Error opening save file for: %1").arg(QFileInfo(m_path).fileName()));
        return false;
    }

    if (m_isFileriftTranscoded) {
        QFileInfo fi(m_path);
        const std::string schema_type = (fi.suffix().toLower() == QStringLiteral("scene")) ? "scene" : "scl";
        try {
            std::string binary_str = ::filerift::recode_markup(newContent.toStdString(), schema_type);
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
        save_file.write(newContent.toUtf8());
    }

    if (!save_file.commit()) {
        emit statusMessage(QStringLiteral("Failed to commit save file!"));
        return false;
    }

    m_content = newContent;
    m_isDirty = false;
    emit dirtyChanged();
    analyzeText(m_content);
    emit fileChanged();
    emit statusMessage(QStringLiteral("Saved successfully."));
    return true;
}

void RubyCodeBridge::markDirty() {
    if (!m_isLoading && !m_isDirty) {
        m_isDirty = true;
        emit dirtyChanged();
    }
}

void RubyCodeBridge::clearDirty() {
    if (m_isDirty) {
        m_isDirty = false;
        emit dirtyChanged();
    }
}

void RubyCodeBridge::analyzeText(const QString& text) {
    if (!m_isLoading && text != m_content) {
        if (!m_isDirty) {
            m_isDirty = true;
            emit dirtyChanged();
        }
    }

    QStringList lines = text.split(QLatin1Char('\n'));
    std::vector<std::string> stdLines;
    stdLines.reserve(lines.size());
    for (const auto& l : lines) {
        stdLines.push_back(l.toStdString());
    }

    QFileInfo fi(m_path);
    std::string rootExt = fi.suffix().toLower().toStdString();
    auto result = ruby::filerift::FileRiftAnalyzer::analyze_lines(stdLines, rootExt);
    auto diags = ruby::filerift::FileRiftAnalyzer::compute_diagnostics(stdLines, result);

    // If lua file, perform compile-time Lua 5.1 syntax verification
    if (rootExt == "lua") {
        std::string err;
        std::string bc = ::filerift::compile_lua_to_bytecode(text.toStdString(), fi.fileName().toStdString(), &err);
        if (bc.empty() && !err.empty()) {

            int errLine = 1;
            auto c1 = err.find("]:");
            if (c1 != std::string::npos) {
                auto c2 = err.find(':', c1 + 2);
                if (c2 != std::string::npos) {
                    try {
                        errLine = std::stoi(err.substr(c1 + 2, c2 - (c1 + 2)));
                    } catch (...) {}
                }
            }
            ruby::filerift::Diagnostic d;
            d.line = errLine;
            d.severity = ruby::filerift::Diagnostic::Error;
            d.message = err;
            diags.push_back(d);
        }
    }

    m_diagnosticsModel.setDiagnostics(diags);
    emit diagnosticsChanged();
}

} // namespace ruby::android

