// ============================================================================
// toml_sync.cpp - properties.toml synchronization for the external Launcher.
//
// The parser is intentionally narrow: it recognizes the [mod] table, the five
// Launcher keys, and enough TOML string syntax to read and rewrite those keys
// without disturbing anything else in the file. Comments, unknown keys, other
// tables, and their multi-line values are copied through untouched.
// ============================================================================

#include "toml_sync.h"

#include "rbp_format.h"

#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QStringConverter>
#include <QStringList>
#include <QVector>

namespace ruby::project {

namespace {

constexpr qint64 kMaxPropertiesFileSize = 1024 * 1024;

const char* const kLauncherKeys[] = {
    "id",
    "name",
    "author",
    "version",
    "description",
};
constexpr int kLauncherKeyCount =
    static_cast<int>(sizeof(kLauncherKeys) / sizeof(kLauncherKeys[0]));

void set_error(QString* error_message, const QString& message) {
    if (error_message) {
        *error_message = message;
    }
}

TomlSyncError fail(TomlSyncError error, const QString& message,
                   QString* error_message) {
    set_error(error_message, message);
    return error;
}

int launcher_key_index(const QString& key) {
    for (int index = 0; index < kLauncherKeyCount; ++index) {
        if (key == QLatin1String(kLauncherKeys[index])) {
            return index;
        }
    }
    return -1;
}

void set_metadata_value(LauncherMetadata& metadata, int index,
                        const QString& value) {
    switch (index) {
    case 0:
        metadata.id = value;
        break;
    case 1:
        metadata.name = value;
        break;
    case 2:
        metadata.author = value;
        break;
    case 3:
        metadata.version = value;
        break;
    case 4:
        metadata.description = value;
        break;
    default:
        break;
    }
}

bool is_bare_key(const QString& key) {
    if (key.isEmpty()) {
        return false;
    }
    for (QChar ch : key) {
        if (!(ch.isLetterOrNumber() || ch == QLatin1Char('_') ||
              ch == QLatin1Char('-'))) {
            return false;
        }
    }
    return true;
}

// One physical statement or section header. A statement may own several
// lines when its value uses a triple-quoted string.
struct LineSpan {
    int first = 0;
    int last = 0;
    bool is_section = false;
    bool is_statement = false;
    QString section_name;
    QString key;
};

QVector<LineSpan> scan_spans(const QStringList& lines) {
    QVector<LineSpan> spans;
    spans.reserve(lines.size());

    int index = 0;
    while (index < lines.size()) {
        LineSpan span;
        span.first = index;
        span.last = index;

        const QString trimmed = lines.at(index).trimmed();
        if (trimmed.size() >= 2 && trimmed.startsWith(QLatin1Char('[')) &&
            trimmed.endsWith(QLatin1Char(']'))) {
            span.is_section = true;
            span.section_name = trimmed.mid(1, trimmed.size() - 2).trimmed();
            spans.append(span);
            ++index;
            continue;
        }

        if (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1Char('#'))) {
            const int equals = trimmed.indexOf(QLatin1Char('='));
            if (equals > 0) {
                const QString key = trimmed.left(equals).trimmed();
                if (is_bare_key(key)) {
                    span.is_statement = true;
                    span.key = key;

                    // Consume triple-quoted values here so a body line that
                    // looks like a table header is never mistaken for one.
                    const QString value = trimmed.mid(equals + 1).trimmed();
                    const bool multiline_basic =
                        value.startsWith(QStringLiteral("\"\"\""));
                    const bool multiline_literal =
                        value.startsWith(QStringLiteral("'''"));
                    if (multiline_basic || multiline_literal) {
                        const QString delimiter = value.left(3);
                        if (!value.mid(3).contains(delimiter)) {
                            int next = index + 1;
                            while (next < lines.size() &&
                                   !lines.at(next).contains(delimiter)) {
                                ++next;
                            }
                            span.last = (next < lines.size()) ? next
                                                              : lines.size() - 1;
                        }
                    }
                }
            }
        }

        spans.append(span);
        index = span.last + 1;
    }
    return spans;
}

QString decode_basic_escapes(const QString& raw) {
    QString decoded;
    decoded.reserve(raw.size());

    for (int index = 0; index < raw.size(); ++index) {
        const QChar ch = raw.at(index);
        if (ch != QLatin1Char('\\') || index + 1 >= raw.size()) {
            decoded.append(ch);
            continue;
        }

        const QChar escape = raw.at(++index);
        switch (escape.unicode()) {
        case 'b':
            decoded.append(QLatin1Char('\b'));
            break;
        case 't':
            decoded.append(QLatin1Char('\t'));
            break;
        case 'n':
            decoded.append(QLatin1Char('\n'));
            break;
        case 'f':
            decoded.append(QLatin1Char('\f'));
            break;
        case 'r':
            decoded.append(QLatin1Char('\r'));
            break;
        case '"':
            decoded.append(QLatin1Char('"'));
            break;
        case '\\':
            decoded.append(QLatin1Char('\\'));
            break;
        case 'u':
        case 'U': {
            const int digits = (escape == QLatin1Char('u')) ? 4 : 8;
            if (index + digits < raw.size()) {
                bool ok = false;
                const uint code_point =
                    raw.mid(index + 1, digits).toUInt(&ok, 16);
                if (ok && code_point <= 0x10FFFFu) {
                    const char32_t code = static_cast<char32_t>(code_point);
                    decoded.append(QString::fromUcs4(&code, 1));
                    index += digits;
                    break;
                }
            }
            decoded.append(QLatin1Char('\\')).append(escape);
            break;
        }
        default:
            // Preserve unknown escapes instead of silently dropping them.
            decoded.append(QLatin1Char('\\')).append(escape);
            break;
        }
    }
    return decoded;
}

bool decode_toml_value(const QString& raw, QString& value) {
    if (raw.startsWith(QStringLiteral("\"\"\""))) {
        if (raw.size() < 6 || !raw.endsWith(QStringLiteral("\"\"\""))) {
            return false;
        }
        QString body = raw.mid(3, raw.size() - 6);
        if (body.startsWith(QStringLiteral("\r\n"))) {
            body.remove(0, 2);
        } else if (body.startsWith(QLatin1Char('\n'))) {
            body.remove(0, 1);
        }
        body.replace(QStringLiteral("\r"), QString());
        // The Launcher trims multi-line values; mirror that so a write/read
        // cycle stays stable.
        value = body.trimmed();
        return true;
    }

    if (raw.startsWith(QStringLiteral("'''"))) {
        if (raw.size() < 6 || !raw.endsWith(QStringLiteral("'''"))) {
            return false;
        }
        QString body = raw.mid(3, raw.size() - 6);
        if (body.startsWith(QStringLiteral("\r\n"))) {
            body.remove(0, 2);
        } else if (body.startsWith(QLatin1Char('\n'))) {
            body.remove(0, 1);
        }
        body.replace(QStringLiteral("\r"), QString());
        value = body.trimmed();
        return true;
    }

    if (raw.startsWith(QLatin1Char('"'))) {
        int closing = -1;
        for (int index = 1; index < raw.size(); ++index) {
            if (raw.at(index) == QLatin1Char('\\')) {
                ++index;
                continue;
            }
            if (raw.at(index) == QLatin1Char('"')) {
                closing = index;
                break;
            }
        }
        if (closing < 0) {
            return false;
        }
        value = decode_basic_escapes(raw.mid(1, closing - 1));
        return true;
    }

    if (raw.startsWith(QLatin1Char('\''))) {
        const int closing = raw.indexOf(QLatin1Char('\''), 1);
        if (closing < 0) {
            return false;
        }
        value = raw.mid(1, closing - 1);
        return true;
    }

    // Bare value. The Launcher only writes strings, but tolerating this keeps
    // reads non-destructive on hand-edited files.
    value = raw.trimmed();
    return true;
}

TomlSyncError encode_value(const QString& key, const QString& value,
                           QStringList& lines, QString* error_message) {
    for (QChar ch : value) {
        const char16_t code = ch.unicode();
        if ((code < 0x20 && code != '\t' && code != '\n') || code == 0x7F) {
            return fail(TomlSyncError::UnrepresentableValue,
                        QStringLiteral("The [mod].%1 value contains a control "
                                       "character that properties.toml cannot "
                                       "represent")
                            .arg(key),
                        error_message);
        }
    }

    const bool has_newline = value.contains(QLatin1Char('\n'));
    const bool has_double_quote = value.contains(QLatin1Char('"'));
    const bool has_backslash = value.contains(QLatin1Char('\\'));

    if (!has_newline && !has_double_quote && !has_backslash) {
        lines.append(key + QStringLiteral(" = \"") + value + QLatin1Char('"'));
        return TomlSyncError::None;
    }

    if (!has_newline && !has_double_quote && has_backslash &&
        !value.contains(QLatin1Char('\''))) {
        // A literal string keeps backslashes intact for TOML readers and for
        // the Launcher, which does not unescape values.
        lines.append(key + QStringLiteral(" = '") + value + QLatin1Char('\''));
        return TomlSyncError::None;
    }

    if (has_backslash || value.contains(QStringLiteral("\"\"\""))) {
        return fail(TomlSyncError::UnrepresentableValue,
                    QStringLiteral("The [mod].%1 value cannot be represented "
                                   "without escaping, which the Launcher parser "
                                   "does not support")
                        .arg(key),
                    error_message);
    }

    // Multi-line basic string. The delimiters sit on their own lines so that
    // content ending in a quote cannot merge with the closing delimiter.
    lines.append(key + QStringLiteral(" = \"\"\""));
    const QStringList body = value.split(QLatin1Char('\n'));
    for (const QString& body_line : body) {
        lines.append(body_line);
    }
    lines.append(QStringLiteral("\"\"\""));
    return TomlSyncError::None;
}

QByteArray join_lines(const QStringList& lines) {
    return (lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
}

} // namespace

QString toml_sync_error_string(TomlSyncError error) {
    switch (error) {
    case TomlSyncError::None:
        return QStringLiteral("No error");
    case TomlSyncError::FileNotFound:
        return QStringLiteral("properties.toml not found");
    case TomlSyncError::FileTooLarge:
        return QStringLiteral("properties.toml is too large");
    case TomlSyncError::IoError:
        return QStringLiteral("properties.toml I/O error");
    case TomlSyncError::MissingModSection:
        return QStringLiteral("properties.toml has no [mod] table");
    case TomlSyncError::InvalidSyntax:
        return QStringLiteral("properties.toml has invalid syntax");
    case TomlSyncError::UnrepresentableValue:
        return QStringLiteral("A Launcher value cannot be represented in "
                              "properties.toml");
    case TomlSyncError::AtomicRenameFailed:
        return QStringLiteral("Could not replace properties.toml");
    }
    return QStringLiteral("Unknown properties.toml error");
}

TomlSyncError parse_properties_toml(const QByteArray& content,
                                    LauncherMetadata& out,
                                    QString* error_message) {
    QStringDecoder decoder(QStringConverter::Utf8);
    const QString text = decoder.decode(content);
    if (decoder.hasError()) {
        return fail(TomlSyncError::InvalidSyntax,
                    QStringLiteral("properties.toml is not valid UTF-8"),
                    error_message);
    }

    QStringList lines;
    if (!text.isEmpty()) {
        QString normalized = text;
        normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        lines = normalized.split(QLatin1Char('\n'));
        if (!lines.isEmpty() && lines.last().isEmpty()) {
            lines.removeLast();
        }
    }

    const QVector<LineSpan> spans = scan_spans(lines);
    bool in_mod = false;
    bool found_mod = false;
    LauncherMetadata parsed;

    for (const LineSpan& span : spans) {
        if (span.is_section) {
            in_mod = span.section_name.compare(QLatin1String("mod"),
                                               Qt::CaseInsensitive) == 0;
            found_mod = found_mod || in_mod;
            continue;
        }
        if (!in_mod || !span.is_statement) {
            continue;
        }

        const int key_index = launcher_key_index(span.key);
        if (key_index < 0) {
            continue;
        }

        const QString statement =
            lines.mid(span.first, span.last - span.first + 1)
                .join(QLatin1Char('\n'));
        const int equals = statement.indexOf(QLatin1Char('='));
        if (equals < 0) {
            continue;
        }

        QString value;
        if (!decode_toml_value(statement.mid(equals + 1).trimmed(), value)) {
            return fail(TomlSyncError::InvalidSyntax,
                        QStringLiteral("Malformed value for [mod].%1")
                            .arg(span.key),
                        error_message);
        }
        set_metadata_value(parsed, key_index, value);
    }

    if (!found_mod) {
        return fail(TomlSyncError::MissingModSection,
                    QStringLiteral("properties.toml has no [mod] table"),
                    error_message);
    }

    out = parsed;
    return TomlSyncError::None;
}

TomlSyncError read_properties_toml(const QString& path, LauncherMetadata& out,
                                   QString* error_message) {
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return fail(TomlSyncError::FileNotFound,
                    QStringLiteral("properties.toml not found: %1").arg(path),
                    error_message);
    }
    if (info.size() > kMaxPropertiesFileSize) {
        return fail(TomlSyncError::FileTooLarge,
                    QStringLiteral("properties.toml is %1 bytes; the limit is "
                                   "%2: %3")
                        .arg(info.size())
                        .arg(kMaxPropertiesFileSize)
                        .arg(path),
                    error_message);
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(TomlSyncError::IoError,
                    QStringLiteral("Cannot open %1: %2")
                        .arg(path, file.errorString()),
                    error_message);
    }
    const QByteArray content = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        return fail(TomlSyncError::IoError,
                    QStringLiteral("Cannot read %1: %2")
                        .arg(path, file.errorString()),
                    error_message);
    }

    QString parse_message;
    const TomlSyncError result =
        parse_properties_toml(content, out, &parse_message);
    if (result != TomlSyncError::None) {
        set_error(error_message,
                  QStringLiteral("%1: %2").arg(path, parse_message));
    }
    return result;
}

TomlSyncError format_properties_toml(const QByteArray& existing,
                                     const LauncherMetadata& metadata,
                                     QByteArray& out,
                                     QString* error_message) {
    out.clear();

    QStringList encoded[kLauncherKeyCount];
    const QString values[kLauncherKeyCount] = {
        metadata.id,
        metadata.name,
        metadata.author,
        metadata.version,
        metadata.description,
    };
    for (int index = 0; index < kLauncherKeyCount; ++index) {
        const TomlSyncError error =
            encode_value(QLatin1String(kLauncherKeys[index]), values[index],
                         encoded[index], error_message);
        if (error != TomlSyncError::None) {
            return error;
        }
    }

    QStringDecoder decoder(QStringConverter::Utf8);
    const QString text = decoder.decode(existing);
    if (decoder.hasError()) {
        return fail(TomlSyncError::InvalidSyntax,
                    QStringLiteral("properties.toml is not valid UTF-8"),
                    error_message);
    }

    QStringList lines;
    if (!text.isEmpty()) {
        QString normalized = text;
        normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        lines = normalized.split(QLatin1Char('\n'));
        if (!lines.isEmpty() && lines.last().isEmpty()) {
            lines.removeLast();
        }
    }

    const QVector<LineSpan> spans = scan_spans(lines);

    int mod_span = -1;
    for (int index = 0; index < spans.size(); ++index) {
        if (spans.at(index).is_section &&
            spans.at(index).section_name.compare(QLatin1String("mod"),
                                                 Qt::CaseInsensitive) == 0) {
            mod_span = index;
            break;
        }
    }

    QStringList result;
    result.reserve(lines.size() + kLauncherKeyCount * 3);

    if (mod_span < 0) {
        // No [mod] table yet: keep the file and append one.
        result = lines;
        while (!result.isEmpty() && result.last().trimmed().isEmpty()) {
            result.removeLast();
        }
        if (!result.isEmpty()) {
            result.append(QString());
        }
        result.append(QStringLiteral("[mod]"));
        for (int index = 0; index < kLauncherKeyCount; ++index) {
            result.append(encoded[index]);
        }
        out = join_lines(result);
        return TomlSyncError::None;
    }

    const int body_start = spans.at(mod_span).last + 1;
    int body_end = lines.size();
    for (int index = mod_span + 1; index < spans.size(); ++index) {
        if (!spans.at(index).is_section) {
            continue;
        }
        if (spans.at(index).section_name.compare(QLatin1String("mod"),
                                                 Qt::CaseInsensitive) == 0) {
            // A repeated [mod] table is malformed TOML, but the Launcher
            // reads all of them in order. Merge them into the first table so
            // stale keys cannot win at Launcher parse time.
            continue;
        }
        body_end = spans.at(index).first;
        break;
    }

    for (int index = 0; index < body_start; ++index) {
        result.append(lines.at(index));
    }

    QStringList body;
    bool seen[kLauncherKeyCount] = {false, false, false, false, false};
    for (const LineSpan& span : spans) {
        if (span.first < body_start || span.first >= body_end) {
            continue;
        }
        if (span.is_section) {
            continue; // drop repeated [mod] headers after merging
        }

        if (span.is_statement) {
            const int key_index = launcher_key_index(span.key);
            if (key_index >= 0) {
                if (seen[key_index]) {
                    continue; // drop duplicate Launcher keys
                }
                seen[key_index] = true;
                body.append(encoded[key_index]);
                continue;
            }
        }

        for (int line = span.first; line <= span.last; ++line) {
            body.append(lines.at(line));
        }
    }

    QStringList missing;
    for (int index = 0; index < kLauncherKeyCount; ++index) {
        if (!seen[index]) {
            missing.append(encoded[index]);
        }
    }
    if (!missing.isEmpty()) {
        int insert_at = body.size();
        while (insert_at > 0 && body.at(insert_at - 1).trimmed().isEmpty()) {
            --insert_at;
        }
        for (int index = 0; index < missing.size(); ++index) {
            body.insert(insert_at + index, missing.at(index));
        }
    }

    result.append(body);
    for (int index = body_end; index < lines.size(); ++index) {
        result.append(lines.at(index));
    }

    out = join_lines(result);
    return TomlSyncError::None;
}

TomlSyncError write_properties_toml(const QString& path,
                                    const LauncherMetadata& metadata,
                                    QString* error_message) {
    QByteArray existing;
    const QFileInfo info(path);

    if (info.exists()) {
        if (!info.isFile()) {
            return fail(TomlSyncError::IoError,
                        QStringLiteral("Not a regular file: %1").arg(path),
                        error_message);
        }
        if (info.size() > kMaxPropertiesFileSize) {
            return fail(TomlSyncError::FileTooLarge,
                        QStringLiteral("properties.toml is %1 bytes; the limit "
                                       "is %2: %3")
                            .arg(info.size())
                            .arg(kMaxPropertiesFileSize)
                            .arg(path),
                        error_message);
        }

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return fail(TomlSyncError::IoError,
                        QStringLiteral("Cannot open %1: %2")
                            .arg(path, file.errorString()),
                        error_message);
        }
        existing = file.readAll();
        if (file.error() != QFileDevice::NoError) {
            return fail(TomlSyncError::IoError,
                        QStringLiteral("Cannot read %1: %2")
                            .arg(path, file.errorString()),
                        error_message);
        }
    }

    QByteArray formatted;
    const TomlSyncError format_error =
        format_properties_toml(existing, metadata, formatted, error_message);
    if (format_error != TomlSyncError::None) {
        return format_error;
    }

    const RbpError write_error =
        detail::write_file_atomic(path, formatted, error_message);
    switch (write_error) {
    case RbpError::None:
        return TomlSyncError::None;
    case RbpError::AtomicRenameFailed:
        return TomlSyncError::AtomicRenameFailed;
    default:
        return TomlSyncError::IoError;
    }
}

} // namespace ruby::project
