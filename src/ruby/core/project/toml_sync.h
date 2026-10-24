#pragma once
// ============================================================================
// toml_sync.h - properties.toml synchronization for the external Launcher.
//
// properties.toml is the Launcher partner contract, not a Ruby project file.
// Ruby reads and writes exactly five standard keys inside the [mod] table:
//
//   [mod]
//   id = "net.aevora.mod.crystal"
//   name = "Crystal Caverns"
//   author = "Aevora"
//   version = "1.0.0"
//   description = "A short overview."
//
// Everything else in the file (comments, other sections such as [store], and
// partner-owned keys such as category or screenshots) is preserved verbatim.
// Ruby-owned data - rbpkg dependencies, target ABIs, compiler pipelines,
// session statistics, and internal paths - belongs in the .rbp descriptor and
// is deliberately not representable through this API.
//
// Values are emitted in the subset the Launcher parser accepts: basic
// "strings", literal 'strings' when a backslash must stay verbatim, and
// """multi-line""" strings for values with quotes or line breaks. The Launcher
// trims multi-line values, so Ruby mirrors that trim when reading them back.
// Ruby never writes bare values or tables of its own.
// ============================================================================

#include <QByteArray>
#include <QString>

namespace ruby::project {

inline constexpr const char* kPropertiesFileName = "properties.toml";

// The Launcher-visible subset of [mod]. Empty fields mean the key is absent
// (on read) or should be written as an empty string (on write).
struct LauncherMetadata {
    QString id;
    QString name;
    QString author;
    QString version;
    QString description;
};

enum class TomlSyncError {
    None = 0,
    FileNotFound,
    FileTooLarge,
    IoError,
    MissingModSection,
    InvalidSyntax,
    UnrepresentableValue,
    AtomicRenameFailed
};

// Human-readable description for logging and UI. ASCII only.
QString toml_sync_error_string(TomlSyncError error);

// Parses the five Launcher keys from the [mod] table of `content`. Keys are
// case-sensitive (as TOML requires); a repeated key follows the Launcher's
// last-wins behavior. `out` is only modified when parsing succeeds.
TomlSyncError parse_properties_toml(const QByteArray& content,
                                    LauncherMetadata& out,
                                    QString* error_message = nullptr);

// Reads properties.toml from disk. A missing file reports FileNotFound; an
// existing file without a [mod] table reports MissingModSection.
TomlSyncError read_properties_toml(const QString& path, LauncherMetadata& out,
                                   QString* error_message = nullptr);

// Produces the updated manifest bytes without touching disk. `existing` may be
// empty to create a fresh file. Unknown content is preserved; only the five
// [mod] keys are added, replaced, or de-duplicated. On failure `out` is empty.
TomlSyncError format_properties_toml(const QByteArray& existing,
                                     const LauncherMetadata& metadata,
                                     QByteArray& out,
                                     QString* error_message = nullptr);

// Formats and saves atomically through `<path>.tmp`, like write_rbp().
TomlSyncError write_properties_toml(const QString& path,
                                    const LauncherMetadata& metadata,
                                    QString* error_message = nullptr);

} // namespace ruby::project
